/* T7b-b1: font の read lease (fs/vfs_fd.c と kernel/font_source.c を実物で組む)。
 * 設計 ~/os32-tmp/run/t7bb12/DESIGN.final.md §2 / §5 の b1①〜⑦ と b2②④ の
 * lease 表側。S1 = VFS 部、S2 = lease 表・app の門 (public KAPI の実体)・
 * shell 側の段 C/D。S3b = exec.c の回収 hook (exec_reclaim_resources を
 * 試験 py が exec.c から切り出して exec_reclaim.inc に書き、lease_reclaim が
 * 実物として呼ぶ。回収の他の段は贋物)。
 * 贋物は FS backend・TTY・owner・caller の境界だけ (vfs_fd_sqlite_host.c と
 * 同じ流儀)。FONT_SOURCE_HOST_APP_TOTAL を与えた組み (試験 py の変種) は
 * app の合計上限だけをその値にして、owner ごとの上限を合計と分けて見る。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../fs/vfs_fd.c"
#ifdef FONT_SOURCE_HOST_APP_TOTAL
#undef VFS_FONT_FD_MAX_APP_TOTAL
#define VFS_FONT_FD_MAX_APP_TOTAL FONT_SOURCE_HOST_APP_TOTAL
#endif
/* kernel/gui.h は ILP32 の構造体の大きさを STATIC_ASSERT するので 64 ビットの
 * ホストでは読めない。font_source.c が使うのは GUI_SHELL_OWNER だけなので、
 * 試験 py が kernel/gui.h から読んだ値を -DHOST_GUI_SHELL_OWNER で渡す */
#define __GUI_H
#define GUI_SHELL_OWNER HOST_GUI_SHELL_OWNER
#include "../../kernel/font_source.c"

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

/* FD の予算 (T7b-a6 の値、F3 で束の +1・予約 9→7): 予約分の GENERIC を開いたまま shell と app の
 * 上限ぶんの font FD が全部開ける。値そのものも固定する */
static void budget(void)
{
    VfsFontLease l;
    int i;
    CHECK(VFS_FONT_FD_RESERVED == 7 && VFS_FONT_FD_MAX_SHELL == 3);
    CHECK(VFS_FONT_FD_MAX_APP == 3 && VFS_FONT_FD_MAX_APP_TOTAL == 3);
    CHECK(VFS_FONT_FD_RESERVED + VFS_FONT_FD_MAX_SHELL +
          VFS_FONT_FD_MAX_APP_TOTAL <= VFS_MAX_OPEN_FILES - 3);
    for (i = 0; i < VFS_FONT_FD_RESERVED; i++) CHECK(vfs_open("/g", O_RDONLY) >= 3);
    for (i = 0; i < VFS_FONT_FD_MAX_SHELL; i++)
        open_font_ok(SHELL_OWNER, VFS_FONT_OPEN_PROTECT | VFS_FONT_OPEN_PIN, &l);
    for (i = 0; i < VFS_FONT_FD_MAX_APP_TOTAL; i++)
        open_font_ok(APP_OWNER, VFS_FONT_OPEN_PIN, &l);
}

/* ==== S2: lease 表と app の門 (kernel/font_source.c) ==================== */

/* exec / redir_access の境界の贋物。caller は試験が演じる */
static int from_user = 1, caller_ok = 1, copy_fail, kill_calls;
static struct caller_access caller;
int ring3_call_from_user(void) { return from_user; }
int caller_access_get(struct caller_access *out)
{
    if (!caller_ok) return 0;
    *out = caller;
    return 1;
}
int copy_caller_cstr(const struct caller_access *c, const char *src,
                     char *dst, u32 cap)
{
    CHECK(c && c->origin == caller.origin && c->owner == caller.owner);
    if (copy_fail || !src || strlen(src) + 1 > cap) return 0;
    memcpy(dst, src, strlen(src) + 1);
    return 1;
}
void ring3_fault_kill(void) { kill_calls++; }

#define APP_GEN   0x51u
#define APP2      (APP_OWNER + 1)

/* app (owner, AS 世代) として dispatch 中にする */
static void as_app(int owner, u32 gen)
{
    from_user = 1;
    caller_ok = 1;
    caller.origin = CALLER_USER;
    caller.app_id = owner;
    /* AS の所有者の札 (paging の as->owner) は app_id と別の番号 (ゲストでは
     * app 2 に札 18 など)。同じ値にすると取り違えが隠れる (S3b) */
    caller.owner = (u32)owner + 0x40u;
    caller.generation = gen;
    current_owner = owner;
}

static u32 app_open_ok(void)
{
    u32 id = 0, size = 0;
    CHECK(font_lease_app_open("/f.ttf", &id, &size) == 0);
    CHECK(size == MOCK_FILE_SIZE && id != 0);
    return id;
}

static FontLease *slot_of(u32 id) { return &g_font_leases[id & FONT_LEASE_IDX_MASK]; }

/* open の結果: 枠・VFS FD の印 (owner は caller、PIN だけ)・id の符号化 */
static void lease_open(void)
{
    u32 id, rej;
    FontLease *s;
    u8 buf[8];
    int fd;
    use_ino(1);
    as_app(APP_OWNER, APP_GEN);
    rej = font_lease_reject_count;
    id = app_open_ok();
    s = slot_of(id);
    CHECK(s->kind == FONT_LEASE_APP && s->owner_gen == APP_GEN);
    CHECK(s->file_size == MOCK_FILE_SIZE && (id >> FONT_LEASE_IDX_BITS) == s->id_gen);
    CHECK(s->vfs.owner == APP_OWNER && open_files[s->vfs.fd].owner == APP_OWNER);
    CHECK(open_files[s->vfs.fd].lifetime == VFS_FD_FONT);
    CHECK(open_files[s->vfs.fd].pinned == 1 && open_files[s->vfs.fd].protect == 0);
    CHECK(vfs_fd_pinned_busy(&mock_ctx, 1, MOCK_INO, "/f.ttf") == 1);
    CHECK(font_lease_app_read_at(id, 10, buf, 4) == 4 && !memcmp(buf, file_data + 10, 4));
    CHECK(font_lease_reject_count == rej && kill_calls == 0);
    fd = s->vfs.fd;
    CHECK(font_lease_app_close(id) == 0);
    CHECK(!open_files[fd].in_use && !open_files[fd].pinned);
    CHECK(vfs_fd_pinned_busy(&mock_ctx, 1, MOCK_INO, "/f.ttf") == 0);
}

/* 門 1・2 (b1①): 外れたら INVAL・出力を書かない・reject を数える */
static void lease_gate(void)
{
    u32 id, oid = 0xa5a5a5a5u, osz = 0x5a5a5a5au, rej;
    u8 buf[4];
    int i;
    use_ino(1);
    as_app(APP_OWNER, APP_GEN);
    id = app_open_ok();
    for (i = 0; i < 4; i++) {
        as_app(APP_OWNER, APP_GEN);
        if (i == 0) from_user = 0;                    /* WM 中・CPL0 直呼び */
        if (i == 1) caller_ok = 0;                    /* 保存 caller なし */
        if (i == 2) caller.origin = CALLER_TRUSTED;
        if (i == 3) current_owner = APP2;             /* caller と資源 owner がずれる */
        rej = font_lease_reject_count;
        CHECK(font_lease_app_open("/f.ttf", &oid, &osz) == OS32_ERR_INVAL);
        CHECK(font_lease_app_read_at(id, 0, buf, 1) == OS32_ERR_INVAL);
        CHECK(font_lease_app_close(id) == OS32_ERR_INVAL);
        CHECK(font_lease_reject_count == rej + 3);
        CHECK(font_lease_reject_last ==
              (i == 0 ? FONT_LEASE_REJ_NOT_USER : FONT_LEASE_REJ_CALLER));
        CHECK(oid == 0xa5a5a5a5u && osz == 0x5a5a5a5au);
    }
    CHECK(kill_calls == 0);
    /* path を写せない USER ポインタは kill して INVAL、FD は開かない */
    as_app(APP_OWNER, APP_GEN);
    copy_fail = 1;
    CHECK(font_lease_app_open("/f.ttf", &oid, &osz) == OS32_ERR_INVAL);
    CHECK(kill_calls == 1 && font_lease_reject_last == FONT_LEASE_REJ_PATH);
    copy_fail = 0;
    CHECK(oid == 0xa5a5a5a5u);
    CHECK(slot_of(id)->kind == FONT_LEASE_APP && font_lease_app_close(id) == 0);
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++) CHECK(!open_files[i].in_use);
}

/* token の判定表 3b〜3e (b1①・b2②) */
static void lease_token(void)
{
    u32 id, sid = 0, ssz = 0;
    FontLease *s;
    u8 buf[4];
    int fd;
    use_ino(1);
    as_app(APP_OWNER, APP_GEN);
    id = app_open_ok();
    s = slot_of(id);
    /* 3b: id_gen 違い → STALE */
    CHECK(font_lease_app_read_at(id + (1u << FONT_LEASE_IDX_BITS), 0, buf, 1) == OS32_ERR_STALE);
    CHECK(font_lease_reject_last == FONT_LEASE_REJ_ID_GEN);
    /* 3c: 使っていない枠 (id_gen 0、kind FREE) → INVAL */
    CHECK(font_lease_app_read_at(FONT_LEASE_SLOTS - 1, 0, buf, 1) == OS32_ERR_INVAL);
    CHECK(font_lease_reject_last == FONT_LEASE_REJ_KIND);
    /* 3c: shell の id を KAPI に渡す → INVAL (b2②) */
    CHECK(font_lease_shell_open("/s.ttf", 3, &sid, &ssz) == 0);
    CHECK(font_lease_app_read_at(sid, 0, buf, 1) == OS32_ERR_INVAL);
    CHECK(font_lease_reject_last == FONT_LEASE_REJ_KIND);
    CHECK(font_lease_app_close(sid) == OS32_ERR_INVAL);
    CHECK(slot_of(sid)->kind == FONT_LEASE_SHELL && open_files[slot_of(sid)->vfs.fd].in_use);
    /* shell 側も app の id を断る */
    CHECK(font_lease_shell_read_at(id, 3, 0, buf, 1) == OS32_ERR_INVAL);
    CHECK(font_lease_shell_close(id, 3) == OS32_ERR_INVAL && s->kind == FONT_LEASE_APP);
    /* 3d: 他 owner の id → INVAL、FD は閉じない */
    as_app(APP2, APP_GEN);
    CHECK(font_lease_app_read_at(id, 0, buf, 1) == OS32_ERR_INVAL);
    CHECK(font_lease_reject_last == FONT_LEASE_REJ_OWNER);
    CHECK(font_lease_app_close(id) == OS32_ERR_INVAL && open_files[s->vfs.fd].in_use);
    /* 3e: 同じ owner 番号の次の app (AS 世代違い) → STALE */
    as_app(APP_OWNER, APP_GEN + 1);
    CHECK(font_lease_app_read_at(id, 0, buf, 1) == OS32_ERR_STALE);
    CHECK(font_lease_reject_last == FONT_LEASE_REJ_OWNER_GEN);
    CHECK(font_lease_app_close(id) == OS32_ERR_STALE && open_files[s->vfs.fd].in_use);
    /* 本人はまだ読める */
    as_app(APP_OWNER, APP_GEN);
    CHECK(font_lease_app_read_at(id, 0, buf, 1) == 1);
    /* 4: VFS の FD 世代ずれ (lease の知らない経路で FD が開き直された) → INVAL */
    s->vfs.generation++;
    CHECK(font_lease_app_read_at(id, 0, buf, 1) == OS32_ERR_INVAL);
    s->vfs.generation--;
    /* 4: 失効 → STALE、close は解放できる */
    vfs_fd_invalidate_ino(&mock_ctx, MOCK_INO);
    CHECK(font_lease_app_read_at(id, 0, buf, 1) == OS32_ERR_STALE);
    fd = s->vfs.fd;
    CHECK(font_lease_app_close(id) == 0 && !open_files[fd].in_use);
}

/* b1②: 短読はそのまま・範囲外は INVAL・位置を持たない */
static void lease_read(void)
{
    u32 id;
    u8 buf[MOCK_FILE_SIZE];
    use_ino(0);
    as_app(APP_OWNER, APP_GEN);
    id = app_open_ok();
    read_calls = 0;
    CHECK(font_lease_app_read_at(id, 3, buf, 5) == 5 && last_off == 3 && last_len == 5);
    CHECK(!memcmp(buf, file_data + 3, 5));
    CHECK(font_lease_app_read_at(id, 3, buf, 5) == 5 && last_off == 3);
    read_mode = READ_SHORT;
    CHECK(font_lease_app_read_at(id, 8, buf, 6) == 5);
    read_mode = READ_IO;
    CHECK(font_lease_app_read_at(id, 8, buf, 6) == OS32_ERR_IO);
    read_mode = READ_FULL;
    CHECK(read_calls == 4);
    CHECK(font_lease_app_read_at(id, MOCK_FILE_SIZE - 1, buf, 2) == OS32_ERR_INVAL);
    CHECK(font_lease_app_read_at(id, MOCK_FILE_SIZE + 1, buf, 0) == OS32_ERR_INVAL);
    CHECK(font_lease_app_read_at(id, MOCK_FILE_SIZE, buf, 0) == 0);
    CHECK(read_calls == 4);
    CHECK(font_lease_app_close(id) == 0);
}

/* b1④: close → 再 close は STALE、閉じた id で読めない、同じ枠を開き直しても
 * 旧 id は新しい lease を閉じない。満了した枠は二度と使わない */
static void lease_close(void)
{
    u32 id, id2, gen;
    FontLease *s;
    int fd;
    u8 buf[4];
    use_ino(1);
    as_app(APP_OWNER, APP_GEN);
    id = app_open_ok();
    s = slot_of(id);
    fd = s->vfs.fd;
    gen = s->id_gen;
    CHECK(font_lease_app_close(id) == 0);
    CHECK(s->kind == FONT_LEASE_FREE && s->id_gen == gen + 1 && !open_files[fd].in_use);
    CHECK(font_lease_app_close(id) == OS32_ERR_STALE);
    CHECK(font_lease_app_read_at(id, 0, buf, 1) == OS32_ERR_STALE);
    id2 = app_open_ok();
    CHECK(slot_of(id2) == s && id2 != id && s->vfs.fd == fd);
    CHECK(font_lease_app_close(id) == OS32_ERR_STALE && open_files[fd].in_use);
    CHECK(font_lease_app_read_at(id2, 0, buf, 1) == 1);
    CHECK(font_lease_app_close(id2) == 0);
    /* 満了: id_gen が上限に達した枠は retire */
    s->id_gen = FONT_LEASE_GEN_MAX - 1;
    id = app_open_ok();
    CHECK(slot_of(id) == s);
    CHECK(font_lease_app_close(id) == 0 && s->id_gen == FONT_LEASE_GEN_MAX);
    id2 = app_open_ok();
    CHECK(slot_of(id2) != s && s->kind == FONT_LEASE_FREE);
    CHECK(font_lease_app_close(id2) == 0);
    /* lease の知らない経路で FD が先に解放されていても、close は他人の FD を
     * 閉じずに枠を空ける (戻りは INVAL)、以後は STALE */
    id = app_open_ok();
    fd = slot_of(id)->vfs.fd;
    vfs_close_owned(APP_OWNER);
    CHECK(!open_files[fd].in_use);
    current_owner = 7;
    CHECK(vfs_open("/g", O_RDONLY) == fd);
    current_owner = APP_OWNER;
    CHECK(font_lease_app_close(id) == OS32_ERR_INVAL);
    CHECK(open_files[fd].in_use && open_files[fd].lifetime == VFS_FD_GENERIC);
    CHECK(slot_of(id)->kind == FONT_LEASE_FREE);
    CHECK(font_lease_app_close(id) == OS32_ERR_STALE);
}

/* b1③: owner ごとの上限と app 全体の上限は NOSPC、close で 1 本戻る。
 * FD 枯渇も NOSPC (FD 増設で逃げない) */
static void lease_limit(void)
{
    u32 ids[VFS_MAX_OPEN_FILES], oid = 0, osz = 0, extra;
    int i, n = 0;
    use_ino(1);
    as_app(APP_OWNER, APP_GEN);
    for (i = 0; i < VFS_FONT_FD_MAX_APP; i++) ids[n++] = app_open_ok();
    CHECK(font_lease_app_open("/f.ttf", &oid, &osz) == OS32_ERR_NOSPC && oid == 0);
    /* 別 owner は全体の残りぶんだけ開ける */
    as_app(APP2, APP_GEN);
    for (i = VFS_FONT_FD_MAX_APP; i < VFS_FONT_FD_MAX_APP_TOTAL; i++) ids[n++] = app_open_ok();
    CHECK(font_lease_app_open("/f.ttf", &oid, &osz) == OS32_ERR_NOSPC && oid == 0);
    /* shell の lease は app の上限に数えない */
    CHECK(font_lease_shell_open("/s.ttf", 1, &oid, &osz) == 0);
    CHECK(font_lease_shell_close(oid, 1) == 0);
    oid = 0;
    as_app(APP_OWNER, APP_GEN);
    CHECK(font_lease_app_close(ids[0]) == 0);
    extra = app_open_ok();
    CHECK(font_lease_app_open("/f.ttf", &oid, &osz) == OS32_ERR_NOSPC);
    CHECK(font_lease_app_close(extra) == 0);
    for (i = 1; i < n; i++) {
        as_app(i < VFS_FONT_FD_MAX_APP ? APP_OWNER : APP2, APP_GEN);
        CHECK(font_lease_app_close(ids[i]) == 0);
    }
    /* VFS の FD 枯渇 */
    current_owner = 7;
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++) CHECK(vfs_open("/g", O_RDONLY) == i);
    as_app(APP_OWNER, APP_GEN);
    oid = 0;
    CHECK(font_lease_app_open("/f.ttf", &oid, &osz) == OS32_ERR_NOSPC && oid == 0);
    for (i = 0; i < FONT_LEASE_SLOTS; i++) CHECK(g_font_leases[i].kind == FONT_LEASE_FREE);
}

/* b1⑤ の lease 表側: font_lease_close_owned + vfs_close_owned で app の FD と
 * 枠が全部戻る。他 owner・shell の lease は残る。shell 自身の回収
 * (vfs_close_owned(GUI_SHELL_OWNER)) でも shell の lease (protect) は残る */
static void lease_owned(void)
{
    u32 a, b, sid = 0, ssz = 0;
    int afd, i;
    use_ino(1);
    as_app(APP_OWNER, APP_GEN);
    a = app_open_ok();
    afd = slot_of(a)->vfs.fd;
    as_app(APP2, APP_GEN);
    b = app_open_ok();
    CHECK(font_lease_shell_open("/s.ttf", 2, &sid, &ssz) == 0);
    font_lease_close_owned(APP_OWNER);
    vfs_close_owned(APP_OWNER);
    CHECK(slot_of(a)->kind == FONT_LEASE_FREE && !open_files[afd].in_use);
    CHECK(slot_of(b)->kind == FONT_LEASE_APP && open_files[slot_of(b)->vfs.fd].in_use);
    vfs_close_owned(GUI_SHELL_OWNER);
    CHECK(slot_of(sid)->kind == FONT_LEASE_SHELL);
    CHECK(vfs_validate_font(&slot_of(sid)->vfs) == VFS_OK);
    as_app(APP_OWNER, APP_GEN);
    CHECK(font_lease_app_close(a) == OS32_ERR_STALE);
    font_lease_close_owned(APP2);
    vfs_close_owned(APP2);
    CHECK(slot_of(b)->kind == FONT_LEASE_FREE);
    font_lease_close_shell();
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++) CHECK(!open_files[i].in_use);
    for (i = 0; i < FONT_LEASE_SLOTS; i++) CHECK(g_font_leases[i].kind == FONT_LEASE_FREE);
}

/* S3b b1⑤: exec/exec.c の exec_reclaim_resources (切り出した実物) が app の
 * lease 表の枠と VFS の FD を両方返す。他の回収の段は贋物で、前後の段から
 * lease の状態を見る (font_lease_close_owned は fd_redirect の後・pipe の前)。 */
static int reclaim_target = -1, reclaim_mid_ok, reclaim_steps;
static u32 reclaim_watch;
void kapi_strings_cleanup_owned(int id) { (void)id; reclaim_steps++; }
void db_cleanup_owned(int id) { (void)id; reclaim_steps++; }
void fd_redirect_reset_owned(int id)
{
    reclaim_steps++;
    if (id == reclaim_target && slot_of(reclaim_watch)->kind == FONT_LEASE_APP) reclaim_mid_ok++;
}
void pipe_free_owned(int id)
{
    reclaim_steps++;
    if (id == reclaim_target && slot_of(reclaim_watch)->kind == FONT_LEASE_FREE) reclaim_mid_ok++;
}
void shm_free_owned(int id) { (void)id; reclaim_steps++; }
void snd_owner_exit(int id) { (void)id; reclaim_steps++; }
void pcm_reclaim(int id) { (void)id; reclaim_steps++; }
#include "exec_reclaim.inc"

static void lease_reclaim(void)
{
    u32 a, b, sid = 0, ssz = 0;
    int afd, bfd, i;
    use_ino(1);
    as_app(APP_OWNER, APP_GEN);
    a = app_open_ok();
    afd = slot_of(a)->vfs.fd;
    as_app(APP2, APP_GEN);
    b = app_open_ok();
    bfd = slot_of(b)->vfs.fd;
    CHECK(font_lease_shell_open("/s.ttf", 2, &sid, &ssz) == 0);
    reclaim_target = APP_OWNER;
    reclaim_watch = a;
    reclaim_mid_ok = reclaim_steps = 0;
    exec_reclaim_resources(APP_OWNER);
    CHECK(reclaim_steps == 7 && reclaim_mid_ok == 2);
    CHECK(slot_of(a)->kind == FONT_LEASE_FREE && !open_files[afd].in_use);
    CHECK(slot_of(b)->kind == FONT_LEASE_APP && open_files[bfd].in_use);
    CHECK(slot_of(sid)->kind == FONT_LEASE_SHELL);
    as_app(APP_OWNER, APP_GEN + 1);   /* 同じ owner 番号の次の app */
    CHECK(font_lease_app_close(a) == OS32_ERR_STALE);
    /* 次の app は上限まで開ける (枠が漏れていない) */
    a = app_open_ok();
    CHECK(font_lease_app_close(a) == 0);
    reclaim_target = APP2;
    reclaim_watch = b;
    exec_reclaim_resources(APP2);
    CHECK(slot_of(b)->kind == FONT_LEASE_FREE && !open_files[bfd].in_use);
    /* shell の回収 (owner = GUI_SHELL_OWNER) でも shell の lease は残る */
    reclaim_target = -1;
    exec_reclaim_resources(GUI_SHELL_OWNER);
    CHECK(slot_of(sid)->kind == FONT_LEASE_SHELL);
    font_lease_close_shell();
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++) CHECK(!open_files[i].in_use);
    for (i = 0; i < FONT_LEASE_SLOTS; i++) CHECK(g_font_leases[i].kind == FONT_LEASE_FREE);
}

/* 段 C/D (shell): owner 固定・PROTECT|PIN・登録世代の照合・上限・close_shell */
static void lease_shell(void)
{
    u32 ids[VFS_FONT_FD_MAX_SHELL], id = 0, sz = 0, aid;
    FontLease *s;
    u8 buf[4];
    int i, fd;
    use_ino(1);
    current_owner = APP_OWNER;   /* WM 中は res_owner が app のまま (b2④) */
    CHECK(font_lease_shell_open("/s.ttf", 0, &id, &sz) == OS32_ERR_INVAL);
    CHECK(font_lease_shell_open((const char *)0, 4, &id, &sz) == OS32_ERR_INVAL);
    for (i = 0; i < VFS_FONT_FD_MAX_SHELL; i++) {
        CHECK(font_lease_shell_open("/s.ttf", 4, &ids[i], &sz) == 0 && sz == MOCK_FILE_SIZE);
        s = slot_of(ids[i]);
        CHECK(s->kind == FONT_LEASE_SHELL && s->owner_gen == 4);
        CHECK(s->vfs.owner == GUI_SHELL_OWNER);
        CHECK(open_files[s->vfs.fd].owner == GUI_SHELL_OWNER);
        CHECK(open_files[s->vfs.fd].protect == 1 && open_files[s->vfs.fd].pinned == 1);
    }
    CHECK(font_lease_shell_open("/s.ttf", 4, &id, &sz) == OS32_ERR_NOSPC);
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++)
        CHECK(!open_files[i].in_use || open_files[i].owner == GUI_SHELL_OWNER);
    CHECK(font_lease_shell_read_at(ids[0], 4, 6, buf, 3) == 3 && !memcmp(buf, file_data + 6, 3));
    CHECK(font_lease_shell_read_at(ids[0], 4, MOCK_FILE_SIZE, buf, 1) == OS32_ERR_INVAL);
    CHECK(font_lease_shell_validate(ids[0], 4) == 0);
    /* 登録世代違い (reload 後の旧 token) → STALE */
    CHECK(font_lease_shell_read_at(ids[0], 5, 0, buf, 1) == OS32_ERR_STALE);
    CHECK(font_lease_shell_validate(ids[0], 5) == OS32_ERR_STALE);
    CHECK(font_lease_shell_close(ids[0], 5) == OS32_ERR_STALE);
    CHECK(font_lease_shell_read_at(ids[0] + (1u << FONT_LEASE_IDX_BITS), 4, 0, buf, 1) ==
          OS32_ERR_STALE);
    /* app 側は shell の lease を回収しない */
    as_app(APP_OWNER, APP_GEN);
    aid = app_open_ok();
    font_lease_close_owned(GUI_SHELL_OWNER);
    CHECK(slot_of(ids[0])->kind == FONT_LEASE_SHELL);
    /* close → 再 close は STALE */
    fd = slot_of(ids[1])->vfs.fd;
    CHECK(font_lease_shell_close(ids[1], 4) == 0 && !open_files[fd].in_use);
    CHECK(font_lease_shell_close(ids[1], 4) == OS32_ERR_STALE);
    /* 失効 → STALE、close_shell は失効も protect も閉じる。app は残る */
    vfs_fd_invalidate_ino(&mock_ctx, MOCK_INO);
    CHECK(font_lease_shell_validate(ids[0], 4) == OS32_ERR_STALE);
    fd = slot_of(ids[0])->vfs.fd;
    font_lease_close_shell();
    CHECK(slot_of(ids[0])->kind == FONT_LEASE_FREE && !open_files[fd].in_use);
    CHECK(font_lease_shell_read_at(ids[0], 4, 0, buf, 1) == OS32_ERR_STALE);
    CHECK(slot_of(aid)->kind == FONT_LEASE_APP);
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
    else if (!strcmp(argv[1], "lease_open")) lease_open();
    else if (!strcmp(argv[1], "lease_gate")) lease_gate();
    else if (!strcmp(argv[1], "lease_token")) lease_token();
    else if (!strcmp(argv[1], "lease_read")) lease_read();
    else if (!strcmp(argv[1], "lease_close")) lease_close();
    else if (!strcmp(argv[1], "lease_limit")) lease_limit();
    else if (!strcmp(argv[1], "lease_owned")) lease_owned();
    else if (!strcmp(argv[1], "lease_shell")) lease_shell();
    else if (!strcmp(argv[1], "lease_reclaim")) lease_reclaim();
    else return 2;
    printf("PASS %s\n", argv[1]);
    return 0;
}
