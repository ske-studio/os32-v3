/* ========================================================================
 *  ext2_empty_name_host.c — 長さ 0 の名前 (と "." / "..") を新しい名前として
 *  媒体に載せないこと、を **実物の ext2 + VFS + pkg 展開**で確かめる
 *
 *  票:   docs/archive/kernel_v21/TASK_EXT2_EMPTY_NAME.md
 *  実行: python3 -B tools/tests/test_ext2_empty_name.py
 *  記録: tools/tests/ext2_empty_name_tdd.md
 *
 *  経緯: NP21/W 上の cdinst で作った NHD のルートに name_len = 0 の
 *  ディレクトリ項目 (inode 23) があり、Linux の e2fsck が「directory
 *  corrupted」と判定した。作ったのは pkg 展開の ensure_parent_dirs() が
 *  呼ぶ sys_mkdir("/hd0") — マウント点そのものの mkdir は VFS で相対パス "/"
 *  になり、ext2_split_path が親 "/" + 名前 "" に分け、ext2_mkdir は空の名前を
 *  断らずに作っていた。
 *
 *  段:
 *    vfs    … /hd0 に載せた ext2 へ、マウント点・末尾 "/"・"//"・"." を
 *             VFS 経由で渡す。mkdir は EXIST、書き込み系は負値で 1 セクタも
 *             書かない。末尾 "/" の mkdir は POSIX どおり作れる。
 *    root   … "/" に載せた ext2 で同じこと (mkdir "/")。
 *    ext2   … ext2 の入口を直に叩く (VFS の正規化を通らない呼び手の防御)。
 *    cdinst … cdinst と同じ mkdir の並び + 実物の userland/lib/rt/pkg.c で
 *             実物の tools/mkpkg.py が作った PKG を展開する。
 *  どの段も最後に**全ディレクトリの生の項目**を歩いて name_len = 0 / "/" を
 *  含む名前が無いことを見て、像を argv[1] のディレクトリへ書き出す。
 *  像は test_ext2_empty_name.py が本物の `e2fsck -fn` に当てる。
 *
 *  [C1] C89 / GNU89。u32 = unsigned long なので **ILP32 で組む**。
 *  libc は使わない (-nostdlib、Linux の int 0x80 だけ)。
 * ======================================================================== */

#include "ext2_priv.h"
#include "ide.h"
#include "kmalloc.h"
#include "hdd_pt_fake.h"   /* LBA 1 に OS32 区画 (票 TASK_HDD_INSTALL 段 1、1088 の廃止) */
#include "kstring.h"

/* ======================================================================== */
/*  libc の代わり                                                           */
/* ======================================================================== */

static int h_sys3(int nr, long a, long b, long c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return r;
}

static void die(int code)
{
    (void)h_sys3(1, code, 0, 0);
    for (;;) { }
}

static u32 h_strlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static void report(const char *t) { (void)h_sys3(4, 1, (long)t, (long)h_strlen(t)); }

static void report_i(int v)
{
    char buf[16];
    int i = 15;
    u32 u;
    buf[i] = '\0';
    u = (u32)(v < 0 ? -v : v);
    if (u == 0) buf[--i] = '0';
    while (u > 0) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (v < 0) buf[--i] = '-';
    report(&buf[i]);
}

static int g_checks, g_failures;

static void check_at(int cond, const char *what, int line)
{
    g_checks++;
    if (cond) return;
    g_failures++;
    report("  FAIL line "); report_i(line); report(": "); report(what); report("\n");
}
#define CHECK(x) check_at((x) ? 1 : 0, #x, __LINE__)

/* ---- kstring / kprintf / kmalloc の境界 ---- */
void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst; const u8 *s = (const u8 *)src; u32 i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
void *kmemset(void *dst, int val, u32 n)
{
    u8 *d = (u8 *)dst; u32 i;
    for (i = 0; i < n; i++) d[i] = (u8)val;
    return dst;
}
u32 kstrlen(const char *s) { return h_strlen(s); }
int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}
int kstrncmp(const char *a, const char *b, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i = 0;
    if (n == 0) return dst;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}
char *kstrncat(char *dst, const char *src, u32 n)
{
    u32 d = h_strlen(dst);
    if (d + 1 >= n) return dst;
    kstrncpy(dst + d, src, n - d);
    return dst;
}
void *memcpy(void *dst, const void *src, u32 n) { return kmemcpy(dst, src, n); }
void *memset(void *dst, int val, u32 n) { return kmemset(dst, val, n); }

void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

#define HEAP_SLOTS 4
static struct { int in_use; Ext2Ctx ctx; } g_slots[HEAP_SLOTS];
void *kzalloc(u32 size)
{
    int i;
    if (size > sizeof(Ext2Ctx)) return (void *)0;
    for (i = 0; i < HEAP_SLOTS; i++) {
        if (!g_slots[i].in_use) {
            g_slots[i].in_use = 1;
            kmemset(&g_slots[i].ctx, 0, sizeof(Ext2Ctx));
            return (void *)&g_slots[i].ctx;
        }
    }
    return (void *)0;
}
void kfree(void *p)
{
    int i;
    for (i = 0; i < HEAP_SLOTS; i++)
        if (p == (void *)&g_slots[i].ctx) { g_slots[i].in_use = 0; return; }
}

/* ======================================================================== */
/*  RAM ディスク (8MB の ext2、base_lba 1088)                               */
/* ======================================================================== */

#define DISK_FS_SECTORS 16384u
#define DISK_BASE_LBA   1088u
#define DISK_SECTORS    (DISK_BASE_LBA + PT_FAKE_ROUNDUP(DISK_FS_SECTORS))
#define SB_STATE_LBA    (DISK_BASE_LBA + 2u)

static u8 g_disk[DISK_SECTORS * 512u];
static u32 g_wr_sect, g_wr_sb;
static Device g_hd0;

/* s_state のセクタ以外への書き込み */
static u32 wr_non_state(void) { return g_wr_sect - g_wr_sb; }

Device *dev_find(const char *name)
{
    if (name && name[0] == 'h' && name[1] == 'd' && name[2] == '0' && !name[3])
        return &g_hd0;
    return (Device *)0;
}

int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf)
{
    int i;
    if (!dev) return -1;
    for (i = 0; i < count; i++) {
        if (lba + (u32)i >= DISK_SECTORS) return -1;
        kmemcpy((u8 *)buf + i * 512, g_disk + (lba + (u32)i) * 512u, 512);
    }
    return 0;
}

int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *buf)
{
    int i;
    if (!dev) return -1;
    for (i = 0; i < count; i++) {
        u32 cur = lba + (u32)i;
        if (cur >= DISK_SECTORS) return -1;
        kmemcpy(g_disk + cur * 512u, (const u8 *)buf + i * 512, 512);
        g_wr_sect++;
        if (cur == SB_STATE_LBA) g_wr_sb++;
    }
    return 0;
}

int ide_drive_present(int drive) { return (drive & 3) == 0; }
int ide_get_info(int drive, IdeInfo *info)
{
    if ((drive & 3) != 0) return IDE_ERR_NO_DRIVE;
    if (info) {
        kmemset(info, 0, sizeof(*info));
        info->cylinders = 1024;
        info->heads = 8;
        info->sectors = 17;
        info->phys_sector_size = 512;
        info->total_sectors = DISK_SECTORS;
    }
    return IDE_OK;
}

/* ---- fs/vfs_fd.c の境界 ---- */
int fd_is_redirected(int fd) { (void)fd; return 0; }
u16 fd_redirect_ifmt(int fd, int *out_file_fd)
{ (void)fd; if (out_file_fd) *out_file_fd = -1; return OS_S_IFCHR; }
int fd_redirect_read(int fd, void *buf, u32 size)
{ (void)fd; (void)buf; (void)size; return VFS_ERR_INVAL; }
int fd_redirect_write(int fd, const void *buf, u32 size)
{ (void)fd; (void)buf; (void)size; return VFS_ERR_INVAL; }
int kbd_getchar(void) { return '\n'; }
void console_write(const char *buf, u32 size, u8 color)
{ (void)buf; (void)size; (void)color; }
int res_owner_get(void) { return 0; }

/* ======================================================================== */
/*  実物のソース (-I の先頭に fs/ を置く。変異試験は写しの fs/ を先に置く)   */
/* ======================================================================== */

#include "ext2_super.c"
#include "ext2_inode.c"
#include "ext2_dir.c"
#include "ext2_file.c"
#include "ext2_fmt.c"
#include "ext2_vfs.c"
#include "vfs.c"
#include "vfs_fd.c"
/* cdinst / fdinst が使う展開ライブラリそのもの */
#include "rt/pkg.c"

/* ======================================================================== */
/*  足場                                                                    */
/* ======================================================================== */

static Ext2Ctx *g_ec;
static const char *g_dump_dir;
static const char *g_pkg_path[2];

static void disk_setup(const char *mount_at)
{
    int i;
    kmemset(g_disk, 0, sizeof(g_disk));
    CHECK(pt_fake_write(g_disk, DISK_BASE_LBA, DISK_SECTORS - DISK_BASE_LBA) == 0);
    kmemset(&g_hd0, 0, sizeof(g_hd0));
    g_hd0.name = "hd0";
    g_hd0.type = DEV_BLOCK;
    g_hd0.bus_type = DEV_BUS_IDE;
    g_hd0.sect_size = 512;
    g_hd0.total_sects = DISK_SECTORS;
    g_hd0.heads = 8;
    g_hd0.spt = 17;
    for (i = 0; i < HEAP_SLOTS; i++) g_slots[i].in_use = 0;

    CHECK(ext2_format(0, DISK_FS_SECTORS) == EXT2_OK);

    num_fs = 0;
    for (i = 0; i < VFS_MAX_FS; i++) kmemset(&mounts[i], 0, sizeof(mounts[i]));
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        open_files[i].in_use = 0;
        open_files[i].generation = 0;
    }
    kstrncpy(cwd, "/", VFS_MAX_PATH);
    ext2_init();
    CHECK(vfs_mount(mount_at, "hd0", "ext2") == VFS_OK);
    g_ec = (Ext2Ctx *)mounts[0].fs_ctx;
    CHECK(g_ec != (Ext2Ctx *)0);
}

/* 全ディレクトリの**生の**項目を歩く。ext2_list_dir は name_len = 0 を読み
 * 飛ばすので使わない (それで NP21/W 上では誰も気づかなかった)。
 * 戻り値 = 見つけた不正な項目の数 (空の名前 / "/" を含む名前 / ".", ".." が
 * 先頭 2 項目以外に居る)。 */
static int scan_dir(u32 dir_ino, int depth)
{
    static u8 blk[8][EXT2_BLOCK_SIZE];
    Ext2Inode inode;
    u32 bi, phys, pos;
    int bad = 0, idx = 0;

    if (depth >= 8) return 1;
    if (ext2_read_inode(g_ec, dir_ino, &inode) != 0) return 1;
    for (bi = 0; bi * EXT2_BLOCK_SIZE < inode.size; bi++) {
        if (ext2_bmap(g_ec, &inode, bi, &phys) != 0) return bad + 1;
        if (phys == 0) break;
        if (ext2_read_block(g_ec, phys, blk[depth]) != 0) return bad + 1;
        pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            u32 de_ino = (u32)blk[depth][pos] | ((u32)blk[depth][pos + 1] << 8)
                       | ((u32)blk[depth][pos + 2] << 16)
                       | ((u32)blk[depth][pos + 3] << 24);
            u32 rec = (u32)blk[depth][pos + 4] | ((u32)blk[depth][pos + 5] << 8);
            u32 nl = blk[depth][pos + 6];
            u8 ty = blk[depth][pos + 7];
            const u8 *nm = &blk[depth][pos + 8];
            if (rec == 0) break;
            if (de_ino != 0) {
                u32 k;
                int dot = (nl == 1 && nm[0] == '.');
                int dotdot = (nl == 2 && nm[0] == '.' && nm[1] == '.');
                if (nl == 0) {
                    report("  (scan) empty name: dir ino "); report_i((int)dir_ino);
                    report(" -> ino "); report_i((int)de_ino); report("\n");
                    bad++;
                }
                for (k = 0; k < nl; k++) if (nm[k] == '/') bad++;
                if ((dot && idx != 0) || (dotdot && idx != 1)) bad++;
                if (ty == EXT2_FT_DIR && nl > 0 && !dot && !dotdot)
                    bad += scan_dir(de_ino, depth + 1);
                idx++;
            }
            pos += rec;
        }
    }
    return bad;
}

static int media_names_ok(void) { return scan_dir(EXT2_ROOT_INO, 0) == 0; }

static int exists(const char *ext2_path)
{
    u32 ino;
    return ext2_lookup(g_ec, ext2_path, &ino) == EXT2_OK;
}

/* 像を <dump_dir>/<name> へ書き、`@@IMG <path>` を出す (Python が e2fsck に当てる) */
static void dump_image(const char *name)
{
    static char path[512];
    const u8 *src = g_disk + DISK_BASE_LBA * 512u;
    u32 left = DISK_FS_SECTORS * 512u, i, j;
    int fd, n;

    if (!g_dump_dir) return;
    CHECK(vfs_sync() == VFS_OK);
    for (i = 0; g_dump_dir[i] && i < 400; i++) path[i] = g_dump_dir[i];
    path[i++] = '/';
    for (j = 0; name[j] && i < 500; j++) path[i++] = name[j];
    path[i] = '\0';
    fd = h_sys3(5, (long)path, 01 | 0100 | 01000, 0644);
    if (fd < 0) { report("  (harness) image open failed\n"); g_failures++; return; }
    while (left > 0) {
        n = h_sys3(4, fd, (long)src, (long)left);
        if (n <= 0) { report("  (harness) image write failed\n"); g_failures++; break; }
        src += n; left -= (u32)n;
    }
    (void)h_sys3(6, fd, 0, 0);
    report("@@IMG "); report(path); report("\n");
}

/* 書き込み系の呼び出しが「負値で断り、s_state 以外に 1 セクタも書かない」 */
#define REFUSED(expr) do {                                  \
        int rc_;                                           \
        u32 w0_ = wr_non_state();                          \
        rc_ = (expr);                                      \
        CHECK(rc_ < 0);                                    \
        CHECK(wr_non_state() == w0_);                      \
        if (rc_ >= 0 || wr_non_state() != w0_) {           \
            report("    ^ rc="); report_i(rc_); report("\n"); \
        }                                                  \
    } while (0)

#define REFUSED_AS(expr, want) do {                         \
        int rc_;                                           \
        u32 w0_ = wr_non_state();                          \
        rc_ = (expr);                                      \
        CHECK(rc_ == (want));                              \
        CHECK(wr_non_state() == w0_);                      \
        if (rc_ != (want)) {                               \
            report("    ^ rc="); report_i(rc_); report("\n"); \
        }                                                  \
    } while (0)

/* ======================================================================== */
/*  段 vfs: /hd0 に載せた ext2                                              */
/* ======================================================================== */

static void case_vfs(void)
{
    int fd;
    report("case vfs\n");
    disk_setup("/hd0");

    /* 票の本体: マウント点そのものの mkdir (ensure_parent_dirs が呼ぶ形) */
    REFUSED_AS(vfs_mkdir("/hd0"), VFS_ERR_EXIST);
    REFUSED_AS(vfs_mkdir("/hd0/"), VFS_ERR_EXIST);
    REFUSED_AS(vfs_mkdir("/hd0//"), VFS_ERR_EXIST);
    /* 最終要素が "." / ".." の mkdir は正規化の前に INVAL (票
     * TASK_VFS_FD_PATH 方針 v2 の 10。以前は正規化してマウント点の EXIST) */
    REFUSED_AS(vfs_mkdir("/hd0/."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_mkdir("/hd0/tmp/.."), VFS_ERR_INVAL);
    CHECK(vfs_chdir("/hd0") == VFS_OK);
    REFUSED_AS(vfs_mkdir("."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_mkdir(""), VFS_ERR_EXIST);
    kstrncpy(cwd, "/", VFS_MAX_PATH);   /* "/" には何も載っていない */

    /* 末尾 "/" と "//" は VFS が正規化する — POSIX どおり作れる */
    CHECK(vfs_mkdir("/hd0/a/") == VFS_OK);
    CHECK(exists("/a"));
    CHECK(vfs_mkdir("/hd0//b") == VFS_OK);
    CHECK(exists("/b"));
    CHECK(vfs_mkdir("/hd0/a//c/") == VFS_OK);
    CHECK(exists("/a/c"));
    REFUSED_AS(vfs_mkdir("/hd0/a/"), VFS_ERR_EXIST);

    /* マウント点へのファイル作成・書き込み・消去・改名 */
    REFUSED(vfs_write("/hd0", "x", 1));
    REFUSED(vfs_write("/hd0/", "x", 1));
    fd = vfs_open("/hd0", O_WRONLY | O_CREAT | O_TRUNC);
    CHECK(fd < 0);
    if (fd >= 0) vfs_close(fd);
    fd = vfs_open("/hd0/", O_WRONLY | O_CREAT | O_EXCL);
    CHECK(fd < 0);
    if (fd >= 0) vfs_close(fd);
    REFUSED(vfs_rename("/hd0/b", "/hd0"));
    REFUSED(vfs_rename("/hd0", "/hd0/z"));
    CHECK(exists("/b"));
    REFUSED(vfs_rmdir("/hd0"));
    REFUSED(vfs_rm("/hd0"));

    /* 普通の操作は通る */
    CHECK(vfs_write("/hd0/a/f.txt", "hello", 5) >= 0);
    CHECK(exists("/a/f.txt"));

    CHECK(media_names_ok());
    dump_image("vfs.img");
}

/* ======================================================================== */
/*  段 root: "/" に載せた ext2 (起動後の通常の形)                           */
/* ======================================================================== */

static void case_root(void)
{
    report("case root\n");
    disk_setup("/");
    REFUSED_AS(vfs_mkdir("/"), VFS_ERR_EXIST);
    REFUSED_AS(vfs_mkdir("//"), VFS_ERR_EXIST);
    REFUSED_AS(vfs_mkdir("/."), VFS_ERR_INVAL);    /* 票 TASK_VFS_FD_PATH */
    REFUSED_AS(vfs_mkdir("/.."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_mkdir(""), VFS_ERR_EXIST);
    REFUSED(vfs_write("/", "x", 1));
    REFUSED(vfs_rmdir("/"));
    REFUSED(vfs_rm("/"));
    CHECK(vfs_mkdir("/tmp/") == VFS_OK);
    CHECK(exists("/tmp"));
    CHECK(media_names_ok());
    dump_image("root.img");
}

/* ======================================================================== */
/*  段 ext2: VFS の正規化を通らない呼び手の防御                             */
/* ======================================================================== */

static char g_long[EXT2_NAME_LEN + 2];

static void long_name(int len)
{
    int i;
    for (i = 0; i < len; i++) g_long[i] = (char)('a' + i % 26);
    g_long[len] = '\0';
}

static void case_ext2(void)
{
    u32 ino = 0;
    report("case ext2\n");
    disk_setup("/");

    CHECK(ext2_create(g_ec, EXT2_ROOT_INO, "f", "x", 1) == EXT2_OK);
    CHECK(ext2_mkdir(g_ec, EXT2_ROOT_INO, "d") == EXT2_OK);

    REFUSED_AS(ext2_mkdir(g_ec, EXT2_ROOT_INO, ""), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_mkdir(g_ec, EXT2_ROOT_INO, "."), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_mkdir(g_ec, EXT2_ROOT_INO, ".."), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_mkdir(g_ec, EXT2_ROOT_INO, "x/y"), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_create(g_ec, EXT2_ROOT_INO, "", "x", 1), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_create(g_ec, EXT2_ROOT_INO, "..", "x", 1), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_add_entry(g_ec, EXT2_ROOT_INO, "", 11, EXT2_FT_DIR), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_add_entry(g_ec, EXT2_ROOT_INO, ".", 11, EXT2_FT_DIR), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_rename(g_ec, EXT2_ROOT_INO, "f", EXT2_ROOT_INO, ""), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_rename(g_ec, EXT2_ROOT_INO, "f", EXT2_ROOT_INO, "."), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_rename(g_ec, EXT2_ROOT_INO, "d", EXT2_ROOT_INO, ".."), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_rename(g_ec, EXT2_ROOT_INO, ".", EXT2_ROOT_INO, "z"), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_rmdir(g_ec, EXT2_ROOT_INO, ""), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_rmdir(g_ec, EXT2_ROOT_INO, "."), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_rmdir(g_ec, EXT2_ROOT_INO, ".."), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_unlink(g_ec, EXT2_ROOT_INO, ""), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_unlink(g_ec, EXT2_ROOT_INO, "."), EXT2_ERR_INVAL);

    /* 名前の長さの上限。name_len は u8 なので 256 は 0 に回り込む */
    long_name(EXT2_NAME_LEN + 1);
    REFUSED_AS(ext2_mkdir(g_ec, EXT2_ROOT_INO, g_long), EXT2_ERR_INVAL);
    REFUSED_AS(ext2_create(g_ec, EXT2_ROOT_INO, g_long, "x", 1), EXT2_ERR_INVAL);
    long_name(EXT2_NAME_LEN);
    CHECK(ext2_mkdir(g_ec, EXT2_ROOT_INO, g_long) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, EXT2_ROOT_INO, g_long, &ino, (u8 *)0) == EXT2_OK);

    /* ext2 の VFS 口へ正規化されていないパスが来た場合 */
    REFUSED_AS(ext2_vfs_mkdir(g_ec, "/"), VFS_ERR_EXIST);
    REFUSED_AS(ext2_vfs_mkdir(g_ec, ""), VFS_ERR_EXIST);
    REFUSED(ext2_vfs_mkdir(g_ec, "/d/"));
    REFUSED(ext2_vfs_write(g_ec, "/", "x", 1));
    REFUSED(ext2_vfs_write(g_ec, "/d/", "x", 1));
    REFUSED(ext2_vfs_create_excl(g_ec, "/d/"));
    REFUSED(ext2_vfs_rename(g_ec, "/f", "/d/"));
    CHECK(exists("/f"));
    CHECK(exists("/d"));

    CHECK(media_names_ok());
    dump_image("ext2.img");
}


/* ======================================================================== */
/*  段 legacy: 以前の版が作った「名前の無い項目」が既に在る媒体             */
/* ======================================================================== */

/* 既存の NHD (cdinst で作ったもの) には名前の無い項目が残っている。
 * 直した後の OS32 がそれを**名前 "" で掴まない**こと — 掴むと "/" への
 * 書き込みがそのディレクトリ inode へファイルの中身を書く。 */
static void case_legacy(void)
{
    Ext2Inode root, d_before, d_after;
    u32 d_ino = 0, phys = 0, pos = 0, last = 0;
    static u8 blk[EXT2_BLOCK_SIZE];

    report("case legacy\n");
    disk_setup("/hd0");
    CHECK(vfs_mkdir("/hd0/d") == VFS_OK);
    CHECK(ext2_lookup(g_ec, "/d", &d_ino) == EXT2_OK);

    /* ルートの最後の項目を実寸に縮め、残りへ name_len = 0 の項目を置く
     * (実 NHD の block 260 offset 0x84 と同じ形: rec_len 残り全部、type 2) */
    CHECK(ext2_read_inode(g_ec, EXT2_ROOT_INO, &root) == EXT2_OK);
    CHECK(ext2_bmap(g_ec, &root, 0, &phys) == EXT2_OK && phys != 0);
    CHECK(ext2_read_block(g_ec, phys, blk) == EXT2_OK);
    while (pos < EXT2_BLOCK_SIZE) {
        u32 rec = (u32)blk[pos + 4] | ((u32)blk[pos + 5] << 8);
        if (rec == 0) break;
        last = pos;
        pos += rec;
    }
    {
        u32 actual = (8u + blk[last + 6] + 3u) & ~3u;
        u32 rec = (u32)blk[last + 4] | ((u32)blk[last + 5] << 8);
        u32 np = last + actual;
        CHECK(rec - actual >= 12);
        blk[last + 4] = (u8)actual; blk[last + 5] = (u8)(actual >> 8);
        blk[np] = (u8)d_ino; blk[np + 1] = (u8)(d_ino >> 8);
        blk[np + 2] = 0; blk[np + 3] = 0;
        blk[np + 4] = (u8)(rec - actual); blk[np + 5] = (u8)((rec - actual) >> 8);
        blk[np + 6] = 0;
        blk[np + 7] = EXT2_FT_DIR;
    }
    CHECK(ext2_write_block(g_ec, phys, blk) == EXT2_OK);
    ext2_ns_touch(g_ec);
    CHECK(!media_names_ok());           /* 仕込めている */
    report("  (上の empty name の 1 行は仕込んだもの)\n");

    CHECK(ext2_read_inode(g_ec, d_ino, &d_before) == EXT2_OK);
    CHECK(ext2_find_entry(g_ec, EXT2_ROOT_INO, "", (u32 *)0, (u8 *)0) == EXT2_ERR_NOTFOUND);
    REFUSED(ext2_vfs_write(g_ec, "/", "BAD", 3));
    REFUSED(vfs_write("/hd0", "BAD", 3));
    REFUSED(ext2_rmdir(g_ec, EXT2_ROOT_INO, ""));
    REFUSED(ext2_unlink(g_ec, EXT2_ROOT_INO, ""));
    REFUSED(ext2_rename(g_ec, EXT2_ROOT_INO, "", EXT2_ROOT_INO, "x"));
    REFUSED_AS(vfs_mkdir("/hd0"), VFS_ERR_EXIST);
    CHECK(ext2_read_inode(g_ec, d_ino, &d_after) == EXT2_OK);
    CHECK(d_after.size == d_before.size && d_after.mode == d_before.mode &&
          d_after.links_count == d_before.links_count);
    CHECK(exists("/d"));
    /* 像は書かない (わざと壊した媒体) */
}


/* ======================================================================== */
/*  段 synth: VFS の断りは FS を問わない (ext2 以外のドライバの前でも効く)   */
/* ======================================================================== */

static int s_calls;
static char s_last[VFS_MAX_PATH];
static int s_ctx_tag = 1;
static void *s_mount(int dev) { (void)dev; return &s_ctx_tag; }
static void s_umount(void *ctx) { (void)ctx; }
static int s_is_mounted(void *ctx) { (void)ctx; return 1; }
static int s_note(const char *path) { s_calls++; kstrncpy(s_last, path, VFS_MAX_PATH); return VFS_OK; }
static int s_mkdir(void *c, const char *p) { (void)c; return s_note(p); }
static int s_rmdir(void *c, const char *p) { (void)c; return s_note(p); }
static int s_unlink(void *c, const char *p) { (void)c; return s_note(p); }
static int s_write(void *c, const char *p, const void *d, u32 n)
{ (void)c; (void)d; (void)n; return s_note(p); }
static int s_rename(void *c, const char *a, const char *b)
{ (void)c; (void)b; return s_note(a); }
static VfsOps s_ops;

static void case_synth(void)
{
    int i;
    report("case synth\n");
    num_fs = 0;
    for (i = 0; i < VFS_MAX_FS; i++) kmemset(&mounts[i], 0, sizeof(mounts[i]));
    kstrncpy(cwd, "/", VFS_MAX_PATH);
    kmemset(&s_ops, 0, sizeof(s_ops));
    s_ops.name = "synth";
    s_ops.mount = s_mount;
    s_ops.umount = s_umount;
    s_ops.is_mounted = s_is_mounted;
    s_ops.mkdir = s_mkdir;
    s_ops.rmdir = s_rmdir;
    s_ops.unlink = s_unlink;
    s_ops.write_file = s_write;
    s_ops.rename = s_rename;
    vfs_register_fs(&s_ops);
    CHECK(vfs_mount("/syn", "hostdrv", "synth") == VFS_OK);

    s_calls = 0;
    CHECK(vfs_mkdir("/syn") == VFS_ERR_EXIST);
    CHECK(vfs_mkdir("/syn/") == VFS_ERR_EXIST);
    CHECK(vfs_mkdir("/syn/.") == VFS_ERR_INVAL);   /* 票 TASK_VFS_FD_PATH */
    CHECK(vfs_rmdir("/syn") < 0);
    CHECK(vfs_rm("/syn") < 0);
    CHECK(vfs_write("/syn", "x", 1) < 0);
    CHECK(vfs_rename("/syn", "/syn/a") < 0);
    CHECK(vfs_rename("/syn/a", "/syn") < 0);
    CHECK(s_calls == 0);            /* ドライバまで 1 回も届かない */

    CHECK(vfs_mkdir("/syn/a/") == VFS_OK);
    CHECK(s_calls == 1);
    CHECK(kstrcmp(s_last, "/a") == 0);   /* 末尾 "/" は VFS が落として渡す */
    CHECK(vfs_mkdir("/syn//b//c/") == VFS_OK);
    CHECK(kstrcmp(s_last, "/b/c") == 0);
}

/* ======================================================================== */
/*  段 cdinst: cdinst と同じ mkdir の並び + 実物の pkg 展開                  */
/* ======================================================================== */

/* ---- KernelAPI の贋物: /cd0/ はホストの PKG ファイル、ほかは実物の VFS ---- */
#define CD_FD_BASE 900
static int g_cd_hfd[4];
static int g_cd_open[4];

static int cd_index(const char *path)
{
    /* "/cd0/P0.PKG" / "/cd0/P1.PKG" */
    if (path[0] == '/' && path[1] == 'c' && path[2] == 'd' && path[3] == '0' &&
        path[4] == '/' && path[5] == 'P' && (path[6] == '0' || path[6] == '1'))
        return path[6] - '0';
    return -1;
}

static int __cdecl f_sys_open(const char *path, int mode)
{
    int i = cd_index(path);
    if (i >= 0) {
        int h;
        if (!g_pkg_path[i]) return VFS_ERR_NOTFOUND;
        h = h_sys3(5, (long)g_pkg_path[i], 0, 0);
        if (h < 0) return VFS_ERR_NOTFOUND;
        g_cd_hfd[i] = h; g_cd_open[i] = 1;
        return CD_FD_BASE + i;
    }
    return vfs_open(path, mode);
}
static void __cdecl f_sys_close(int fd)
{
    if (fd >= CD_FD_BASE && fd < CD_FD_BASE + 2) {
        (void)h_sys3(6, g_cd_hfd[fd - CD_FD_BASE], 0, 0);
        g_cd_open[fd - CD_FD_BASE] = 0;
        return;
    }
    vfs_close(fd);
}
static int __cdecl f_sys_read(int fd, void *buf, u32 size)
{
    if (fd >= CD_FD_BASE && fd < CD_FD_BASE + 2) {
        u32 got = 0;
        while (got < size) {
            int n = h_sys3(3, g_cd_hfd[fd - CD_FD_BASE], (long)((u8 *)buf + got),
                           (long)(size - got));
            if (n <= 0) break;
            got += (u32)n;
        }
        return (int)got;
    }
    return vfs_read_fd(fd, buf, size);
}
static int __cdecl f_sys_write(int fd, const void *buf, u32 size)
{
    return vfs_write_fd(fd, buf, size);
}
static int __cdecl f_sys_lseek(int fd, int off, int whence)
{
    if (fd >= CD_FD_BASE && fd < CD_FD_BASE + 2)
        return h_sys3(19, g_cd_hfd[fd - CD_FD_BASE], off, whence);
    return vfs_seek(fd, off, whence);
}
static int __cdecl f_sys_mkdir(const char *path) { return vfs_mkdir(path); }

static u8 g_arena[4u * 1024u * 1024u];
static u32 g_arena_used;
static void * __cdecl f_mem_alloc(u32 size)
{
    void *p;
    size = (size + 15u) & ~15u;
    if (g_arena_used + size > sizeof(g_arena)) return (void *)0;
    p = &g_arena[g_arena_used];
    g_arena_used += size;
    return p;
}
static void __cdecl f_mem_free(void *p) { (void)p; }
static void __cdecl f_kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

static KernelAPI g_api;
static PkgInfo g_info;

/* userland/system/cdinst.c install_package_hd() と同じパスの付け替え
 * ("/hd0" + PKG のパス)。cdinst.c 本体は IDE を直に叩くのでホストで組めない。 */
static int install_package_hd(const char *pkg)
{
    int i, ret;
    ret = pkg_parse(&g_api, pkg, &g_info);
    if (ret != PKG_OK) return ret;
    for (i = 0; i < g_info.entry_count; i++) {
        char orig[PKG_MAX_PATH];
        int j;
        for (j = 0; j < PKG_MAX_PATH - 1 && g_info.entries[i].path[j]; j++)
            orig[j] = g_info.entries[i].path[j];
        orig[j] = '\0';
        g_info.entries[i].path[0] = '/';
        g_info.entries[i].path[1] = 'h';
        g_info.entries[i].path[2] = 'd';
        g_info.entries[i].path[3] = '0';
        for (j = 0; orig[j] && j + 4 < PKG_MAX_PATH - 1; j++)
            g_info.entries[i].path[4 + j] = orig[j];
        g_info.entries[i].path[4 + j] = '\0';
    }
    return pkg_extract(&g_api, pkg, &g_info);
}

static void case_cdinst(void)
{
    static const char *const dirs[] = {
        "/hd0/sys", "/hd0/boot", "/hd0/bin", "/hd0/sbin", "/hd0/usr",
        "/hd0/usr/bin", "/hd0/usr/man", "/hd0/etc", "/hd0/data", "/hd0/home",
        "/hd0/home/user", "/hd0/tmp"
    };
    int i;
    u32 ino, sz;

    report("case cdinst\n");
    if (!g_pkg_path[0] || !g_pkg_path[1]) {
        report("  (harness) PKG paths missing\n");
        g_failures++;
        return;
    }
    disk_setup("/hd0");
    kmemset(&g_api, 0, sizeof(g_api));
    g_api.sys_open = f_sys_open;
    g_api.sys_close = f_sys_close;
    g_api.sys_read = f_sys_read;
    g_api.sys_write = f_sys_write;
    g_api.sys_lseek = f_sys_lseek;
    g_api.sys_mkdir = f_sys_mkdir;
    g_api.mem_alloc = f_mem_alloc;
    g_api.mem_free = f_mem_free;
    g_api.kprintf = f_kprintf;
    g_arena_used = 0;

    /* cdinst.c:505〜520 の並び */
    for (i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++)
        CHECK(vfs_mkdir(dirs[i]) == VFS_OK);

    CHECK(install_package_hd("/cd0/P0.PKG") == PKG_OK);   /* 無圧縮 (MINIMAL 相当) */
    CHECK(install_package_hd("/cd0/P1.PKG") == PKG_OK);   /* LZSS (NORMAL 相当) */

    /* 展開の結果が載っている */
    CHECK(ext2_lookup(g_ec, "/boot/vmkernel.lz4", &ino) == EXT2_OK);
    CHECK(ext2_get_size_ino(g_ec, ino, &sz) == EXT2_OK && sz == 3000);
    CHECK(exists("/sys/unicode.bin"));
    CHECK(exists("/bin/more.bin"));
    CHECK(exists("/db"));
    CHECK(exists("/db/keep.txt"));
    CHECK(exists("/usr/man/ls.1"));

    CHECK(media_names_ok());
    dump_image("cdinst.img");
}

/* ======================================================================== */

static void run(const char *sel)
{
    int all = (sel == (const char *)0);
    if (all || kstrcmp(sel, "vfs") == 0) case_vfs();
    if (all || kstrcmp(sel, "root") == 0) case_root();
    if (all || kstrcmp(sel, "ext2") == 0) case_ext2();
    if (all || kstrcmp(sel, "cdinst") == 0) case_cdinst();
    if (all || kstrcmp(sel, "legacy") == 0) case_legacy();
    if (all || kstrcmp(sel, "synth") == 0) case_synth();
    report("checks "); report_i(g_checks);
    report(" failures "); report_i(g_failures); report("\n");
}

/* argv: <dump_dir> <P0.PKG> <P1.PKG> [case] */
void ename_start_c(long *sp);
__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "  movl %esp, %eax\n"
        "  andl $-16, %esp\n"
        "  pushl %eax\n"
        "  call ename_start_c\n"
        "  hlt\n");

void ename_start_c(long *sp)
{
    long argc = sp[0];
    const char *sel = (const char *)0;
    if (argc >= 2) g_dump_dir = (const char *)sp[2];
    if (argc >= 3) g_pkg_path[0] = (const char *)sp[3];
    if (argc >= 4) g_pkg_path[1] = (const char *)sp[4];
    if (argc >= 5) sel = (const char *)sp[5];
    run(sel);
    die(g_failures ? 1 : 0);
}
