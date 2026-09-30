/* ========================================================================
 *  vfs_fd_path_host.c — FD は inode で動き、名前空間の変更で失効する /
 *  長いパス・深いパスは切り詰めずに断る、を **実物の vfs + vfs_fd + ext2 +
 *  rt/pkg.c** で確かめる
 *
 *  票:   docs/archive/kernel_v21/TASK_VFS_FD_PATH.md (方針 v2 の 8〜12、v3、ラリー 3)
 *  実行: python3 -B tools/tests/test_vfs_fd_path.py [--target] [--mutants] [case]
 *  記録: tools/tests/vfs_fd_path_tdd.md
 *
 *  段:
 *    fd    … 欠陥 1 の反例 (open → unlink → mkdir → write)、inode の再利用、
 *            set_mtime / O_TRUNC 後も失効しない、親の rename 後も同じ実体、
 *            置き換え rename (成否を問わず・公開後の失敗の注入)、rename(f,f)、
 *            ハードリンク、inode 取得の失敗注入、umount、read / fstat / seek、
 *            ext2 の stream / 切り詰めの ISDIR、O_EXCL のマウント点
 *    busy  … 開いている SQLite DB / ジャーナル / 祖先の rename は BUSY、
 *            使用中の loop イメージの unlink / 置き換えは BUSY
 *    dot   … 最終要素の "." / ".." (rmdir / rename 両引数 / unlink / mkdir)
 *    path  … 255/256 バイト、32/33 要素、相対パス + 長い cwd、mount prefix
 *    namerule … fs/vfs_name_rules.inc の表 (FAT / Win32 が意味を変える綴り)
 *    fatname  … 実物の fs/fatfs_vfs.c + fs/fatfs/ff.c (RAM の FAT12) で、
 *               "disk.img " / "d\img.dat" / "db." などを INVAL で断り、
 *               BUSY / pinned の実体が消えない・動かない (ラリー 2 blocker)
 *    fatroot  … (fatname の中) FAT を "/" にマウントして 253/254/255 バイト、
 *               255 バイトの名前で 253 バイトの pinned を消せない (ラリー 3 B1)
 *    utf8     … Win32 の名前の規則と lib/kutf16.c の変換が同じ集合を通し、
 *               通ったものは 1 対 1 (ラリー 3 B2、130^3 の総当たり)
 *    hostname … Win32 の規則 (末尾の空白・'.'・'\'・不正な UTF-8・禁止文字)
 *               を合成ドライバで
 *    pkg   … pkg_parse の 127/128 バイト・128/129 項目・切れた表・項目数の
 *            食い違い、pkg_first_overflow (cdinst の 123〜127)、pkg_extract の
 *            失敗の伝播
 *  fd / busy / path / pkg の像は argv[1] へ書き出し、Python が e2fsck -fn に当てる。
 *
 *  [C1] C89 / GNU89。u32 = unsigned long なので **ILP32 で組む**。
 *  libc は使わない (-nostdlib、Linux の int 0x80 だけ)。
 * ======================================================================== */

#include "ext2_priv.h"
#include "ide.h"
#include "kmalloc.h"
#include "hdd_pt_fake.h"   /* LBA 1 に OS32 区画 (票 TASK_HDD_INSTALL 段 1、1088 の廃止) */
#include "kstring.h"
#include <stdarg.h>

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
#include "rt/pkg.c"

/* ---- RED: 修正前の fs/ と rt/pkg.c に当てる (test_vfs_fd_path.py --red) ----
 * 修正前には無い口は、「修正前の振る舞い」を表す形に写す。失効の印も inode の
 * 口も無いので、それを直接見る検査と注入の検査は組まない (#ifndef FDP_RED)。 */
#ifdef FDP_RED
#ifndef VFS_ERR_STALE
#define VFS_ERR_STALE OS32_ERR_STALE
#endif
#ifndef VFS_ERR_NAMETOOLONG
#define VFS_ERR_NAMETOOLONG OS32_ERR_NAMETOOLONG
#endif
#ifndef VFS_ERR_BUSY
#define VFS_ERR_BUSY OS32_ERR_BUSY
#endif
#ifndef PKG_ERR_TOOLONG
#define PKG_ERR_TOOLONG -5
#endif
static int RESOLVE(const char *in, char *out, int n)
{
    vfs_resolve_path(in, out, n);     /* 修正前は断らない (void) */
    return VFS_OK;
}
static int red_stale(int fd) { (void)fd; return 0; }
#define vfs_fd_is_stale(fd) red_stale(fd)
static int red_mark(int fd, int on) { (void)fd; (void)on; return VFS_OK; }
#define vfs_fd_set_sqlite_db(fd) red_mark((fd), 1)
#define vfs_fd_set_pinned(fd, on) red_mark((fd), (on))
/* 修正前の cdinst は前置で溢れる項目を検査しなかった */
static int pkg_first_overflow(const PkgInfo *info, int prefix_len)
{ (void)info; (void)prefix_len; return -1; }
#else
#define RESOLVE vfs_resolve_path
#endif

/* ======================================================================== */
/*  足場                                                                    */
/* ======================================================================== */

static Ext2Ctx *g_ec;
static const char *g_dump_dir;
static const char *g_pkg_dir;

static void disk_setup(void)
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
        kmemset(&open_files[i], 0, sizeof(open_files[i]));
    }
    kstrncpy(cwd, "/", VFS_MAX_PATH);
    ext2_init();
    CHECK(vfs_mount("/hd0", "hd0", "ext2") == VFS_OK);
    g_ec = (Ext2Ctx *)mounts[0].fs_ctx;
    CHECK(g_ec != (Ext2Ctx *)0);
}

static int exists(const char *ext2_path)
{
    u32 ino;
    return ext2_lookup(g_ec, ext2_path, &ino) == EXT2_OK;
}

static u32 ino_of(const char *ext2_path)
{
    u32 ino = 0;
    if (ext2_lookup(g_ec, ext2_path, &ino) != EXT2_OK) return 0;
    return ino;
}

/* ext2 のパスの中身を読む (NUL 終端、戻り値は長さ / 負値) */
static int slurp(const char *ext2_path, char *buf, u32 cap)
{
    u32 ino;
    int n;
    if (ext2_lookup(g_ec, ext2_path, &ino) != EXT2_OK) return -1;
    n = ext2_read_file(g_ec, ino, buf, cap - 1);
    if (n < 0) return n;
    buf[n] = '\0';
    return n;
}

static int streq(const char *a, const char *b) { return kstrcmp(a, b) == 0; }

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

/* 「want で断り、s_state 以外に 1 セクタも書かない」 */
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

#define EQ(expr, want) do {                                 \
        int v_ = (expr);                                   \
        CHECK(v_ == (want));                               \
        if (v_ != (want)) {                                \
            report("    ^ got "); report_i(v_); report("\n"); \
        }                                                  \
    } while (0)

static int wfile(const char *vpath, const char *text)
{
    return vfs_write(vpath, text, h_strlen(text));
}

/* ======================================================================== */
/*  段 fd                                                                   */
/* ======================================================================== */

/* 失効した FD は read / write / fstat / seek の全部で STALE、close は通る */
static void expect_stale(int fd)
{
    char b[8];
    OS32_Stat st;
    EQ(vfs_write_fd(fd, "X", 1), VFS_ERR_STALE);
    EQ(vfs_read_fd(fd, b, sizeof(b)), VFS_ERR_STALE);
    EQ(vfs_fstat(fd, &st), VFS_ERR_STALE);
    EQ(vfs_seek(fd, 0, SEEK_SET), VFS_ERR_STALE);
    CHECK(vfs_fd_is_stale(fd) == 1);
}

#ifndef FDP_RED
/* 注入: rename を「公開してから失敗」「何もせず失敗」にする */
static int g_rename_mode;   /* 0 = 本物, 1 = 本物の後で IO, 2 = 何もせず IO */
static int inj_rename(void *ctx, const char *o, const char *n)
{
    int rc;
    if (g_rename_mode == 2) return VFS_ERR_IO;
    rc = ext2_vfs_rename(ctx, o, n);
    if (g_rename_mode == 1) return VFS_ERR_IO;
    return rc;
}

/* 注入: inode の取得を失敗させる (名前に "BAD" を含むもの) */
static int inj_lookup(void *ctx, const char *path, u32 *ino)
{
    u32 i;
    for (i = 0; path[i]; i++) {
        if (path[i] == 'B' && path[i + 1] == 'A' && path[i + 2] == 'D')
            return VFS_ERR_IO;
    }
    return ext2_vfs_lookup_ino(ctx, path, ino);
}
static const VfsInoOps inj_ino_ops = {
    inj_lookup, ext2_vfs_read_ino, ext2_vfs_write_ino,
    ext2_vfs_stat_ino, ext2_vfs_truncate_ino
};
#endif

static void case_fd(void)
{
    static u8 before[EXT2_BLOCK_SIZE], after[EXT2_BLOCK_SIZE];
    char buf[64];
    OS32_Stat st;
    Ext2Inode dino, dino2;
    u32 d_ino, phys, ino_f, ino_new;
    int fd, fd2, fd3, i;

    report("case fd\n");
    disk_setup();

    /* ---- 欠陥 1 (Codex): open → unlink → mkdir → 元の FD へ write ---- */
    fd = vfs_open("/hd0/f", O_CREAT | O_WRONLY);
    CHECK(fd >= 3);
    EQ(vfs_rm("/hd0/f"), VFS_OK);
    EQ(vfs_mkdir("/hd0/f"), VFS_OK);
    d_ino = ino_of("/f");
    CHECK(d_ino != 0);
    CHECK(ext2_read_inode(g_ec, d_ino, &dino) == 0);
    CHECK(ext2_bmap(g_ec, &dino, 0, &phys) == 0 && phys != 0);
    CHECK(ext2_read_block(g_ec, phys, before) == 0);
    REFUSED_AS(vfs_write_fd(fd, "EVIL-DATA-OVERWRITES-DIR", 24), VFS_ERR_STALE);
    expect_stale(fd);
    CHECK(ext2_read_block(g_ec, phys, after) == 0);
    for (i = 0; i < EXT2_BLOCK_SIZE; i++) if (before[i] != after[i]) break;
    CHECK(i == EXT2_BLOCK_SIZE);                  /* ディレクトリのブロック不変 */
    CHECK(ext2_read_inode(g_ec, d_ino, &dino2) == 0);
    CHECK(dino2.size == dino.size && dino2.mode == dino.mode);
    vfs_close(fd);
    CHECK(!open_files[fd].in_use);                /* 失効していても閉じられる */
    EQ(vfs_rmdir("/hd0/f"), VFS_OK);

    /* ---- 通常ファイルへの inode 再利用: 旧 FD は新しいファイルに書かない ---- */
    CHECK(wfile("/hd0/g", "old") >= 0);
    ino_f = ino_of("/g");
    fd = vfs_open("/hd0/g", O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_rm("/hd0/g"), VFS_OK);
    CHECK(wfile("/hd0/g", "NEW") >= 0);
    ino_new = ino_of("/g");
    CHECK(ino_new == ino_f);                      /* 最小番号から再利用される */
    expect_stale(fd);
    CHECK(slurp("/g", buf, sizeof(buf)) == 3 && streq(buf, "NEW"));
    vfs_close(fd);

    /* ---- set_mtime / O_TRUNC の後も同じ FD は失効しない ---- */
    fd = vfs_open("/hd0/g", O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_set_mtime("/hd0/g", 1700000000UL), VFS_OK);
    fd2 = vfs_open("/hd0/g", O_WRONLY | O_TRUNC);
    CHECK(fd2 >= 3);
    CHECK(vfs_fd_is_stale(fd) == 0);
    EQ(vfs_write_fd(fd2, "t2", 2), 2);
    EQ(vfs_write_fd(fd, "ab", 2), 2);             /* offset 0 から上書き */
    CHECK(slurp("/g", buf, sizeof(buf)) == 2 && streq(buf, "ab"));
    EQ(vfs_fstat(fd, &st), VFS_OK);
    CHECK(st.st_ino == ino_new && st.st_size == 2);
    CHECK(st.st_dev == vfs_path_dev("/hd0/g"));
    vfs_close(fd); vfs_close(fd2);

    /* ---- 親ディレクトリの rename 後も旧 FD は同じ実体を読み書きする ---- */
    EQ(vfs_mkdir("/hd0/d"), VFS_OK);
    CHECK(wfile("/hd0/d/f", "abc") >= 0);
    fd = vfs_open("/hd0/d/f", O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_rename("/hd0/d", "/hd0/e"), VFS_OK);
    EQ(vfs_mkdir("/hd0/d"), VFS_OK);
    CHECK(wfile("/hd0/d/f", "zzz") >= 0);         /* 同じ名前の新しいファイル */
    EQ(vfs_seek(fd, 3, SEEK_SET), 3);
    EQ(vfs_write_fd(fd, "def", 3), 3);
    CHECK(slurp("/e/f", buf, sizeof(buf)) == 6 && streq(buf, "abcdef"));
    CHECK(slurp("/d/f", buf, sizeof(buf)) == 3 && streq(buf, "zzz"));
    EQ(vfs_seek(fd, 0, SEEK_SET), 0);
    EQ(vfs_read_fd(fd, buf, 6), 6);
    buf[6] = '\0';
    CHECK(streq(buf, "abcdef"));
    EQ(vfs_fstat(fd, &st), VFS_OK);
    CHECK(st.st_ino == ino_of("/e/f"));
    vfs_close(fd);

    /* ---- 置き換え rename: 宛先の旧 FD は失効、rename(f,f) は失効させない ---- */
    CHECK(wfile("/hd0/r1", "one") >= 0);
    CHECK(wfile("/hd0/r2", "two") >= 0);
    fd = vfs_open("/hd0/r1", O_RDONLY);
    fd2 = vfs_open("/hd0/r2", O_RDONLY);
    CHECK(fd >= 3 && fd2 >= 3);
    EQ(vfs_rename("/hd0/r1", "/hd0/r1"), VFS_OK);
    CHECK(vfs_fd_is_stale(fd) == 0);
    EQ(vfs_rename("/hd0/r1", "/hd0/r2"), VFS_OK);
    CHECK(vfs_fd_is_stale(fd) == 0);              /* 元は動いただけ */
    expect_stale(fd2);                            /* 宛先の旧実体 */
    EQ(vfs_read_fd(fd, buf, 3), 3);
    CHECK(buf[0] == 'o' && buf[1] == 'n' && buf[2] == 'e');
    vfs_close(fd); vfs_close(fd2);

#ifndef FDP_RED
    /* 公開後の失敗 (注入) でも宛先の旧 FD は失効する */
    CHECK(wfile("/hd0/p1", "P1") >= 0);
    CHECK(wfile("/hd0/p2", "P2") >= 0);
    fd = vfs_open("/hd0/p2", O_RDONLY);
    CHECK(fd >= 3);
    ext2_ops.rename = inj_rename;
    g_rename_mode = 1;
    EQ(vfs_rename("/hd0/p1", "/hd0/p2"), VFS_ERR_IO);
    expect_stale(fd);
    vfs_close(fd);
    /* 何もせず失敗した場合も印は付ける (成否を問わない = 余分に断る側) */
    CHECK(wfile("/hd0/p3", "P3") >= 0);
    fd = vfs_open("/hd0/p3", O_RDONLY);
    g_rename_mode = 2;
    EQ(vfs_rename("/hd0/p2", "/hd0/p3"), VFS_ERR_IO);
    expect_stale(fd);
    vfs_close(fd);
    /* 宛先が無ければ誰も失効しない */
    fd = vfs_open("/hd0/p3", O_RDONLY);
    CHECK(fd >= 3);
    EQ(vfs_rename("/hd0/p2", "/hd0/p9"), VFS_ERR_IO);
    CHECK(vfs_fd_is_stale(fd) == 0);
    vfs_close(fd);
    ext2_ops.rename = ext2_vfs_rename;
    g_rename_mode = 0;
#endif

    /* ---- ハードリンク (ホストで作った媒体の形) ---- */
    CHECK(wfile("/hd0/h1", "hard") >= 0);
    {
        Ext2Inode hi;
        u32 hino = ino_of("/h1");
        CHECK(ext2_add_entry(g_ec, EXT2_ROOT_INO, "h2", hino, EXT2_FT_REG_FILE) == 0);
        CHECK(ext2_read_inode(g_ec, hino, &hi) == 0);
        hi.links_count++;
        CHECK(ext2_write_inode(g_ec, hino, &hi) == 0);
        ext2_ns_touch(g_ec);
        fd = vfs_open("/hd0/h1", O_RDONLY);
        CHECK(fd >= 3);
        /* 同じ inode の名前間の rename は宛先の実体を解放しない — 印を付けない */
        (void)vfs_rename("/hd0/h2", "/hd0/h1");
        CHECK(vfs_fd_is_stale(fd) == 0);
        vfs_close(fd);
        /* 1 つの名前の unlink で同じ inode の FD は失効する (余分に断る側、許容) */
        if (exists("/h2")) {
            fd = vfs_open("/hd0/h1", O_RDONLY);
            EQ(vfs_rm("/hd0/h2"), VFS_OK);
            expect_stale(fd);
            vfs_close(fd);
        }
    }

#ifndef FDP_RED
    /* ---- inode 取得の失敗注入: open / unlink / rename は断る ---- */
    ext2_ops.ino = &inj_ino_ops;
    EQ(vfs_open("/hd0/BADnew", O_CREAT | O_WRONLY), VFS_ERR_IO);
    CHECK(!exists("/BADnew"));                    /* 作ったものは消した */
    ext2_ops.ino = &ext2_ino_ops;
    CHECK(wfile("/hd0/BADold", "keep") >= 0);
    ext2_ops.ino = &inj_ino_ops;
    EQ(vfs_open("/hd0/BADold", O_RDONLY), VFS_ERR_IO);
    EQ(vfs_open("/hd0/BADx", O_CREAT | O_WRONLY | O_EXCL), VFS_ERR_IO);
    CHECK(!exists("/BADx"));
    REFUSED_AS(vfs_rm("/hd0/BADold"), VFS_ERR_IO);
    CHECK(exists("/BADold"));                     /* 取れないまま消さない */
    REFUSED_AS(vfs_rename("/hd0/g", "/hd0/BADold"), VFS_ERR_IO);
    CHECK(exists("/g") && exists("/BADold"));
    ext2_ops.ino = &ext2_ino_ops;
#endif

    /* ---- ext2 の stream / 切り詰めは通常ファイル以外を ISDIR で断る ---- */
    d_ino = ino_of("/d");
    REFUSED_AS(ext2_write_stream(g_ec, d_ino, "x", 1, 0), EXT2_ERR_ISDIR);
    EQ(ext2_read_stream(g_ec, d_ino, buf, 8, 0), EXT2_ERR_ISDIR);
#ifndef FDP_RED
    REFUSED_AS(ext2_vfs_truncate_ino(g_ec, d_ino), VFS_ERR_ISDIR);
    REFUSED_AS(ext2_vfs_write_ino(g_ec, d_ino, "x", 1, 0), VFS_ERR_ISDIR);
#endif

    /* ---- O_EXCL のマウント点は EXIST (NOSYS の判定の後) ---- */
    REFUSED_AS(vfs_open("/hd0", O_CREAT | O_WRONLY | O_EXCL), VFS_ERR_EXIST);
    REFUSED_AS(vfs_open("/hd0/", O_CREAT | O_WRONLY | O_EXCL), VFS_ERR_EXIST);

    /* ---- umount: そのマウントの FD 全部が失効、fs_ctx を触らない ---- */
    fd = vfs_open("/hd0/e/f", O_RDWR);
    fd3 = vfs_open("/hd0/d/f", O_RDONLY);
    CHECK(fd >= 3 && fd3 >= 3);
    dump_image("fd.img");
    vfs_umount("/hd0");
    CHECK(open_files[fd].fs_ctx == (void *)0);
    expect_stale(fd);
    expect_stale(fd3);
    vfs_close(fd); vfs_close(fd3);
}

/* ======================================================================== */
/*  段 busy: SQLite の rename と loop イメージ                               */
/* ======================================================================== */

static void case_busy(void)
{
    VfsSqliteCookie cookie;
    VfsSqliteLease lease, jlease;
    char buf[16];
    int fd, fd2;

    report("case busy\n");
    disk_setup();
    cookie.group_index = 1;
    cookie.generation = 1;

    EQ(vfs_mkdir("/hd0/db"), VFS_OK);
    EQ(vfs_mkdir("/hd0/db/sub"), VFS_OK);
    CHECK(wfile("/hd0/db/sub/x.db", "DBDATA") >= 0);
    CHECK(wfile("/hd0/db/sub/x.db-journal", "J") >= 0);
    CHECK(wfile("/hd0/db/sub/y", "Y") >= 0);
    CHECK(wfile("/hd0/other", "O") >= 0);
    EQ(vfs_open_sqlite("/hd0/db/sub/x.db", O_RDWR, 1, &cookie, 0, &lease), VFS_OK);

    /* DB 本体・ジャーナル・祖先ディレクトリ (元でも宛先でも) は BUSY */
    REFUSED_AS(vfs_rename("/hd0/db/sub/x.db", "/hd0/moved.db"), VFS_ERR_BUSY);
    REFUSED_AS(vfs_rename("/hd0/db/sub/x.db-journal", "/hd0/j"), VFS_ERR_BUSY);
    REFUSED_AS(vfs_rename("/hd0/db/sub", "/hd0/sub2"), VFS_ERR_BUSY);
    REFUSED_AS(vfs_rename("/hd0/db", "/hd0/db2"), VFS_ERR_BUSY);
    REFUSED_AS(vfs_rename("/hd0/other", "/hd0/db/sub/x.db"), VFS_ERR_BUSY);
    REFUSED_AS(vfs_rename("/hd0/other", "/hd0/db/sub/x.db-journal"), VFS_ERR_BUSY);
    CHECK(vfs_fd_is_stale(lease.fd) == 0);        /* 断っただけ、印は付けない */
    CHECK(exists("/db/sub/x.db") && exists("/other"));
    /* 似た名前・兄弟は通る */
    EQ(vfs_rename("/hd0/db/sub/y", "/hd0/db/sub/x.dbz"), VFS_OK);
    EQ(vfs_rename("/hd0/db/sub/x.dbz", "/hd0/db/sub/x.db-journalz"), VFS_OK);
    /* 閉じれば通る */
    EQ(vfs_close_sqlite(&lease), VFS_OK);
    EQ(vfs_rename("/hd0/db/sub/x.db", "/hd0/db/sub/x2.db"), VFS_OK);

    /* 旧来の vfs_open 経路でも印を付ければ同じ (IME 辞書の常駐接続) */
    fd = vfs_open("/hd0/db/sub/x2.db", O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_fd_set_sqlite_db(fd), VFS_OK);
    REFUSED_AS(vfs_rename("/hd0/db", "/hd0/db3"), VFS_ERR_BUSY);
    vfs_close(fd);
    EQ(vfs_rename("/hd0/db", "/hd0/db3"), VFS_OK);

    /* 開いた SQLite の FD を unlink すると失効し、close_sqlite は通る */
    cookie.generation = 2;
    EQ(vfs_open_sqlite("/hd0/db3/sub/x2.db", O_RDWR, 1, &cookie, 0, &jlease), VFS_OK);
    EQ(vfs_rm("/hd0/db3/sub/x2.db"), VFS_OK);
    expect_stale(jlease.fd);
    EQ(vfs_close_sqlite(&jlease), VFS_OK);

    /* ---- loop イメージ: 使用中の unlink・置き換えは BUSY、移動は通る ---- */
    CHECK(wfile("/hd0/img.hdi", "IMAGEDATA") >= 0);
    fd = vfs_open("/hd0/img.hdi", O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_fd_set_pinned(fd, 1), VFS_OK);
    REFUSED_AS(vfs_rm("/hd0/img.hdi"), VFS_ERR_BUSY);
    REFUSED_AS(vfs_rename("/hd0/other", "/hd0/img.hdi"), VFS_ERR_BUSY);
    CHECK(vfs_fd_is_stale(fd) == 0);
    EQ(vfs_rename("/hd0/img.hdi", "/hd0/img2.hdi"), VFS_OK);
    fd2 = vfs_open("/hd0/img2.hdi", O_RDONLY);
    REFUSED_AS(vfs_rm("/hd0/img2.hdi"), VFS_ERR_BUSY);   /* 名前が変わっても */
    EQ(vfs_read_fd(fd, buf, 9), 9);
    CHECK(buf[0] == 'I' && buf[8] == 'A');
    EQ(vfs_fd_set_pinned(fd, 0), VFS_OK);
    EQ(vfs_rm("/hd0/img2.hdi"), VFS_OK);
    expect_stale(fd);
    expect_stale(fd2);
    vfs_close(fd); vfs_close(fd2);

#ifndef FDP_RED
    /* ---- 失効しても接続が閉じるまでは BUSY (Codex 実装レビュー ラリー 1 の
     * B3)。unlink で FD が失効しても SQLite 接続は開いたままで、ジャーナルを
     * 開いた時の名前で作り・消す ---- */
    EQ(vfs_mkdir("/hd0/s3"), VFS_OK);
    CHECK(wfile("/hd0/s3/x.db", "DBDATA") >= 0);
    cookie.generation = 3;
    EQ(vfs_open_sqlite("/hd0/s3/x.db", O_RDWR, 1, &cookie, 0, &lease), VFS_OK);
    EQ(vfs_rm("/hd0/s3/x.db"), VFS_OK);
    CHECK(vfs_fd_is_stale(lease.fd) == 1);
    REFUSED_AS(vfs_rename("/hd0/s3", "/hd0/s3b"), VFS_ERR_BUSY);
    REFUSED_AS(vfs_rename("/hd0/other", "/hd0/s3/x.db-journal"), VFS_ERR_BUSY);
    REFUSED_AS(vfs_rename("/hd0/other", "/hd0/s3/x.db"), VFS_ERR_BUSY);
    EQ(vfs_close_sqlite(&lease), VFS_OK);
    EQ(vfs_rename("/hd0/s3", "/hd0/s3b"), VFS_OK);
    /* 旧来の経路 (IME 辞書) も同じ */
    CHECK(wfile("/hd0/s3b/y.db", "DBDATA") >= 0);
    fd = vfs_open("/hd0/s3b/y.db", O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_fd_set_sqlite_db(fd), VFS_OK);
    EQ(vfs_rm("/hd0/s3b/y.db"), VFS_OK);
    REFUSED_AS(vfs_rename("/hd0/s3b", "/hd0/s3c"), VFS_ERR_BUSY);
    vfs_close(fd);
    EQ(vfs_rename("/hd0/s3b", "/hd0/s3c"), VFS_OK);

    /* ext2 は名前の大文字小文字を区別する: 別の名前 (Q) は BUSY にしない */
    EQ(vfs_mkdir("/hd0/q"), VFS_OK);
    EQ(vfs_mkdir("/hd0/Q"), VFS_OK);
    CHECK(wfile("/hd0/q/z.db", "DBDATA") >= 0);
    cookie.generation = 4;
    EQ(vfs_open_sqlite("/hd0/q/z.db", O_RDWR, 1, &cookie, 0, &lease), VFS_OK);
    REFUSED_AS(vfs_rename("/hd0/q", "/hd0/q2"), VFS_ERR_BUSY);
    EQ(vfs_rename("/hd0/Q", "/hd0/Q2"), VFS_OK);
    EQ(vfs_close_sqlite(&lease), VFS_OK);
#endif

    dump_image("busy.img");
}

#ifndef FDP_RED
/* ======================================================================== */
/*  段 nocase: 大文字小文字を区別しない FS の BUSY / pinned (Codex 実装      */
/*  レビュー ラリー 1 の B4)。FAT (FatFs) は名前を大文字にして探すので、    */
/*  `DB` と `db` は同じ実体。VFS の名前比較も FS の規則 (name_fold) に従う。   */
/*  ここでは ext2 の口を「パスで動き、名前を ASCII で畳む FS」として別の     */
/*  種別で 2 つ目にマウントする (拒否は FS の操作の前に決まるので、下の FS    */
/*  が実際に区別するかは問わない)。像は書き出さない (同じ装置の 2 つ目の     */
/*  コンテキストで書くと ext2 が整合しない)。                                */
/* ======================================================================== */

static u8 ci_fold(u8 c)
{
    if (c >= 'a' && c <= 'z') return (u8)(c - 'a' + 'A');
    return c;
}
static VfsOps g_ci_ops;

static void case_nocase(void)
{
    VfsSqliteCookie cookie;
    VfsSqliteLease lease;
    int fd;

    report("case nocase\n");
    disk_setup();
    EQ(vfs_mkdir("/hd0/cdb"), VFS_OK);
    CHECK(wfile("/hd0/cdb/x.db", "DBDATA") >= 0);
    CHECK(wfile("/hd0/disk.img", "IMAGEDATA") >= 0);
    CHECK(wfile("/hd0/other", "O") >= 0);
    EQ(vfs_sync(), VFS_OK);

    g_ci_ops = ext2_ops;
    g_ci_ops.name = "ext2ci";
    g_ci_ops.ino = (const VfsInoOps *)0;      /* FAT と同じくパスで動く */
    g_ci_ops.name_fold = ci_fold;
    vfs_register_fs(&g_ci_ops);
    EQ(vfs_mount("/ci", "hd0", "ext2ci"), VFS_OK);

    cookie.group_index = 1;
    cookie.generation = 1;
    EQ(vfs_open_sqlite("/ci/cdb/x.db", O_RDWR, 1, &cookie, 0, &lease), VFS_OK);
    CHECK(open_files[lease.fd].has_ino == 0);
    /* 大文字小文字だけ違う名前でも同じ実体 → BUSY (旧実装は通した) */
    EQ(vfs_rename("/ci/CDB", "/ci/MOVED"), VFS_ERR_BUSY);
    EQ(vfs_rename("/ci/cdb/X.DB", "/ci/y.db"), VFS_ERR_BUSY);
    EQ(vfs_rename("/ci/other", "/ci/CDB/X.DB-JOURNAL"), VFS_ERR_BUSY);
    EQ(vfs_rename("/ci/other", "/ci/cdb/x.db-Journal"), VFS_ERR_BUSY);
    /* 似た名前は通す判定のまま (BUSY にはしない。下の FS の答えを返す) */
    CHECK(vfs_rename("/ci/CDBX", "/ci/MOVED") != VFS_ERR_BUSY);
    EQ(vfs_close_sqlite(&lease), VFS_OK);

    /* 使用中の loop イメージ: 大文字にした名前でも消させない */
    fd = vfs_open("/ci/disk.img", O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_fd_set_pinned(fd, 1), VFS_OK);
    EQ(vfs_rm("/ci/DISK.IMG"), VFS_ERR_BUSY);
    EQ(vfs_rm("/ci/Disk.Img"), VFS_ERR_BUSY);
    EQ(vfs_rename("/ci/other", "/ci/DISK.IMG"), VFS_ERR_BUSY);
    EQ(vfs_fd_set_pinned(fd, 0), VFS_OK);
    vfs_close(fd);
    CHECK(exists("/disk.img") && exists("/cdb/x.db"));
}
#endif

/* ======================================================================== */
/*  段 dot: 最終要素の "." / ".."                                            */
/* ======================================================================== */

static void case_dot(void)
{
    report("case dot\n");
    disk_setup();
    EQ(vfs_mkdir("/hd0/a"), VFS_OK);
    EQ(vfs_mkdir("/hd0/a/b"), VFS_OK);
    CHECK(wfile("/hd0/a/f", "F") >= 0);

    REFUSED_AS(vfs_rmdir("/hd0/a/b/."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_rmdir("/hd0/a/b/./"), VFS_ERR_INVAL);
    REFUSED_AS(vfs_rmdir("/hd0/a/b/.."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_rmdir("/hd0/a/b/..//"), VFS_ERR_INVAL);
    CHECK(exists("/a/b"));
    REFUSED_AS(vfs_rename("/hd0/a/b/.", "/hd0/z"), VFS_ERR_INVAL);
    REFUSED_AS(vfs_rename("/hd0/a/f", "/hd0/a/b/.."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_rename("/hd0/a/f", "/hd0/a/b/."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_rm("/hd0/a/f/."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_rm("/hd0/a/b/.."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_mkdir("/hd0/a/."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_mkdir("/hd0/a/new/.."), VFS_ERR_INVAL);
    CHECK(!exists("/a/new"));
    CHECK(vfs_chdir("/hd0/a") == VFS_OK);
    REFUSED_AS(vfs_rmdir("."), VFS_ERR_INVAL);
    REFUSED_AS(vfs_rmdir(".."), VFS_ERR_INVAL);
    kstrncpy(cwd, "/", VFS_MAX_PATH);
    /* "." / ".." で終わらない名前は従来どおり */
    CHECK(wfile("/hd0/a/.x", "dotfile") >= 0);
    EQ(vfs_rename("/hd0/a/.x", "/hd0/a/..y"), VFS_OK);
    EQ(vfs_rm("/hd0/a/..y"), VFS_OK);
    EQ(vfs_rmdir("/hd0/a/b/"), VFS_OK);
    CHECK(exists("/a/f"));
    dump_image("dot.img");
}

/* ======================================================================== */
/*  段 path: 長いパス・深いパス                                             */
/* ======================================================================== */

static char g_p1[VFS_MAX_PATH * 2 + 8];
static char g_p2[VFS_MAX_PATH * 2 + 8];
static char g_out[VFS_MAX_PATH];

/* "/hd0/" + c×n (n 文字の 1 要素) */
static const char *mk_long(char *dst, int n, char c, char last)
{
    int i, o = 0;
    const char *pre = "/hd0/";
    for (i = 0; pre[i]; i++) dst[o++] = pre[i];
    for (i = 0; i < n; i++) dst[o++] = c;
    if (last) dst[o - 1] = last;
    dst[o] = '\0';
    return dst;
}

static void case_path(void)
{
    int i, o, fd;
    u32 w0;

    report("case path\n");
    disk_setup();

    /* ---- 255 / 256 バイト (NUL 抜き) ---- */
    mk_long(g_p1, 250, 'a', 0);                   /* 255 */
    CHECK(h_strlen(g_p1) == 255);
    EQ(RESOLVE(g_p1, g_out, VFS_MAX_PATH), VFS_OK);
    CHECK(streq(g_out, g_p1));
    CHECK(wfile(g_p1, "fits") >= 0);
    mk_long(g_p1, 251, 'a', 'X');                 /* 256: 旧実装は …a に化けた */
    mk_long(g_p2, 251, 'a', 'Y');
    EQ(RESOLVE(g_p1, g_out, VFS_MAX_PATH), VFS_ERR_NAMETOOLONG);
    CHECK(g_out[0] == '\0');
    w0 = wr_non_state();
    EQ(wfile(g_p1, "X"), VFS_ERR_NAMETOOLONG);
    EQ(vfs_open(g_p2, O_CREAT | O_WRONLY | O_TRUNC), VFS_ERR_NAMETOOLONG);
    EQ(vfs_rm(g_p1), VFS_ERR_NAMETOOLONG);
    EQ(vfs_mkdir(g_p2), VFS_ERR_NAMETOOLONG);
    EQ(vfs_rename(g_p1, "/hd0/q"), VFS_ERR_NAMETOOLONG);
    EQ(vfs_rename("/hd0/q", g_p2), VFS_ERR_NAMETOOLONG);
    CHECK(wr_non_state() == w0);
    mk_long(g_p1, 250, 'a', 0);
    CHECK(slurp(g_p1 + 4, g_out, sizeof(g_out)) == 4);   /* 255 の方は無傷 */
    /* 出力の器が小さいときも切り詰めない */
    EQ(RESOLVE("/hd0/abcdef", g_out, 8), VFS_ERR_NAMETOOLONG);
    EQ(RESOLVE("/hd0/abc", g_out, 9), VFS_OK);
    EQ(RESOLVE("/x", (char *)0, 8), VFS_ERR_INVAL);
    /* 入力そのものが 256 バイト以上なら、正規化で短くなる形でも断る
     * (上限の先を読まない。旧実装は 255 で切ってから畳んだ) */
    o = 0;
    g_p1[o++] = '/';
    for (i = 0; i < 52; i++) {
        g_p1[o++] = 'a'; g_p1[o++] = '/'; g_p1[o++] = '.'; g_p1[o++] = '.';
        g_p1[o++] = '/';
    }
    g_p1[o++] = 'z'; g_p1[o] = '\0';                   /* 262 バイト */
    EQ(RESOLVE(g_p1, g_out, VFS_MAX_PATH), VFS_ERR_NAMETOOLONG);
    g_p1[o - 1 - 10] = '\0';                            /* 251 バイト: 通る */
    EQ(RESOLVE(g_p1, g_out, VFS_MAX_PATH), VFS_OK);

    /* ---- 32 / 33 要素 ("/hd0" が 1 個目) ---- */
    o = 0;
    g_p1[o++] = '/'; g_p1[o++] = 'h'; g_p1[o++] = 'd'; g_p1[o++] = '0';
    for (i = 1; i < 32; i++) {
        g_p1[o++] = '/'; g_p1[o++] = 'd';
        g_p1[o] = '\0';
        EQ(vfs_mkdir(g_p1), VFS_OK);              /* mkdir -p の各段 */
    }
    EQ(RESOLVE(g_p1, g_out, VFS_MAX_PATH), VFS_OK);   /* 32 要素 */
    CHECK(wfile(g_p1 + 0, "") < 0);               /* ディレクトリ */
    g_p1[o++] = '/'; g_p1[o++] = 'e'; g_p1[o] = '\0';          /* 33 要素 */
    w0 = wr_non_state();
    EQ(RESOLVE(g_p1, g_out, VFS_MAX_PATH), VFS_ERR_NAMETOOLONG);
    EQ(vfs_mkdir(g_p1), VFS_ERR_NAMETOOLONG);     /* 旧実装は EXIST で成功に見えた */
    EQ(wfile(g_p1, "deep"), VFS_ERR_NAMETOOLONG);
    CHECK(wr_non_state() == w0);
    /* 33 個目の後の ".." で 32 に戻る形も断る (保持済みの要素を消させない) */
    g_p1[o++] = '/'; g_p1[o++] = '.'; g_p1[o++] = '.';
    g_p1[o++] = '/'; g_p1[o++] = 'f'; g_p1[o] = '\0';
    EQ(RESOLVE(g_p1, g_out, VFS_MAX_PATH), VFS_ERR_NAMETOOLONG);
    /* 途中で ".." で戻るなら 33 個を超えて書いても通る (積んだ数で数える) */
    EQ(RESOLVE("/hd0/a/../b/../c/../d", g_out, VFS_MAX_PATH), VFS_OK);
    CHECK(streq(g_out, "/hd0/d"));

    /* ---- 相対パス + 長い cwd: 結果で判定する ---- */
    mk_long(g_p1, 200, 'c', 0);
    EQ(vfs_mkdir(g_p1), VFS_OK);
    EQ(vfs_chdir(g_p1), VFS_OK);                  /* cwd は 206 バイト */
    /* cwd + "/" + "../" + 60 文字 = 連結は 256 超、結果は 65 バイト → 通る */
    {
        static char rel[80];
        int k = 0;
        rel[k++] = '.'; rel[k++] = '.'; rel[k++] = '/';
        for (i = 0; i < 60; i++) rel[k++] = 'r';
        rel[k] = '\0';
        EQ(RESOLVE(rel, g_out, VFS_MAX_PATH), VFS_OK);
        CHECK(h_strlen(g_out) == 5 + 60);
        CHECK(wfile(rel, "rel") >= 0);
        CHECK(slurp(g_out + 4, g_p2, 16) == 3);
        /* 結果が 256 を超える相対名は断る */
        EQ(RESOLVE("0123456789012345678901234567890123456789012345678901",
                            g_out, VFS_MAX_PATH), VFS_ERR_NAMETOOLONG);
        EQ(RESOLVE("0123456789012345678901234567890123456789012345678",
                            g_out, VFS_MAX_PATH), VFS_OK);
    }
    kstrncpy(cwd, "/", VFS_MAX_PATH);

    /* ---- mount prefix は切り詰めて登録しない ---- */
    mk_long(g_p1, 251, 'm', 0);                   /* 256 */
    EQ(vfs_mount(g_p1, "hd1", "ext2"), VFS_ERR_NAMETOOLONG);
    for (i = 1; i < VFS_MAX_FS; i++) CHECK(!mounts[i].in_use);

    /* ---- 失効の無い普通の経路は従来どおり ---- */
    fd = vfs_open("/hd0/ok", O_CREAT | O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_write_fd(fd, "ok", 2), 2);
    vfs_close(fd);
    dump_image("path.img");
}

/* ======================================================================== */
/*  段 pkg: rt/pkg.c                                                        */
/* ======================================================================== */

#define CD_FD 900
static const char *g_cd_set;    /* 段 cdinst の PKG の組 ("A_" など)、ふだんは NULL */
static int g_cd_hfd = -1;
static char g_cd_path[512];

/* "/cd0/<名前>" はホストの <pkgdir>/<名前>、ほかは実物の VFS */
static int __cdecl f_sys_open(const char *path, int mode)
{
    if (path[0] == '/' && path[1] == 'c' && path[2] == 'd' && path[3] == '0' &&
        path[4] == '/') {
        u32 i = 0, j;
        for (j = 0; g_pkg_dir[j] && i < 400; j++) g_cd_path[i++] = g_pkg_dir[j];
        g_cd_path[i++] = '/';
        /* 段 cdinst: 固定名 (/cd0/NORMAL.PKG 等) を組ごとの名前 (A_NORMAL.PKG) へ */
        for (j = 0; g_cd_set && g_cd_set[j] && i < 450; j++) g_cd_path[i++] = g_cd_set[j];
        for (j = 5; path[j] && i < 500; j++) g_cd_path[i++] = path[j];
        g_cd_path[i] = '\0';
        g_cd_hfd = h_sys3(5, (long)g_cd_path, 0, 0);
        return g_cd_hfd < 0 ? VFS_ERR_NOTFOUND : CD_FD;
    }
    return vfs_open(path, mode);
}
static void __cdecl f_sys_close(int fd)
{
    if (fd == CD_FD) { (void)h_sys3(6, g_cd_hfd, 0, 0); g_cd_hfd = -1; return; }
    vfs_close(fd);
}
static int __cdecl f_sys_read(int fd, void *buf, u32 size)
{
    if (fd == CD_FD) {
        u32 got = 0;
        while (got < size) {
            int n = h_sys3(3, g_cd_hfd, (long)((u8 *)buf + got), (long)(size - got));
            if (n <= 0) break;
            got += (u32)n;
        }
        return (int)got;
    }
    return vfs_read_fd(fd, buf, size);
}
static int __cdecl f_sys_write(int fd, const void *buf, u32 size)
{ return vfs_write_fd(fd, buf, size); }
static int __cdecl f_sys_lseek(int fd, int off, int whence)
{
    if (fd == CD_FD) return h_sys3(19, g_cd_hfd, off, whence);
    return vfs_seek(fd, off, whence);
}
static int __cdecl f_sys_mkdir(const char *path) { return vfs_mkdir(path); }
static u8 g_arena[256u * 1024u];
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

/* cdinst の install_package_hd と同じ手順 (前置の検査 → 付け替え → 展開) */
static int install_hd(const char *pkg)
{
    int i, j, ret;
    ret = pkg_parse(&g_api, pkg, &g_info);
    if (ret != PKG_OK) return ret;
    if (pkg_first_overflow(&g_info, 4) >= 0) return PKG_ERR_TOOLONG;
    for (i = 0; i < g_info.entry_count; i++) {
        char orig[PKG_MAX_PATH];
        for (j = 0; g_info.entries[i].path[j]; j++) orig[j] = g_info.entries[i].path[j];
        orig[j] = '\0';
        g_info.entries[i].path[0] = '/'; g_info.entries[i].path[1] = 'h';
        g_info.entries[i].path[2] = 'd'; g_info.entries[i].path[3] = '0';
        for (j = 0; orig[j]; j++) g_info.entries[i].path[4 + j] = orig[j];
        g_info.entries[i].path[4 + j] = '\0';
    }
    return pkg_extract(&g_api, pkg, &g_info);
}

/* PKG は test_vfs_fd_path.py が <pkgdir> に作る (名前と中身の約束はそちら) */
static void case_pkg(void)
{
    int n;
    u32 w0;
    char buf[16];

    report("case pkg\n");
    if (!g_pkg_dir) { report("  (harness) pkg dir missing\n"); g_failures++; return; }
    disk_setup();
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

    /* 格納パス 127 バイト (NUL 込み 128 = 器ちょうど) は読める */
    EQ(pkg_parse(&g_api, "/cd0/P127.PKG", &g_info), PKG_OK);
    CHECK(g_info.entry_count == 2 && h_strlen(g_info.entries[1].path) == 127);
    CHECK(g_info.entries[1].size == 5);
    /* 128 バイトは即エラー (旧実装は 127 だけ読んで読み位置がずれた) */
    EQ(pkg_parse(&g_api, "/cd0/P128.PKG", &g_info), PKG_ERR_TOOLONG);
    /* 128 項目は読める、129 項目目は即エラー (旧実装は黙って捨てた) */
    EQ(pkg_parse(&g_api, "/cd0/E128.PKG", &g_info), PKG_OK);
    CHECK(g_info.entry_count == 128);
    EQ(pkg_parse(&g_api, "/cd0/E129.PKG", &g_info), PKG_ERR_CORRUPT);
    /* 表の途中で切れている / ヘッダの項目数と合わない */
    EQ(pkg_parse(&g_api, "/cd0/TRUNC.PKG", &g_info), PKG_ERR_CORRUPT);
    EQ(pkg_parse(&g_api, "/cd0/COUNT.PKG", &g_info), PKG_ERR_CORRUPT);

    /* cdinst の前置: 格納 123 は通り、124〜127 は展開の前に断る */
    for (n = 123; n <= 127; n++) {
        char name[16];
        name[0] = '/'; name[1] = 'c'; name[2] = 'd'; name[3] = '0'; name[4] = '/';
        name[5] = 'L'; name[6] = (char)('0' + n / 100);
        name[7] = (char)('0' + (n / 10) % 10); name[8] = (char)('0' + n % 10);
        name[9] = '.'; name[10] = 'P'; name[11] = 'K'; name[12] = 'G'; name[13] = '\0';
        EQ(pkg_parse(&g_api, name, &g_info), PKG_OK);
        if (n == 123) {
            EQ(pkg_first_overflow(&g_info, 4), -1);
            EQ(install_hd(name), PKG_OK);
        } else {
            CHECK(pkg_first_overflow(&g_info, 4) >= 0);
            w0 = wr_non_state();
            EQ(install_hd(name), PKG_ERR_TOOLONG);
            CHECK(wr_non_state() == w0);          /* 1 セクタも書かない */
        }
    }
    CHECK(exists("/l"));                          /* 123 の親ディレクトリ */

    /* 展開の失敗を伝える: 置き場がディレクトリなら IO (旧実装は OK) */
    EQ(vfs_mkdir("/hd0/clash"), VFS_OK);
    EQ(install_hd("/cd0/CLASH.PKG"), PKG_ERR_IO);
    EQ(install_hd("/cd0/CLASHZ.PKG"), PKG_ERR_IO);   /* LZSS 版 */
    /* 普通の PKG は展開できる */
    EQ(install_hd("/cd0/GOOD.PKG"), PKG_OK);
    CHECK(slurp("/good/a.txt", buf, sizeof(buf)) == 5 && streq(buf, "hello"));
    dump_image("pkg.img");
}

/* ======================================================================== */
/*  段 cdinst: 実物の userland/system/cdinst.c の install_packages          */
/*  (Codex / Opus 実装レビュー ラリー 1: MINIMAL 以外の失敗でも止め、完了を   */
/*  表示しない)                                                             */
/* ======================================================================== */

#ifndef FDP_RED
static int g_complete_seen;
static char g_missing[128];     /* "missing on CD: %s" で出た名前を ',' で連ねる */
static void __cdecl f_kprintf_cap(u8 attr, const char *fmt, ...)
{
    va_list ap;
    const char *p;
    (void)attr;
    va_start(ap, fmt);
    {
        const char *k = "  missing on CD: %s";
        u32 i = 0;
        while (k[i] && fmt[i] == k[i]) i++;
        if (!k[i]) {
            const char *a = va_arg(ap, const char *);
            u32 n = 0;
            while (g_missing[n]) n++;
            while (*a && n < sizeof(g_missing) - 2) g_missing[n++] = *a++;
            g_missing[n++] = ',';
            g_missing[n] = '\0';
            va_end(ap);
            return;
        }
    }
    if (fmt[0] == '%' && fmt[1] == 's') {
        const char *a = va_arg(ap, const char *);
        for (p = a; *p; p++) {
            const char *k = "Installation Complete";
            u32 i = 0;
            while (k[i] && p[i] == k[i]) i++;
            if (!k[i]) g_complete_seen = 1;
        }
    }
    va_end(ap);
}
static int __cdecl f_sys_stat(const char *path, OS32_Stat *st)
{
    int fd, end;
    if (path[0] == '/' && path[1] == 'c' && path[2] == 'd' && path[3] == '0') {
        fd = f_sys_open(path, O_RDONLY);
        if (fd < 0) return fd;
        end = f_sys_lseek(fd, 0, SEEK_END);
        f_sys_close(fd);
        kmemset(st, 0, sizeof(*st));
        st->st_size = (u32)end;
        return 0;
    }
    return vfs_stat(path, st);
}
static int __cdecl f_vfs_sync(void) { return vfs_sync(); }

/* 先に無効側 (OS32_DBG_SERIAL なし) で取り込み、cdinst.c の DBG を空にする */
#include "rt/dbgserial.h"
#define main cdinst_main
#include "../system/cdinst.c"   /* -I の userland/lib (変異では写し) から */
#undef main
/* 段 2 (票 TASK_HDD_INSTALL): cdinst が繋ぐ hd0 の共有部。pc98pt.c と
 * ext2_layout.c は hdd_pt_fake.h が取り込み済み。ここの段は install_packages と
 * report_missing_packages だけを回す (hd0 の手順の試験は test_hdd_stage2.py) */
#include "../system/inst_hdd.c"
#include "../system/inst_disk.c"
#include "userland/shell/hdprep_plan.c"   /* -I のリポジトリ根から */

static void cdinst_setup(const char *set)
{
    disk_setup();
    kmemset(&g_api, 0, sizeof(g_api));
    g_api.sys_open = f_sys_open;
    g_api.sys_close = f_sys_close;
    g_api.sys_read = f_sys_read;
    g_api.sys_write = f_sys_write;
    g_api.sys_lseek = f_sys_lseek;
    g_api.sys_mkdir = f_sys_mkdir;
    g_api.sys_stat = f_sys_stat;
    g_api.vfs_sync = f_vfs_sync;
    g_api.mem_alloc = f_mem_alloc;
    g_api.mem_free = f_mem_free;
    g_api.kprintf = f_kprintf_cap;
    api = &g_api;
    g_arena_used = 0;
    g_complete_seen = 0;
    g_missing[0] = '\0';
    g_cd_set = set;
}

static void case_cdinst(void)
{
    report("case cdinst\n");
    if (!g_pkg_dir) { report("  (harness) pkg dir missing\n"); g_failures++; return; }

    /* A: NORMAL が格納パス 124 バイトで PATH TOO LONG → そこで止まる。
     * DEBUG は展開しない、完了は表示しない */
    cdinst_setup("A_");
    EQ(install_packages('3'), PKG_ERR_TOOLONG);
    CHECK(exists("/good/a.txt"));             /* MINIMAL は済んでいる */
    CHECK(!exists("/full"));
    CHECK(!g_complete_seen);

    /* B: 分割の 2 本目 (NORMAL2) の展開が置き場の衝突で IO → 止める。
     * 1 本目は済み、DEBUG へは進まない、完了は表示しない */
    cdinst_setup("B_");
    EQ(vfs_mkdir("/hd0/clash"), VFS_OK);
    EQ(install_packages('3'), PKG_ERR_IO);
    CHECK(exists("/n/a.txt"));
    CHECK(!exists("/full"));
    CHECK(!g_complete_seen);

    /* D: MINIMAL の失敗 (従来から止まる) — NORMAL へ進まない */
    cdinst_setup("D_");
    EQ(install_packages('2'), PKG_ERR_TOOLONG);
    CHECK(!exists("/n"));
    CHECK(!g_complete_seen);

    /* E: Full を選んだのに DEBUG が媒体に無い → 飛ばさず失敗、完了は表示しない */
    cdinst_setup("E_");
    EQ(install_packages('3'), PKG_ERR_IO);
    CHECK(exists("/n/a.txt"));
    CHECK(!g_complete_seen);

    /* F: GUI (MINIMAL → GUI → NORMAL の 2 段目) の失敗 → NORMAL へ進まない */
    cdinst_setup("F_");
    EQ(install_packages('2'), PKG_ERR_TOOLONG);
    CHECK(exists("/good/a.txt"));
    CHECK(!exists("/n"));
    CHECK(!g_complete_seen);

    /* G: Normal を選んだのに GUI が媒体に無い → 飛ばさず失敗 */
    cdinst_setup("G_");
    EQ(install_packages('2'), PKG_ERR_IO);
    CHECK(!exists("/n"));
    CHECK(!g_complete_seen);
    /* 欠けたパッケージは HDD を消す前に名前で知らせる (往復 2 の minor) */
    cdinst_setup("G_");
    EQ(report_missing_packages('1'), 0);
    CHECK(g_missing[0] == '\0');
    EQ(report_missing_packages('3'), 2);        /* GUI と DEBUG */
    CHECK(kstrcmp(g_missing, "GUI,DEBUG,") == 0);

    /* H: Minimal は GUI を展開しない (CUI のみ) */
    cdinst_setup("C_");
    EQ(install_packages('1'), PKG_OK);
    CHECK(exists("/good/a.txt") && !exists("/g") && !exists("/n"));
    CHECK(g_complete_seen);

    /* C: 全部通れば完了を表示する。NORMAL は 2 本に分かれていて両方展開する */
    cdinst_setup("C_");
    EQ(install_packages('3'), PKG_OK);
    CHECK(exists("/good/a.txt") && exists("/g/a.txt") && exists("/n/a.txt")
          && exists("/n2/b.txt") && exists("/full/f.txt"));
    CHECK(g_complete_seen);
    g_cd_set = (const char *)0;
}
#endif

#ifndef FDP_RED
/* ======================================================================== */
/*  段 fatname: 実物の fs/fatfs_vfs.c + fs/fatfs/ff.c (RAM の FAT12) で、    */
/*  FatFs が「別の綴りを同じ名前」と読む名前を入口で断る (TASK_VFS_FD_PATH  */
/*  実装レビュー ラリー 2 の blocker)。旧実装は vfs_rm("/fd0/disk.img ") が   */
/*  BUSY の比較 (1 バイトの fold) をすり抜け、FatFs が DISK.IMG を消した。  */
/*  段 hostname: Win32 の規則 (末尾の空白と '.'、'\') を合成ドライバで。     */
/*  段 namerule: 規則そのもの (fs/vfs_name_rules.inc) の表。                 */
/* ======================================================================== */

#include "fatfs/ff.h"
#include "fatfs/diskio.h"

#define FAT_SECTORS 2880u                   /* 1.44MB */
static u8 g_fatdisk[FAT_SECTORS * 512u];

DSTATUS disk_initialize(BYTE pdrv) { return pdrv == 0 ? 0 : STA_NOINIT; }
DSTATUS disk_status(BYTE pdrv) { return pdrv == 0 ? 0 : STA_NOINIT; }
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || sector + count > FAT_SECTORS) return RES_PARERR;
    kmemcpy(buff, g_fatdisk + sector * 512u, count * 512u);
    return RES_OK;
}
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || sector + count > FAT_SECTORS) return RES_PARERR;
    kmemcpy(g_fatdisk + sector * 512u, buff, count * 512u);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0) return RES_PARERR;
    if (cmd == CTRL_SYNC) return RES_OK;
    if (cmd == GET_SECTOR_COUNT) { *(LBA_t *)buff = FAT_SECTORS; return RES_OK; }
    if (cmd == GET_SECTOR_SIZE) { *(WORD *)buff = 512; return RES_OK; }
    if (cmd == GET_BLOCK_SIZE) { *(DWORD *)buff = 1; return RES_OK; }
    return RES_PARERR;
}
DWORD get_fattime(void) { return ((DWORD)(2026 - 1980) << 25) | (9u << 21) | (24u << 16); }
void diskio_set_fdd_drive(int drv) { (void)drv; }
void diskio_set_hdd_drive(int drv) { (void)drv; }
void diskio_set_hdd_partition(u32 offset) { (void)offset; }
void diskio_set_hdd_sector_size(u16 sz) { (void)sz; }
void diskio_set_hdd_ide_phys_size(u16 sz) { (void)sz; }
u32 dos_time_to_epoch(u16 fdate, u16 ftime) { (void)fdate; (void)ftime; return 0; }
int memcmp(const void *a, const void *b, u32 n)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    u32 i;
    for (i = 0; i < n; i++) if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}
char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return (char *)0;
    }
}

#include "fatfs_vfs.c"
/* 修正前の fatfs_vfs.c (規則を取り込まない) と組んでも規則の表は回せるように */
#include "vfs_name_rules.inc"

/* FatFs から直に見る (VFS を通さない): 在る / 中身 */
static int fat_exists(const char *ffpath)
{
    FILINFO fno;
    return f_stat(ffpath, &fno) == FR_OK;
}
static int fat_slurp(const char *ffpath, char *buf, u32 cap)
{
    FIL fil;
    UINT br = 0;
    if (f_open(&fil, ffpath, FA_READ) != FR_OK) return -1;
    if (f_read(&fil, buf, cap - 1, &br) != FR_OK) br = 0;
    f_close(&fil);
    buf[br] = '\0';
    return (int)br;
}

static int g_ls_count;
static void ls_count_cb(const VfsDirEntry *e, void *ctx)
{
    (void)e; (void)ctx;
    g_ls_count++;
}

static void case_namerule(void)
{
    static const struct { const char *p; int fat, win; } t[] = {
        { "",                   VFS_OK,        VFS_OK },
        { "/",                  VFS_OK,        VFS_OK },
        { "/disk.img",          VFS_OK,        VFS_OK },
        { "a//b/",              VFS_OK,        VFS_OK },
        { "/a/./b/../c",        VFS_OK,        VFS_OK },   /* "." / ".." そのもの */
        { "/.x/..y",            VFS_OK,        VFS_OK },   /* 先頭の '.' */
        /* 0x80 以上: FAT は通す (CP437 / SJIS のバイト)。Win32 は最短形の
         * 正しい BMP の UTF-8 だけ (ラリー 3 B2) — SJIS の "あ" は不正な列 */
        { "/\x82\xa0.txt",      VFS_OK,        VFS_ERR_INVAL },
        { "/\xe3\x81\x82.txt",  VFS_OK,        VFS_OK },   /* "あ" (UTF-8) */
        { "/\xe6\x97\xa5\xe6\x9c\xac/\xe8\xaa\x9e", VFS_OK, VFS_OK }, /* 日本/語 */
        { "/caf\xc3\xa9",       VFS_OK,        VFS_OK },   /* U+00E9 */
        { "/\xc2\x80",          VFS_OK,        VFS_OK },   /* U+0080 (2 バイトの最小) */
        { "/\xdf\xbf",          VFS_OK,        VFS_OK },   /* U+07FF */
        { "/\xe0\xa0\x80",      VFS_OK,        VFS_OK },   /* U+0800 (3 バイトの最小) */
        { "/\xed\x9f\xbf",      VFS_OK,        VFS_OK },   /* U+D7FF */
        { "/\xee\x80\x80",      VFS_OK,        VFS_OK },   /* U+E000 */
        { "/\xef\xbf\xbf",      VFS_OK,        VFS_OK },   /* U+FFFF */
        { "/disk.img\xc2",       VFS_OK,        VFS_ERR_INVAL }, /* 途中で切れた列 */
        { "/disk.img\xe3\x81",   VFS_OK,        VFS_ERR_INVAL },
        { "/\xe3\x81/x",         VFS_OK,        VFS_ERR_INVAL }, /* 区切りの前で切れる */
        { "/a\x80",              VFS_OK,        VFS_ERR_INVAL }, /* 単独の継続バイト */
        { "/\xbf",               VFS_OK,        VFS_ERR_INVAL },
        { "/\xc1\xa4isk.img",    VFS_OK,        VFS_ERR_INVAL }, /* 冗長 = "disk.img" */
        { "/\xc0\xae",           VFS_OK,        VFS_ERR_INVAL }, /* 冗長 = "." */
        { "/\xe0\x81\xa4",      VFS_OK,        VFS_ERR_INVAL }, /* 3 バイトの冗長 */
        { "/\xe0\x9f\xbf",      VFS_OK,        VFS_ERR_INVAL }, /* 冗長 (U+07FF) */
        { "/\xed\xa0\x80",      VFS_OK,        VFS_ERR_INVAL }, /* U+D800 サロゲート */
        { "/\xed\xbf\xbf",      VFS_OK,        VFS_ERR_INVAL }, /* U+DFFF */
        { "/\xf0\x9f\x98\x80.img", VFS_OK,     VFS_ERR_INVAL }, /* U+1F600 (BMP 外) */
        { "/\xf4\x8f\xbf\xbf",  VFS_OK,        VFS_ERR_INVAL },
        { "/\xf8\x88\x80\x80\x80", VFS_OK,    VFS_ERR_INVAL },
        { "/\xff",               VFS_OK,        VFS_ERR_INVAL },
        { "/\xe3\x81\x82\x80",  VFS_OK,        VFS_ERR_INVAL }, /* 正しい文字の後の余り */
        /* Win32 の禁止文字 (FAT は FatFs 自身が断るので入口では見ない) */
        { "/disk.img::$DATA",   VFS_OK,        VFS_ERR_INVAL },
        { "/disk.img:s",        VFS_OK,        VFS_ERR_INVAL },
        { "/a*",                VFS_OK,        VFS_ERR_INVAL },
        { "/a?",                VFS_OK,        VFS_ERR_INVAL },
        { "/a\"b",              VFS_OK,        VFS_ERR_INVAL },
        { "/a<b",               VFS_OK,        VFS_ERR_INVAL },
        { "/a>b",               VFS_OK,        VFS_ERR_INVAL },
        { "/a|b",               VFS_OK,        VFS_ERR_INVAL },
        { "/a+b=c[1];d,e",      VFS_OK,        VFS_OK },   /* Win32 では正当 */
        { "/disk.img ",         VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/disk.img /x",       VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/disk.img ignored",  VFS_ERR_INVAL, VFS_OK },
        { "/my file.txt",       VFS_ERR_INVAL, VFS_OK },
        { "/ lead",             VFS_ERR_INVAL, VFS_OK },
        { "/disk.",             VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/disk.img.",         VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/d./x",              VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/...",               VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/\\disk.img",        VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/d\\img.dat",        VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/disk.img\x01",      VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/a\x1f" "b",         VFS_ERR_INVAL, VFS_ERR_INVAL },
        { "/a\tb",              VFS_ERR_INVAL, VFS_ERR_INVAL },
    };
    u32 i;

    report("case namerule\n");
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        int f = vfs_name_rule_check(t[i].p, VFS_NAME_RULE_FAT);
        int w = vfs_name_rule_check(t[i].p, VFS_NAME_RULE_WIN32);
        CHECK(f == t[i].fat);
        CHECK(w == t[i].win);
        if (f != t[i].fat || w != t[i].win) {
            report("    ^ row "); report_i((int)i); report("\n");
        }
    }
    CHECK(vfs_name_rule_check((const char *)0, VFS_NAME_RULE_FAT) == VFS_OK);
}

/* FD 起動と同じく FAT を "/" にマウントすると、FS に届く相対パスは VFS の
 * 上限 (255 バイト) まで伸びる。ff_make_path は "0:" を前に付けるので、
 * 以前は kstrncat が末尾を黙って落とし、255 バイトの ".../disk1.imgXY" が
 * 253 バイトの ".../disk1.img" (pinned) を消した (実装レビュー ラリー 3、
 * Codex B1)。収まらなければ NAMETOOLONG で、FatFs には何も渡らない。
 * fpath は VFS_MAX_PATH (256): "0:" + 253 + NUL が上限。 */
static void case_fatroot(void)
{
    static char buf[64];
    char *P = g_p1, *X = g_p2;
    OS32_Stat st;
    u32 n, i;
    int fd, fd2;

    report("case fatroot\n");
    EQ(vfs_mount("/", "fd0", "fat"), VFS_OK);
    /* P = "/" + "abcdefgh/" x 27 (244 バイト、27 階層) */
    n = 0;
    P[n++] = '/';
    for (i = 0; i < 27; i++) {
        kmemcpy(P + n, "abcdefgh", 8);
        n += 8;
        P[n] = '\0';
        EQ(vfs_mkdir(P), VFS_OK);
        P[n++] = '/';
    }
    P[n] = '\0';
    CHECK(n == 244);

    /* 253 バイト: 通る (境界の内側) */
    kstrncpy(X, P, VFS_MAX_PATH * 2);
    kstrncat(X, "disk1.img", VFS_MAX_PATH * 2);
    CHECK(h_strlen(X) == 253);
    CHECK(wfile(X, "PINNED") == 6);
    EQ(vfs_read(X, buf, sizeof(buf)), 6);
    fd = vfs_open(X, O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_fd_set_pinned(fd, 1), VFS_OK);
    EQ(vfs_rm(X), VFS_ERR_BUSY);

    /* 255 バイト: Codex の反例 — 以前は "XY" が落ちて pinned の実体が消えた */
    kstrncpy(X, P, VFS_MAX_PATH * 2);
    kstrncat(X, "disk1.imgXY", VFS_MAX_PATH * 2);
    CHECK(h_strlen(X) == 255);
    EQ(vfs_rm(X), VFS_ERR_NAMETOOLONG);
    EQ(vfs_rename(X, "/moved"), VFS_ERR_NAMETOOLONG);
    CHECK(wfile("/src", "SRC") == 3);
    EQ(vfs_rename("/src", X), VFS_ERR_NAMETOOLONG);   /* 置き換えにもならない */
    CHECK(vfs_open(X, O_RDWR | O_CREAT | O_TRUNC) == VFS_ERR_NAMETOOLONG);
    CHECK(vfs_write(X, "Z", 1) == VFS_ERR_NAMETOOLONG);
    fd2 = vfs_open(X, O_RDONLY);
    CHECK(fd2 == VFS_ERR_NAMETOOLONG);
    if (fd2 >= 3) vfs_close(fd2);

    /* 254 バイト: "0:" を付けると 257 > 256 — NAMETOOLONG */
    kstrncpy(X, P, VFS_MAX_PATH * 2);
    kstrncat(X, "disk1.imgX", VFS_MAX_PATH * 2);
    CHECK(h_strlen(X) == 254);
    EQ(vfs_rm(X), VFS_ERR_NAMETOOLONG);
    EQ(vfs_stat(X, &st), VFS_ERR_NAMETOOLONG);
    CHECK(vfs_open(X, O_RDWR | O_CREAT) == VFS_ERR_NAMETOOLONG);
    CHECK(vfs_write(X, "Z", 1) == VFS_ERR_NAMETOOLONG);
    EQ(vfs_read(X, buf, sizeof(buf)), VFS_ERR_NAMETOOLONG);
    EQ(vfs_mkdir(X), VFS_ERR_NAMETOOLONG);
    EQ(vfs_rmdir(X), VFS_ERR_NAMETOOLONG);

    EQ(vfs_fd_set_pinned(fd, 0), VFS_OK);
    vfs_close(fd);
    /* 実体は残り、中身もそのまま */
    kstrncpy(X, P, VFS_MAX_PATH * 2);
    kstrncat(X, "disk1.img", VFS_MAX_PATH * 2);
    EQ(vfs_read(X, buf, sizeof(buf)), 6);
    buf[6] = '\0';
    CHECK(streq(buf, "PINNED"));
    CHECK(fat_exists("0:/SRC") && !fat_exists("0:/MOVED"));
    EQ(vfs_rm(X), VFS_OK);                           /* 放した後は消せる */

    vfs_umount("/");
}

/* 段 utf8: 名前の規則 (Win32) と lib/kutf16.c の kutf8_to_utf16le が**同じ
 * 集合**を通し、通ったものは 1 対 1 に変換される (ラリー 3 B2)。
 * 以前の変換は途中で切れた列で打ち切り ("disk.img\xC2" → "disk.img")、
 * 冗長な符号化を受理し ("\xC1\xA4isk.img" → "disk.img")、BMP 外を U+FFFD
 * に置き換えた (絵文字 2 種が同じ名前に)。
 * 1〜3 バイトの列を、ASCII は 'a' だけ・それ以外は 0x80〜0xFF の全部で
 * 総当たりする (130^3 通り)。 */
#include "kutf16.c"

static void case_utf8(void)
{
    static u16 w[16];
    static char back[16];
    u8 in[4];
    u32 a, b, c;
    u32 bad_agree = 0, bad_round = 0, n_ok = 0;

    report("case utf8\n");
    for (a = 0x7F; a <= 0xFF; a++) {
        for (b = 0x7E; b <= 0xFF; b++) {
            for (c = 0x7D; c <= 0xFF; c++) {
                int rule, words;
                in[0] = (u8)(a == 0x7F ? 'a' : a);
                in[1] = (u8)(b == 0x7E ? 0 : (b == 0x7F ? 'a' : b));
                in[2] = (u8)(c == 0x7D ? 0 : (c == 0x7E || c == 0x7F ? 'a' : c));
                in[3] = 0;
                if (in[1] == 0 && c != 0x7D) continue;   /* 同じ列の重複 */
                rule = vfs_name_rule_check((const char *)in, VFS_NAME_RULE_WIN32);
                words = kutf8_to_utf16le((const char *)in, w, 16);
                if ((rule == VFS_OK) != (words >= 1)) {
                    if (bad_agree++ < 4) {
                        report("    disagree "); report_i((int)a); report(" ");
                        report_i((int)b); report(" "); report_i((int)c); report("\n");
                    }
                    continue;
                }
                if (words < 1) continue;
                n_ok++;
                /* 変換は逆変換で元のバイト列に戻る = 別名を作らない */
                kutf16le_to_utf8(w, (words - 1) * 2, back, sizeof(back));
                if (kstrcmp(back, (const char *)in) != 0) bad_round++;
            }
        }
    }
    CHECK(bad_agree == 0);
    CHECK(bad_round == 0);
    CHECK(n_ok > 60000u);       /* 3 バイトの BMP だけで 61440 - 2048 通り */

    /* Codex の反例: 変換は失敗を返す (置換も打ち切りもしない) */
    CHECK(kutf8_to_utf16le("disk.img\xc2", w, 16) == -1);
    CHECK(kutf8_to_utf16le("\xc1\xa4isk.img", w, 16) == -1);
    CHECK(kutf8_to_utf16le("\xf0\x9f\x98\x80.img", w, 16) == -1);
    CHECK(kutf8_to_utf16le("\xf0\x9f\x98\x81.img", w, 16) == -1);
    CHECK(kutf8_to_utf16le("a\x80", w, 16) == -1);
    CHECK(kutf8_to_utf16le("\xed\xa0\x80", w, 16) == -1);
    /* 正当な日本語 (3 バイト) は通り、中身も正しい */
    CHECK(kutf8_to_utf16le("\xe6\x97\xa5.txt", w, 16) == 6);
    CHECK(w[0] == 0x65E5 && w[1] == '.' && w[4] == 't' && w[5] == 0);
    /* 収まらなければ切り詰めずに失敗 */
    CHECK(kutf8_to_utf16le("abc", w, 4) == 4);
    CHECK(kutf8_to_utf16le("abcd", w, 4) == -1);
    CHECK(kutf8_to_utf16le("\xe6\x97\xa5", w, 1) == -1);
    CHECK(kutf8_to_utf16le("", w, 1) == 1 && w[0] == 0);
    CHECK(kutf8_to_utf16le((const char *)0, w, 4) == -1);
}

static void case_fatname(void)
{
    static BYTE work[FF_MAX_SS];
    static char buf[64];
    MKFS_PARM opt;
    VfsSqliteCookie cookie;
    VfsSqliteLease lease;
    OS32_Stat st;
    int fd_img, fd_dat, fd_plain, fd;

    report("case fatname\n");
    disk_setup();
    kmemset(g_fatdisk, 0, sizeof(g_fatdisk));
    kmemset(&opt, 0, sizeof(opt));
    opt.fmt = FM_FAT;
    opt.n_fat = 2;
    CHECK(f_mkfs("0:", &opt, work, sizeof(work)) == FR_OK);
    pdrv_busy[0] = 0;
    pdrv_busy[1] = 0;
    fatfs_init();
    EQ(vfs_mount("/fd0", "fd0", "fat"), VFS_OK);

    /* ---- 正当な 8.3 名は従来どおり (FD 起動の /sys・/bin の *.bin・/etc) ---- */
    CHECK(wfile("/fd0/disk.img", "IMAGEDATA") == 9);
    CHECK(wfile("/fd0/disk", "PLAIN") == 5);
    CHECK(wfile("/fd0/db", "DBDATA") == 6);
    CHECK(wfile("/fd0/other", "O") == 1);
    EQ(vfs_mkdir("/fd0/d"), VFS_OK);
    CHECK(wfile("/fd0/d/img.dat", "DATDATA") == 7);
    EQ(vfs_mkdir("/fd0/bin"), VFS_OK);
    CHECK(wfile("/fd0/bin/ls.bin", "ELF") == 3);
    EQ(vfs_mkdir("/fd0/etc"), VFS_OK);
    CHECK(wfile("/fd0/etc/system.cfg", "GUI=0") == 5);
    EQ(vfs_read("/fd0/BIN/LS.BIN", buf, sizeof(buf)), 3);
    EQ(vfs_stat("/fd0/etc/system.cfg", &st), VFS_OK);
    CHECK(st.st_size == 5);
    EQ(vfs_stat("/fd0/bin/", &st), VFS_OK);
    /* 8.3 に収まらない名前の stat は従来どおり NOTFOUND (hot journal 検査) */
    EQ(vfs_stat("/fd0/etc/settings.db-journal", &st), VFS_ERR_NOTFOUND);
    g_ls_count = 0;
    EQ(vfs_ls("/fd0", ls_count_cb, (void *)0), VFS_OK);
    CHECK(g_ls_count == 7);
    g_ls_count = 0;
    EQ(vfs_ls("/fd0/bin/", ls_count_cb, (void *)0), VFS_OK);
    CHECK(g_ls_count == 1);
    EQ(vfs_rename("/fd0/other", "/fd0/other2.txt"), VFS_OK);
    EQ(vfs_rename("/fd0/other2.txt", "/fd0/other"), VFS_OK);
    fd = vfs_open("/fd0/.x", O_RDWR | O_CREAT);   /* 先頭の '.' は正当 */
    CHECK(fd >= 3);
    if (fd >= 3) vfs_close(fd);
    EQ(vfs_rm("/fd0/.x"), VFS_OK);

    /* ---- 使用中の loop イメージ (pinned) ---- */
    fd_img = vfs_open("/fd0/disk.img", O_RDWR);
    fd_dat = vfs_open("/fd0/d/img.dat", O_RDWR);
    fd_plain = vfs_open("/fd0/disk", O_RDWR);
    CHECK(fd_img >= 3 && fd_dat >= 3 && fd_plain >= 3);
    EQ(vfs_fd_set_pinned(fd_img, 1), VFS_OK);
    EQ(vfs_fd_set_pinned(fd_dat, 1), VFS_OK);
    EQ(vfs_fd_set_pinned(fd_plain, 1), VFS_OK);

    /* 綴りを変えても消せない・動かせない (旧実装は FatFs が同じ実体を消した) */
    EQ(vfs_rm("/fd0/disk.img "), VFS_ERR_INVAL);
    EQ(vfs_rm("/fd0/disk.img ignored"), VFS_ERR_INVAL);
    EQ(vfs_rm("/fd0/disk.img\x01"), VFS_ERR_INVAL);
    EQ(vfs_rm("/fd0/\\disk.img"), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/disk.img ", "/fd0/moved"), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/other", "/fd0/disk.img "), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/other", "/fd0/\\DISK.IMG"), VFS_ERR_INVAL);
    EQ(vfs_rm("/fd0/d\\img.dat"), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/d\\img.dat", "/fd0/moved"), VFS_ERR_INVAL);
    EQ(vfs_rm("/fd0/disk."), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/disk.", "/fd0/moved"), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/other", "/fd0/DISK."), VFS_ERR_INVAL);
    /* 同じ綴り (大文字小文字違い) は BUSY のまま */
    EQ(vfs_rm("/fd0/DISK.IMG"), VFS_ERR_BUSY);
    EQ(vfs_rm("/fd0/D/IMG.DAT"), VFS_ERR_BUSY);
    EQ(vfs_rm("/fd0/Disk"), VFS_ERR_BUSY);

    /* ---- rmdir は FatFs の f_unlink (ファイルも消す) — 実装レビュー
     *      ラリー 3 (Opus)。pinned の実体は VFS が BUSY で、FAT は種別を見て
     *      NOTDIR で断る (二重の防御) ---- */
    EQ(vfs_rmdir("/fd0/disk.img"), VFS_ERR_BUSY);
    EQ(vfs_rmdir("/fd0/DISK.IMG"), VFS_ERR_BUSY);
    EQ(vfs_rmdir("/fd0/d/img.dat"), VFS_ERR_BUSY);
    EQ(vfs_rmdir("/fd0/d"), VFS_ERR_BUSY);          /* pinned の祖先 */
    EQ(vfs_rmdir("/fd0/other"), VFS_ERR_NOTDIR);    /* pinned でないファイル */
    EQ(vfs_rmdir("/fd0/nothing"), VFS_ERR_NOTFOUND);
    CHECK(fat_exists("0:/OTHER") && fat_exists("0:/DISK.IMG"));
    EQ(vfs_mkdir("/fd0/e"), VFS_OK);
    EQ(vfs_rmdir("/fd0/e"), VFS_OK);                /* 本物のディレクトリは消える */
    CHECK(!fat_exists("0:/E"));

    /* ---- 開いている SQLite DB ---- */
    cookie.group_index = 1;
    cookie.generation = 1;
    EQ(vfs_open_sqlite("/fd0/db", O_RDWR, 1, &cookie, 0, &lease), VFS_OK);
    EQ(vfs_rename("/fd0/db.", "/fd0/z"), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/DB ", "/fd0/z"), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/other", "/fd0/DB."), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/other", "/fd0/db\t"), VFS_ERR_INVAL);
    EQ(vfs_rename("/fd0/DB", "/fd0/z"), VFS_ERR_BUSY);
    EQ(vfs_close_sqlite(&lease), VFS_OK);

    /* ---- 名前を受け取るほかの入口も同じ ---- */
    CHECK(vfs_open("/fd0/disk.img ", O_RDONLY) == VFS_ERR_INVAL);
    CHECK(vfs_open("/fd0/new ", O_RDWR | O_CREAT) == VFS_ERR_INVAL);
    EQ(vfs_stat("/fd0/disk.img ", &st), VFS_ERR_INVAL);
    EQ(vfs_read("/fd0/disk.", buf, sizeof(buf)), VFS_ERR_INVAL);
    CHECK(vfs_write("/fd0/a b", "X", 1) == VFS_ERR_INVAL);
    EQ(vfs_mkdir("/fd0/new."), VFS_ERR_INVAL);
    EQ(vfs_rmdir("/fd0/d."), VFS_ERR_INVAL);
    EQ(vfs_rmdir("/fd0/bin\\"), VFS_ERR_INVAL);
    EQ(vfs_ls("/fd0/d\\", ls_count_cb, (void *)0), VFS_ERR_INVAL);
    CHECK(!fat_exists("0:/A") && !fat_exists("0:/NEW"));

    EQ(vfs_fd_set_pinned(fd_img, 0), VFS_OK);
    EQ(vfs_fd_set_pinned(fd_dat, 0), VFS_OK);
    EQ(vfs_fd_set_pinned(fd_plain, 0), VFS_OK);
    vfs_close(fd_img);
    vfs_close(fd_dat);
    vfs_close(fd_plain);

    /* 実体は消えず・動かず・中身もそのまま */
    CHECK(fat_slurp("0:/DISK.IMG", buf, sizeof(buf)) == 9 && streq(buf, "IMAGEDATA"));
    CHECK(fat_slurp("0:/D/IMG.DAT", buf, sizeof(buf)) == 7 && streq(buf, "DATDATA"));
    CHECK(fat_slurp("0:/DISK", buf, sizeof(buf)) == 5 && streq(buf, "PLAIN"));
    CHECK(fat_slurp("0:/DB", buf, sizeof(buf)) == 6 && streq(buf, "DBDATA"));
    CHECK(fat_slurp("0:/OTHER", buf, sizeof(buf)) == 1);
    CHECK(!fat_exists("0:/MOVED") && !fat_exists("0:/Z"));
    /* 放した後は正しい綴りで消せる */
    EQ(vfs_rm("/fd0/disk.img"), VFS_OK);
    CHECK(!fat_exists("0:/DISK.IMG"));

    vfs_umount("/fd0");
    case_fatroot();
}

/* 合成ドライバ: ext2 の口の前に Win32 (HostDrv) の名前の入口検査を置く。
 * 実物の fs/hostdrvfs.c はハイパーコールを叩くのでホストで組めない。
 * 規則は同じ vfs_name_rule_check (配線は test_vfs_fd_path.py の hostwire 段が
 * fs/hostdrvfs.c の本文で確かめる)。 */
static VfsOps g_win_ops;
#define WIN_CHK(p) do { int r_ = vfs_name_rule_check((p), VFS_NAME_RULE_WIN32); \
                        if (r_ != VFS_OK) return r_; } while (0)
static int win_list(void *c, const char *p, vfs_dir_cb cb, void *u)
{ WIN_CHK(p); return ext2_ops.list_dir(c, p, cb, u); }
static int win_mkdir(void *c, const char *p) { WIN_CHK(p); return ext2_ops.mkdir(c, p); }
static int win_rmdir(void *c, const char *p) { WIN_CHK(p); return ext2_ops.rmdir(c, p); }
static int win_read(void *c, const char *p, void *b, u32 n)
{ WIN_CHK(p); return ext2_ops.read_file(c, p, b, n); }
static int win_write(void *c, const char *p, const void *d, u32 n)
{ WIN_CHK(p); return ext2_ops.write_file(c, p, d, n); }
static int win_unlink(void *c, const char *p) { WIN_CHK(p); return ext2_ops.unlink(c, p); }
static int win_rename(void *c, const char *o, const char *n)
{ WIN_CHK(n); WIN_CHK(o); return ext2_ops.rename(c, o, n); }
static int win_get_size(void *c, const char *p, u32 *s)
{ WIN_CHK(p); return ext2_ops.get_file_size(c, p, s); }
static int win_read_stream(void *c, const char *p, void *b, u32 n, u32 o)
{ WIN_CHK(p); return ext2_ops.read_stream(c, p, b, n, o); }
static int win_write_stream(void *c, const char *p, const void *d, u32 n, u32 o)
{ WIN_CHK(p); return ext2_ops.write_stream(c, p, d, n, o); }
static int win_stat(void *c, const char *p, OS32_Stat *s)
{ WIN_CHK(p); return ext2_ops.stat(c, p, s); }

static void case_hostname(void)
{
    int fd;
    char buf[16];

    report("case hostname\n");
    disk_setup();
    CHECK(wfile("/hd0/disk.img", "IMAGEDATA") >= 0);
    CHECK(wfile("/hd0/other", "O") >= 0);
    EQ(vfs_sync(), VFS_OK);

    g_win_ops = ext2_ops;
    g_win_ops.name = "ext2win";
    g_win_ops.ino = (const VfsInoOps *)0;      /* HostDrv と同じくパスで動く */
    g_win_ops.name_fold = ci_fold;
    g_win_ops.set_mtime = 0;
    g_win_ops.create_excl = 0;
    g_win_ops.list_dir = win_list;
    g_win_ops.mkdir = win_mkdir;
    g_win_ops.rmdir = win_rmdir;
    g_win_ops.read_file = win_read;
    g_win_ops.write_file = win_write;
    g_win_ops.unlink = win_unlink;
    g_win_ops.rename = win_rename;
    g_win_ops.get_file_size = win_get_size;
    g_win_ops.read_stream = win_read_stream;
    g_win_ops.write_stream = win_write_stream;
    g_win_ops.stat = win_stat;
    vfs_register_fs(&g_win_ops);
    EQ(vfs_mount("/hw", "hd0", "ext2win"), VFS_OK);

    fd = vfs_open("/hw/disk.img", O_RDWR);
    CHECK(fd >= 3);
    EQ(vfs_fd_set_pinned(fd, 1), VFS_OK);
    /* Win32 が同じ実体と読む綴り: 末尾の空白・末尾の '.'・'\'・制御文字 */
    EQ(vfs_rm("/hw/disk.img "), VFS_ERR_INVAL);
    EQ(vfs_rm("/hw/DISK.IMG."), VFS_ERR_INVAL);
    EQ(vfs_rm("/hw/disk.img. ."), VFS_ERR_INVAL);
    EQ(vfs_rm("/hw/\\disk.img"), VFS_ERR_INVAL);
    EQ(vfs_rm("/hw/disk.img\x01"), VFS_ERR_INVAL);
    EQ(vfs_rename("/hw/disk.img ", "/hw/moved"), VFS_ERR_INVAL);
    EQ(vfs_rename("/hw/other", "/hw/disk.img."), VFS_ERR_INVAL);
    EQ(vfs_rename("/hw/other", "/hw/disk.img "), VFS_ERR_INVAL);
    CHECK(vfs_open("/hw/disk.img ", O_RDONLY) == VFS_ERR_INVAL);
    EQ(vfs_rm("/hw/DISK.IMG"), VFS_ERR_BUSY);
    /* ラリー 3 B2 (Codex の反例): UTF-16 への変換が別名に化かす綴りと、
     * Win32 の禁止文字 (':' は代替データストリーム) */
    EQ(vfs_rm("/hw/disk.img\xc2"), VFS_ERR_INVAL);
    EQ(vfs_rm("/hw/\xc1\xa4isk.img"), VFS_ERR_INVAL);
    EQ(vfs_rm("/hw/\xf0\x9f\x98\x80.img"), VFS_ERR_INVAL);
    EQ(vfs_rm("/hw/disk.img::$DATA"), VFS_ERR_INVAL);
    EQ(vfs_rm("/hw/a\x80"), VFS_ERR_INVAL);
    EQ(vfs_rename("/hw/disk.img\xc2", "/hw/moved"), VFS_ERR_INVAL);
    EQ(vfs_rename("/hw/\xc1\xa4isk.img", "/hw/moved"), VFS_ERR_INVAL);
    EQ(vfs_rename("/hw/disk.img::$DATA", "/hw/moved"), VFS_ERR_INVAL);
    EQ(vfs_rename("/hw/other", "/hw/disk.img\xc2"), VFS_ERR_INVAL);
    EQ(vfs_rename("/hw/other", "/hw/\xc1\xa4isk.img"), VFS_ERR_INVAL);
    EQ(vfs_rename("/hw/other", "/hw/disk.img:x"), VFS_ERR_INVAL);
    CHECK(vfs_open("/hw/disk.img\xc2", O_RDWR | O_CREAT) == VFS_ERR_INVAL);
    CHECK(vfs_write("/hw/\xf0\x9f\x98\x81.img", "X", 1) == VFS_ERR_INVAL);
    /* 正当な日本語の名前 (3 バイトの UTF-8) は通る */
    CHECK(wfile("/hw/\xe6\x97\xa5\xe6\x9c\xac.txt", "JP") >= 0);
    EQ(vfs_read("/hw/\xe6\x97\xa5\xe6\x9c\xac.txt", buf, sizeof(buf)), 2);
    EQ(vfs_rename("/hw/\xe6\x97\xa5\xe6\x9c\xac.txt", "/hw/\xe8\xaa\x9e.txt"), VFS_OK);
    CHECK(exists("/\xe8\xaa\x9e.txt"));
    EQ(vfs_rm("/hw/\xe8\xaa\x9e.txt"), VFS_OK);
    /* 途中の空白は Windows の名前として正当 (FAT と違う) */
    CHECK(wfile("/hw/my file.txt", "SP") >= 0);
    CHECK(exists("/my file.txt"));
    EQ(vfs_read("/hw/my file.txt", buf, sizeof(buf)), 2);
    EQ(vfs_rename("/hw/my file.txt", "/hw/your file.txt"), VFS_OK);
    EQ(vfs_rm("/hw/your file.txt"), VFS_OK);
    EQ(vfs_fd_set_pinned(fd, 0), VFS_OK);
    vfs_close(fd);
    CHECK(exists("/disk.img") && exists("/other") && !exists("/moved"));
    CHECK(slurp("/disk.img", buf, sizeof(buf)) == 9 && streq(buf, "IMAGEDATA"));
}
#endif

/* sel が "+first-fail" なら全段を回し、どこかの段で落ちたらそこで打ち切る
 * (test_vfs_fd_path.py の変異。RED かどうかだけ分かればよい)。 */
#define FDP_STEP(name, fn) \
    if (all || kstrcmp(sel, name) == 0) { \
        fn(); \
        if (ff && g_failures) goto done; \
    }
static void run(const char *sel)
{
    int ff = (sel != (const char *)0 && kstrcmp(sel, "+first-fail") == 0);
    int all;
    if (ff) sel = (const char *)0;
    all = (sel == (const char *)0);
    FDP_STEP("fd", case_fd)
    FDP_STEP("busy", case_busy)
    FDP_STEP("dot", case_dot)
    FDP_STEP("path", case_path)
    FDP_STEP("pkg", case_pkg)
#ifndef FDP_RED
    FDP_STEP("nocase", case_nocase)
    FDP_STEP("cdinst", case_cdinst)
    FDP_STEP("namerule", case_namerule)
    FDP_STEP("utf8", case_utf8)
    FDP_STEP("fatname", case_fatname)
    FDP_STEP("hostname", case_hostname)
#endif
done:
    report("checks "); report_i(g_checks);
    report(" failures "); report_i(g_failures); report("\n");
}
#undef FDP_STEP

/* argv: <dump_dir> <pkg_dir> [case] */
void fdp_start_c(long *sp);
__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "  movl %esp, %eax\n"
        "  andl $-16, %esp\n"
        "  pushl %eax\n"
        "  call fdp_start_c\n"
        "  hlt\n");

void fdp_start_c(long *sp)
{
    long argc = sp[0];
    const char *sel = (const char *)0;
    if (argc >= 2) g_dump_dir = (const char *)sp[2];
    if (argc >= 3) g_pkg_dir = (const char *)sp[3];
    if (argc >= 4) sel = (const char *)sp[4];
    run(sel);
    die(g_failures ? 1 : 0);
}
