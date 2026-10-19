/* T7b-b1 S1: font の read lease の VFS 部 (fs/vfs_fd.c を実物で組む)。
 * 設計 ~/os32-tmp/run/t7bb12/DESIGN.final.md §2-2 / §5 の b1②④⑤⑦ の VFS 側。
 * lease 表・KAPI の門 (kernel/font_source.c) は S2 でこのファイルに足す。
 * 贋物は FS backend・TTY・owner の境界だけ (vfs_fd_sqlite_host.c と同じ流儀)。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../fs/vfs_fd.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)

#define MOCK_FILE_SIZE 64u
#define MOCK_INO       42u
#define SHELL_OWNER    1
#define APP_OWNER      5

static int current_owner = 7;     /* res_owner_get() — font open は見ない */
static int kind_rc = VFS_KIND_FILE, size_rc = VFS_OK, read_mode;
static int read_calls;
static u32 last_off, last_len;
static u8 file_data[MOCK_FILE_SIZE];
static VfsOps mock_ops;
static VfsInoOps mock_ino;
static int mock_ctx;

enum { READ_FULL = 0, READ_SHORT = 1, READ_IO = 2 };

int res_owner_get(void) { return current_owner; }
void res_owner_set(int owner) { current_owner = owner; }
const char *vfs_cwd(void) { return "/"; }
int vfs_resolve_path(const char *in, char *out, int size)
{
    if (!in || !out || size <= 0) return VFS_ERR_INVAL;
    if ((int)strlen(in) + 1 > size) return VFS_ERR_NAMETOOLONG;
    str_cpy(out, in, size);
    return VFS_OK;
}
VfsOps *vfs_route(const char *path, char *out, int size, void **ctx)
{ str_cpy(out, path, size); *ctx = &mock_ctx; return &mock_ops; }
int vfs_path_kind(const char *path) { return kind_rc; }
u32 vfs_path_dev(const char *path) { return 1; }
u32 vfs_path_mount_generation(const char *path) { return 1; }
int vfs_sqlite_immutable_generation(void *ctx, u32 generation, u32 *out)
{ return VFS_ERR_ROFS; }
static int mock_size(void *ctx, const char *path, u32 *size)
{ *size = MOCK_FILE_SIZE; return size_rc; }
static int mock_write(void *ctx, const char *path, const void *buf, u32 size)
{ return VFS_ERR_IO; }
static int mock_read(void *dst, u32 len, u32 off)
{
    u32 n = len;
    read_calls++;
    last_off = off;
    last_len = len;
    if (read_mode == READ_IO) return VFS_ERR_IO;
    if (read_mode == READ_SHORT && n > 0) n--;
    if (off > MOCK_FILE_SIZE) return 0;
    if (n > MOCK_FILE_SIZE - off) n = MOCK_FILE_SIZE - off;
    memcpy(dst, file_data + off, n);
    return (int)n;
}
static int mock_read_stream(void *ctx, const char *path, void *buf,
                            u32 size, u32 offset)
{ return mock_read(buf, size, offset); }
static int mock_write_stream(void *ctx, const char *path, const void *buf,
                             u32 size, u32 offset)
{ return (int)size; }
static int mock_ino_lookup(void *ctx, const char *path, u32 *ino)
{ *ino = MOCK_INO; return VFS_OK; }
static int mock_ino_read(void *ctx, u32 ino, void *buf, u32 size, u32 offset)
{ CHECK(ino == MOCK_INO); return mock_read(buf, size, offset); }
int fd_is_redirected(int fd) { return 0; }
u16 fd_redirect_ifmt(int fd, int *out_file_fd)
{ (void)fd; if (out_file_fd) *out_file_fd = -1; return OS_S_IFCHR; }
int fd_redirect_read(int fd, void *buf, u32 size) { return VFS_ERR_INVAL; }
int fd_redirect_write(int fd, const void *buf, u32 size) { return VFS_ERR_INVAL; }
int kbd_getchar_kapi(void) { return '\n'; }
void console_write(const char *buf, u32 size, u8 color) { }

static void use_ino(int on) { mock_ops.ino = on ? &mock_ino : (VfsInoOps *)0; }

static void open_font_ok(int owner, u32 flags, VfsFontLease *l)
{
    u32 size = 0;
    CHECK(vfs_open_font("/f.ttf", owner, flags, l, &size) == VFS_OK);
    CHECK(size == MOCK_FILE_SIZE);
}

/* 開いた FD の印: lifetime / owner / protect / pinned / O_RDONLY / 世代。
 * owner は呼び手の固定値で res_owner_get() ではない */
static void open_fields(void)
{
    VfsFontLease a, s;
    open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &a);
    CHECK(a.fd >= 3 && a.owner == APP_OWNER);
    CHECK(open_files[a.fd].in_use && open_files[a.fd].lifetime == VFS_FD_FONT);
    CHECK(open_files[a.fd].owner == APP_OWNER && current_owner == 7);
    CHECK(open_files[a.fd].pinned == 1 && open_files[a.fd].protect == 0);
    CHECK((open_files[a.fd].mode & 3) == O_RDONLY);
    CHECK(a.generation == open_files[a.fd].generation && a.generation != 0);
    open_font_ok(SHELL_OWNER, VFS_FONT_OPEN_PROTECT | VFS_FONT_OPEN_PIN, &s);
    CHECK(s.fd != a.fd);
    CHECK(open_files[s.fd].pinned == 1 && open_files[s.fd].protect == 1);
    CHECK(open_files[s.fd].owner == SHELL_OWNER);
    CHECK(vfs_validate_font(&a) == VFS_OK && vfs_validate_font(&s) == VFS_OK);
}

/* 引数不正・通常ファイル以外・FD 枯渇。失敗では out を埋めない */
static void open_reject(void)
{
    VfsFontLease l, before;
    u32 size = 0xdeadbeefu;
    int i, fd;
    memset(&l, 0x5a, sizeof(l));
    before = l;
    CHECK(vfs_open_font((const char *)0, APP_OWNER, 0, &l, &size) == VFS_ERR_INVAL);
    CHECK(vfs_open_font("/f", APP_OWNER, 0, (VfsFontLease *)0, &size) == VFS_ERR_INVAL);
    CHECK(vfs_open_font("/f", APP_OWNER, 0, &l, (u32 *)0) == VFS_ERR_INVAL);
    CHECK(vfs_open_font("/f", -1, 0, &l, &size) == VFS_ERR_INVAL);
    CHECK(vfs_open_font("/f", APP_OWNER, 4u, &l, &size) == VFS_ERR_INVAL);
    kind_rc = VFS_KIND_DIR;
    CHECK(vfs_open_font("/d", APP_OWNER, 0, &l, &size) == VFS_ERR_ISDIR);
    kind_rc = VFS_ERR_NOTFOUND;
    size_rc = VFS_ERR_NOTFOUND;
    CHECK(vfs_open_font("/none", APP_OWNER, 0, &l, &size) == VFS_ERR_NOTFOUND);
    kind_rc = VFS_KIND_FILE;
    size_rc = VFS_OK;
    CHECK(!memcmp(&l, &before, sizeof(l)) && size == 0xdeadbeefu);
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++) CHECK(!open_files[i].in_use);
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++) {
        fd = vfs_open("/g", O_RDONLY);
        CHECK(fd == i);
    }
    CHECK(vfs_open_font("/f", APP_OWNER, 0, &l, &size) == VFS_ERR_NOSPC);
    CHECK(!memcmp(&l, &before, sizeof(l)));
}

/* b1②: 明示 offset で 1 回、位置を持たない、範囲外は INVAL、短読はそのまま */
static void read_at(void)
{
    VfsFontLease l;
    u8 buf[MOCK_FILE_SIZE + 8];
    int ino;
    for (ino = 0; ino < 2; ino++) {
        use_ino(ino);
        open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &l);
        CHECK(open_files[l.fd].has_ino == ino);
        read_mode = READ_FULL;
        read_calls = 0;
        CHECK(vfs_font_read_at(&l, 10, buf, 4) == 4);
        CHECK(read_calls == 1 && last_off == 10 && last_len == 4);
        CHECK(!memcmp(buf, file_data + 10, 4));
        CHECK(open_files[l.fd].offset == 0);
        CHECK(vfs_font_read_at(&l, 2, buf, 3) == 3 && last_off == 2);
        CHECK(!memcmp(buf, file_data + 2, 3));
        CHECK(vfs_font_read_at(&l, 0, buf, MOCK_FILE_SIZE) == (int)MOCK_FILE_SIZE);
        CHECK(vfs_font_read_at(&l, MOCK_FILE_SIZE, buf, 0) == 0);
        CHECK(vfs_font_read_at(&l, 5, buf, 0) == 0);
        read_calls = 0;
        CHECK(vfs_font_read_at(&l, MOCK_FILE_SIZE + 1, buf, 0) == VFS_ERR_INVAL);
        CHECK(vfs_font_read_at(&l, MOCK_FILE_SIZE - 1, buf, 2) == VFS_ERR_INVAL);
        CHECK(vfs_font_read_at(&l, 4, buf, 0xffffffffu) == VFS_ERR_INVAL);
        CHECK(vfs_font_read_at(&l, 0xffffffffu, buf, 2) == VFS_ERR_INVAL);
        CHECK(vfs_font_read_at(&l, 0, (void *)0, 4) == VFS_ERR_INVAL);
        CHECK(read_calls == 0);
        /* 短読: len - 1 をそのまま返し、再試行しない */
        read_mode = READ_SHORT;
        CHECK(vfs_font_read_at(&l, 8, buf, 6) == 5 && read_calls == 1);
        read_mode = READ_IO;
        CHECK(vfs_font_read_at(&l, 8, buf, 6) == VFS_ERR_IO && read_calls == 2);
        read_mode = READ_FULL;
        CHECK(vfs_close_font(&l) == VFS_OK);
    }
}

/* 照合: fd 範囲・in_use・lifetime・generation・owner は INVAL、stale は STALE */
static void validate(void)
{
    VfsFontLease l, w;
    u8 buf[4];
    int g;
    use_ino(1);
    open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &l);
    CHECK(vfs_validate_font((const VfsFontLease *)0) == VFS_ERR_INVAL);
    w = l; w.fd = 2;                    CHECK(vfs_validate_font(&w) == VFS_ERR_INVAL);
    w = l; w.fd = VFS_MAX_OPEN_FILES;   CHECK(vfs_validate_font(&w) == VFS_ERR_INVAL);
    w = l; w.generation++;              CHECK(vfs_validate_font(&w) == VFS_ERR_INVAL);
    CHECK(vfs_font_read_at(&w, 0, buf, 1) == VFS_ERR_INVAL);
    CHECK(vfs_close_font(&w) == VFS_ERR_INVAL && open_files[l.fd].in_use);
    w = l; w.owner = SHELL_OWNER;       CHECK(vfs_validate_font(&w) == VFS_ERR_INVAL);
    CHECK(vfs_font_read_at(&w, 0, buf, 1) == VFS_ERR_INVAL);
    CHECK(vfs_close_font(&w) == VFS_ERR_INVAL && open_files[l.fd].in_use);
    /* GENERIC の FD を FONT の lease で読めない */
    g = vfs_open("/g", O_RDONLY);
    w.fd = g; w.generation = open_files[g].generation; w.owner = current_owner;
    CHECK(vfs_validate_font(&w) == VFS_ERR_INVAL);
    CHECK(vfs_close_font(&w) == VFS_ERR_INVAL && open_files[g].in_use);
    vfs_fd_invalidate_ino(&mock_ctx, MOCK_INO);
    CHECK(vfs_validate_font(&l) == VFS_ERR_STALE);
    CHECK(vfs_font_read_at(&l, 0, buf, 1) == VFS_ERR_STALE);
}

/* b1④: close → 同 fd を同 owner の FONT で開き直し → 旧 lease は INVAL で
 * 新 FD を閉じない (generation だけが見分ける) */
static void close_reuse(void)
{
    VfsFontLease old, fresh;
    VfsFile before;
    u8 buf[4];
    open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &old);
    CHECK(vfs_close_font(&old) == VFS_OK);
    CHECK(!open_files[old.fd].in_use && !open_files[old.fd].pinned);
    CHECK(vfs_close_font(&old) == VFS_ERR_INVAL);
    open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &fresh);
    CHECK(fresh.fd == old.fd && fresh.owner == old.owner);
    CHECK(fresh.generation != old.generation);
    before = open_files[fresh.fd];
    CHECK(vfs_validate_font(&old) == VFS_ERR_INVAL);
    CHECK(vfs_font_read_at(&old, 0, buf, 1) == VFS_ERR_INVAL);
    CHECK(vfs_close_font(&old) == VFS_ERR_INVAL);
    CHECK(!memcmp(&before, &open_files[fresh.fd], sizeof(before)));
    CHECK(vfs_close_font(&fresh) == VFS_OK && !open_files[fresh.fd].in_use);
}

/* 失効した FD も (generation / owner が一致すれば) 解放できる。umount の
 * 失効 (fs_ctx / ops を外す) の後も backend に触らない */
static void stale_close(void)
{
    VfsFontLease a, b;
    u8 buf[4];
    use_ino(1);
    open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &a);
    open_font_ok(SHELL_OWNER, VFS_FONT_OPEN_PROTECT | VFS_FONT_OPEN_PIN, &b);
    vfs_fd_invalidate_ino(&mock_ctx, MOCK_INO);
    CHECK(vfs_close_font(&a) == VFS_OK && !open_files[a.fd].in_use);
    CHECK(!open_files[a.fd].stale && !open_files[a.fd].pinned);
    vfs_fd_invalidate_mount(&mock_ctx);
    CHECK(open_files[b.fd].ops == (VfsOps *)0);
    read_calls = 0;
    CHECK(vfs_font_read_at(&b, 0, buf, 1) == VFS_ERR_STALE && read_calls == 0);
    CHECK(vfs_close_font(&b) == VFS_OK && !open_files[b.fd].in_use);
    CHECK(!open_files[b.fd].protect && !open_files[b.fd].owner);
}

/* 公開の FD 経路は FONT を断る (stale より先に INVAL)、vfs_close は素通し */
static void public_reject(void)
{
    VfsFontLease l;
    VfsFile before;
    u8 buf[4];
    int stale;
    use_ino(1);
    for (stale = 0; stale < 2; stale++) {
        open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &l);
        if (stale) vfs_fd_invalidate_ino(&mock_ctx, MOCK_INO);
        before = open_files[l.fd];
        read_calls = 0;
        CHECK(vfs_read_fd(l.fd, buf, 1) == VFS_ERR_INVAL);
        CHECK(vfs_write_fd(l.fd, buf, 1) == VFS_ERR_INVAL);
        CHECK(vfs_seek(l.fd, 1, SEEK_SET) == VFS_ERR_INVAL);
        CHECK(vfs_tell(l.fd) == VFS_ERR_INVAL);
        CHECK(vfs_resize_fd(l.fd, 1) == VFS_ERR_INVAL);
        CHECK(vfs_sync_fd(l.fd) == VFS_ERR_INVAL);
        CHECK(vfs_fd_set_protect(l.fd, 1) == VFS_ERR_INVAL);
        CHECK(vfs_fd_set_pinned(l.fd, 0) == VFS_ERR_INVAL);
        vfs_close(l.fd);
        CHECK(read_calls == 0);
        CHECK(!memcmp(&before, &open_files[l.fd], sizeof(before)));
        CHECK(vfs_close_font(&l) == VFS_OK);
    }
}

/* b1⑤ の VFS 部: close_owned は protect でない FONT を閉じる。protect の
 * FONT (shell)・他 owner・SQLITE は残す。GENERIC は従来どおり閉じる */
static void close_owned(void)
{
    VfsFontLease app, app2, shell, other;
    VfsSqliteCookie cookie = { 1, 1 };
    VfsSqliteLease db;
    int g;
    open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &app);
    open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &app2);
    open_font_ok(APP_OWNER, VFS_FONT_OPEN_PROTECT | VFS_FONT_OPEN_PIN, &shell);
    open_font_ok(APP_OWNER + 1, VFS_FONT_OPEN_PIN, &other);
    CHECK(vfs_open_sqlite("/db", O_RDWR, APP_OWNER, &cookie, 0, &db) == VFS_OK);
    current_owner = APP_OWNER;
    g = vfs_open("/g", O_RDONLY);
    current_owner = 7;
    vfs_close_owned(APP_OWNER);
    CHECK(!open_files[app.fd].in_use && !open_files[app2.fd].in_use);
    CHECK(!open_files[app.fd].pinned && !open_files[app.fd].owner);
    CHECK(!open_files[g].in_use);
    CHECK(open_files[shell.fd].in_use && open_files[other.fd].in_use);
    CHECK(open_files[db.fd].in_use);
    CHECK(vfs_validate_font(&app) == VFS_ERR_INVAL);
    CHECK(vfs_validate_font(&shell) == VFS_OK && vfs_validate_font(&other) == VFS_OK);
}

/* b1⑦ の VFS 部: PIN 付きの font FD は rename / unlink の判定 (vfs.c が見る
 * vfs_fd_pinned_busy) で使用中、PIN 無しと close 後は使用中でない */
static void pinned_busy(void)
{
    VfsFontLease l;
    int ino;
    for (ino = 0; ino < 2; ino++) {
        use_ino(ino);
        open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &l);
        CHECK(vfs_fd_pinned_busy(&mock_ctx, ino, MOCK_INO, "/f.ttf") == 1);
        CHECK(vfs_close_font(&l) == VFS_OK);
        CHECK(vfs_fd_pinned_busy(&mock_ctx, ino, MOCK_INO, "/f.ttf") == 0);
        open_font_ok(SHELL_OWNER, VFS_FONT_OPEN_PROTECT, &l);
        CHECK(open_files[l.fd].protect == 1 && open_files[l.fd].pinned == 0);
        CHECK(vfs_fd_pinned_busy(&mock_ctx, ino, MOCK_INO, "/f.ttf") == 0);
        CHECK(vfs_close_font(&l) == VFS_OK);
        open_font_ok(SHELL_OWNER, VFS_FONT_OPEN_PROTECT | VFS_FONT_OPEN_PIN, &l);
        CHECK(vfs_fd_pinned_busy(&mock_ctx, ino, MOCK_INO, "/f.ttf") == 1);
        CHECK(vfs_close_font(&l) == VFS_OK);
    }
}

/* fd_read_backend への括り出しの後も GENERIC の読みは位置を進める */
static void generic_regression(void)
{
    u8 buf[8];
    int fd, ino;
    for (ino = 0; ino < 2; ino++) {
        use_ino(ino);
        fd = vfs_open("/g", O_RDONLY);
        CHECK(fd >= 3 && open_files[fd].lifetime == VFS_FD_GENERIC);
        CHECK(vfs_read_fd(fd, buf, 4) == 4 && !memcmp(buf, file_data, 4));
        CHECK(vfs_tell(fd) == 4 && last_off == 0);
        CHECK(vfs_read_fd(fd, buf, 4) == 4 && !memcmp(buf, file_data + 4, 4));
        CHECK(last_off == 4 && vfs_tell(fd) == 8);
        CHECK(vfs_seek(fd, (int)MOCK_FILE_SIZE - 2, SEEK_SET) == (int)MOCK_FILE_SIZE - 2);
        CHECK(vfs_read_fd(fd, buf, 8) == 2 && last_len == 2);
        CHECK(vfs_read_fd(fd, buf, 8) == 0);
        vfs_close(fd);
        CHECK(!open_files[fd].in_use);
    }
}

/* FD の予算 (T7b-a6 の値): 予約分の GENERIC を開いたまま shell と app の
 * 上限ぶんの font FD が全部開ける。値そのものも固定する */
static void budget(void)
{
    VfsFontLease l;
    int i;
    CHECK(VFS_FONT_FD_RESERVED == 9 && VFS_FONT_FD_MAX_SHELL == 2);
    CHECK(VFS_FONT_FD_MAX_APP == 2 && VFS_FONT_FD_MAX_APP_TOTAL == 2);
    CHECK(VFS_FONT_FD_RESERVED + VFS_FONT_FD_MAX_SHELL +
          VFS_FONT_FD_MAX_APP_TOTAL <= VFS_MAX_OPEN_FILES - 3);
    for (i = 0; i < VFS_FONT_FD_RESERVED; i++) CHECK(vfs_open("/g", O_RDONLY) >= 3);
    for (i = 0; i < VFS_FONT_FD_MAX_SHELL; i++)
        open_font_ok(SHELL_OWNER, VFS_FONT_OPEN_PROTECT | VFS_FONT_OPEN_PIN, &l);
    for (i = 0; i < VFS_FONT_FD_MAX_APP_TOTAL; i++)
        open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &l);
}

int main(int argc, char **argv)
{
    u32 i;
    for (i = 0; i < MOCK_FILE_SIZE; i++) file_data[i] = (u8)(i * 7u + 3u);
    mock_ops.get_file_size = mock_size;
    mock_ops.write_file = mock_write;
    mock_ops.read_stream = mock_read_stream;
    mock_ops.write_stream = mock_write_stream;
    mock_ino.lookup = mock_ino_lookup;
    mock_ino.read = mock_ino_read;
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "open_fields")) open_fields();
    else if (!strcmp(argv[1], "open_reject")) open_reject();
    else if (!strcmp(argv[1], "read_at")) read_at();
    else if (!strcmp(argv[1], "validate")) validate();
    else if (!strcmp(argv[1], "close_reuse")) close_reuse();
    else if (!strcmp(argv[1], "stale_close")) stale_close();
    else if (!strcmp(argv[1], "public_reject")) public_reject();
    else if (!strcmp(argv[1], "close_owned")) close_owned();
    else if (!strcmp(argv[1], "pinned_busy")) pinned_busy();
    else if (!strcmp(argv[1], "generic_regression")) generic_regression();
    else if (!strcmp(argv[1], "budget")) budget();
    else return 2;
    printf("PASS %s\n", argv[1]);
    return 0;
}
