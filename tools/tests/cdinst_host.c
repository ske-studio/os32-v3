/* ========================================================================
 *  cdinst_host.c — 実物の cdinst.c を main から回す (票 TASK_HDD_INSTALL 段 2)
 *
 *  実行:   python3 -B tools/tests/test_hdd_stage2.py
 *  記録:   tools/tests/hdd_stage2_tdd.md
 *
 *  取り込む実物: userland/system/cdinst.c (main ごと)・inst_hdd.c・inst_disk.c・
 *  userland/lib/rt/pkg.c・userland/shell/hdprep_plan.c・drivers/pc98pt.c・
 *  fs/ext2_layout.c。贋物は KAPI だけ:
 *    - /cd0 の .PKG は試験がバイト列で組む (無圧縮、PKG1 形式)
 *    - hd0 は LBA 0〜31 のセクタの模型 (区画表・IPL・ローダの帯)。ext2 の
 *      中身は ext2_format_at の呼び出しを記録するだけ (実物の format_at と
 *      e2fsck は test_hdd_stage1.py が見る)
 *    - /hd0 のファイルは名前と大きさだけを持つ
 *  見るもの: 幾何 (8/17・16/63) と区画表・IPL の一致、モード、事前検査の失敗で
 *  1 セクタも書かないこと、書いた後の失敗で「完了」と言わないこと。
 *
 *  pkg.c の PkgHeader は u32 = unsigned long を詰めて並べるので ILP32 で組む
 *  (ext2_part_host.c と同じ様式 — ホスト -m32、-nostdlib)。
 * ======================================================================== */

#include "os32api.h"
#include <stdarg.h>

void *memcpy(void *dst, const void *src, u32 n);
void *memset(void *dst, int val, u32 n);
u32   strlen(const char *s);
char *strcpy(char *dst, const char *src);

/* ======================================================================== */
/*  libc の代わり                                                           */
/* ======================================================================== */

static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) { }
}

void *memcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst; const u8 *s = (const u8 *)src; u32 i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
void *memset(void *dst, int val, u32 n)
{
    u8 *d = (u8 *)dst; u32 i;
    for (i = 0; i < n; i++) d[i] = (u8)val;
    return dst;
}
u32 strlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0') { }
    return dst;
}
static int h_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}
static int h_strncmp(const char *a, const char *b, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
static const char *h_strstr(const char *h, const char *n)
{
    u32 ln = strlen(n);
    for (; *h; h++) if (h_strncmp(h, n, ln) == 0) return h;
    return ln == 0 ? h : (const char *)0;
}

static void out_raw(const char *p, u32 len)
{
    while (len > 0) {
        int n;
        __asm__ volatile("int $0x80" : "=a"(n) : "a"(4), "b"(2), "c"(p), "d"(len)
                         : "memory");
        if (n <= 0) die(3);
        p += n;
        len -= (u32)n;
    }
}
static void report(const char *t) { out_raw(t, strlen(t)); }

/* ---- kprintf の捕捉 (必要な書式だけ: %s %d %u %x %c、幅と 0 埋め、l) ---- */
static char cap[131072];
static u32  cap_len;

static void cap_ch(char c)
{
    if (c == '\0') c = '?';                /* NUL で捕捉が切れないように (画面には映さない字) */
    if (cap_len + 1 < sizeof(cap)) { cap[cap_len++] = c; cap[cap_len] = '\0'; }
}
static void cap_num(unsigned long v, int base, int width, char pad, int neg)
{
    char b[24];
    int i = 0;
    if (v == 0) b[i++] = '0';
    while (v > 0) { b[i++] = "0123456789abcdef"[v % (unsigned long)base]; v /= (unsigned long)base; }
    if (neg) b[i++] = '-';
    while (i < width) b[i++] = pad;
    while (i > 0) cap_ch(b[--i]);
}
static void cap_vfmt(const char *f, va_list ap)
{
    for (; *f; f++) {
        int width = 0;
        char pad = ' ';
        if (*f != '%') { cap_ch(*f); continue; }
        f++;
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == 'l') f++;
        switch (*f) {
        case 's': { const char *s = va_arg(ap, const char *); while (s && *s) cap_ch(*s++); break; }
        case 'c': cap_ch((char)va_arg(ap, int)); break;
        case 'd': { int v = va_arg(ap, int);
                    cap_num(v < 0 ? (unsigned long)(-(long)v) : (unsigned long)v, 10, width, pad, v < 0);
                    break; }
        case 'u': cap_num(va_arg(ap, unsigned long), 10, width, pad, 0); break;
        case 'x': cap_num(va_arg(ap, unsigned long), 16, width, pad, 0); break;
        case '%': cap_ch('%'); break;
        default:  cap_ch('?'); break;
        }
    }
}
static void __cdecl f_kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    (void)attr;
    va_start(ap, fmt);
    cap_vfmt(fmt, ap);
    va_end(ap);
}

static void fail(const char *what, int line)
{
    char b[16];
    int i = 15, v = line;
    b[i] = '\0';
    while (v > 0) { b[--i] = (char)('0' + v % 10); v /= 10; }
    report("FAIL line ");
    report(&b[i]);
    report(": ");
    report(what);
    report("\n--- output ---\n");
    report(cap);
    report("\n");
    die(1);
}
#define CHECK(x) do { if (!(x)) fail(#x, __LINE__); } while (0)
#define CHECK_STR(s) do { if (!h_strstr(cap, (s))) fail("message missing: " s, __LINE__); } while (0)
#define CHECK_NOSTR(s) do { if (h_strstr(cap, (s))) fail("unexpected message: " s, __LINE__); } while (0)

/* ======================================================================== */
/*  記憶域 (単純な積み上げ。free は何もしない)                                */
/* ======================================================================== */

static u8  arena[8u * 1024u * 1024u];
static u32 arena_used;
static void *arena_get(u32 n)
{
    void *p;
    n = (n + 7u) & ~7u;
    if (arena_used + n > sizeof(arena)) fail("arena full", __LINE__);
    p = arena + arena_used;
    arena_used += n;
    return p;
}
static void *__cdecl f_mem_alloc(u32 n) { return arena_get(n); }
static void __cdecl f_mem_free(void *p) { (void)p; }

/* ======================================================================== */
/*  ファイル (CD の PKG は中身あり、/hd0 は名前と大きさだけ)                  */
/* ======================================================================== */

#define FX_MAX 256
/* vsize: stat が名乗る長さ (表だけの PKG は中身を持たずに正しい長さを名乗る。
 * 0 なら size) */
typedef struct { int used; int is_dir; char path[132]; u8 *data; u32 size; u32 vsize; } Fx;
static Fx fx[FX_MAX];

static int fx_find(const char *p)
{
    int i;
    for (i = 0; i < FX_MAX; i++) if (fx[i].used && h_strcmp(fx[i].path, p) == 0) return i;
    return -1;
}
static int fx_new(const char *p)
{
    int i;
    for (i = 0; i < FX_MAX; i++) {
        if (!fx[i].used) {
            memset(&fx[i], 0, sizeof(fx[i]));
            fx[i].used = 1;
            strcpy(fx[i].path, p);
            return i;
        }
    }
    fail("fx table full", __LINE__);
    return -1;
}

#define FD_MAX 8
static struct { int used; int fi; u32 pos; int wr; } fds[FD_MAX];

static const char *inj_open_fail;     /* この /hd0 の名前は作れない */
static const char *inj_stat_wrong;    /* この名前の stat は 1 バイト違う長さを名乗る */
static int inj_sync_fail, inj_mount_fail, inj_format_fail, inj_readback_bad = -1;
static int inj_readback_off = 3;      /* 読み戻しで違える位置 (3 か 511 = 末尾) */
static int inj_write_fail_lba = -1;   /* この LBA の ide_write_sector が負 (媒体は変わらない) */
static int inj_read_fail_lba = -1;    /* この LBA の ide_read_sector が負 */
static int inj_read_fail_after_write; /* 1 = 最初の書き込みの後だけ (読み戻し) */
static int hd0_mounts, hd0_at_hd0, inj_root_hd0, inj_umount_fail;
static int hd0_other_dev;             /* /hd0 には別のデバイス (hd1) がマウントされている */
static int inj_mount_stays;           /* umount が成功しても hd0 のマウントが残る */
/* 実物の umount (fs/vfs.c vfs_umount) は外す前に ops->sync() を呼び、ext2 の dirty な
 * メタデータを書き出し得る (成否に関わらず)。inj_umount_sync = 1 でそれを再現し、
 * umount_syncs に数える (インストーラの ide_write_sector = writes とは別) */
static int inj_umount_sync, umount_syncs;

/* ---- 鍵の台本: 長さ付きのバイト列 (NUL も 1 バイトの入力)。尽きたら「入力なし」
 * = -1 (実物の kbd_trygetchar / serial_trygetchar と同じ)。台本の各鍵は書く前に
 * 読まれるので、贋物の側で write・format・umount が 0 回であることを見る
 * (行末を返す前に消す実装なら、ここで落ちる) ---- */
static const u8 *keys;
static int keys_len, keys_pos;
static int keys_serial;               /* 台本を serial_trygetchar から出す (kbd は -1) */
static int keys_gap;                  /* 各鍵の前に「入力なし」を何回返すか */
static int keys_gap_left;
static long keys_idle;                /* 尽きた後に読まれた回数 */
static int keys_hook_at;              /* この鍵 (0 始まり) を渡す直前に keys_hook を呼ぶ */
static void (*keys_hook)(void);

static void set_keys(const void *p, int len)
{
    keys = (const u8 *)p;
    keys_len = len;
    keys_pos = 0;
    keys_gap_left = keys_gap;
    keys_idle = 0;
}
#define KEYS(lit) set_keys((lit), (int)sizeof(lit) - 1)

static int __cdecl f_sys_open(const char *path, int mode)
{
    int i, fi = fx_find(path);
    if (mode & KAPI_O_CREAT) {
        CHECK(h_strncmp(path, "/hd0/", 5) == 0);       /* CD には書かない */
        if (inj_open_fail && h_strcmp(inj_open_fail, path) == 0) return -5;
        if (fi < 0) fi = fx_new(path);
        if (mode & KAPI_O_TRUNC) fx[fi].size = 0;
    } else if (fi < 0) {
        return -2;
    }
    if (fx[fi].is_dir) return -21;
    for (i = 0; i < FD_MAX; i++) {
        if (!fds[i].used) {
            fds[i].used = 1; fds[i].fi = fi; fds[i].pos = 0;
            fds[i].wr = (mode & (KAPI_O_WRONLY | KAPI_O_RDWR)) ? 1 : 0;
            return i + 3;
        }
    }
    return -24;
}
static void __cdecl f_sys_close(int fd) { if (fd >= 3 && fd < 3 + FD_MAX) fds[fd - 3].used = 0; }
static int __cdecl f_sys_read(int fd, void *buf, u32 n)
{
    Fx *f;
    u32 left;
    if (fd < 3 || fd >= 3 + FD_MAX || !fds[fd - 3].used) return -9;
    f = &fx[fds[fd - 3].fi];
    if (!f->data) return -5;
    left = f->size > fds[fd - 3].pos ? f->size - fds[fd - 3].pos : 0;
    if (n > left) n = left;
    memcpy(buf, f->data + fds[fd - 3].pos, n);
    fds[fd - 3].pos += n;
    return (int)n;
}
static int __cdecl f_sys_write(int fd, const void *buf, u32 n)
{
    (void)buf;
    if (fd < 3 || fd >= 3 + FD_MAX || !fds[fd - 3].used || !fds[fd - 3].wr) return -9;
    fds[fd - 3].pos += n;
    if (fds[fd - 3].pos > fx[fds[fd - 3].fi].size) fx[fds[fd - 3].fi].size = fds[fd - 3].pos;
    return (int)n;
}
static int __cdecl f_sys_lseek(int fd, int off, int whence)
{
    if (fd < 3 || fd >= 3 + FD_MAX || !fds[fd - 3].used) return -9;
    if (whence == SEEK_SET) fds[fd - 3].pos = (u32)off;
    else if (whence == SEEK_END) fds[fd - 3].pos = fx[fds[fd - 3].fi].size + (u32)off;
    else fds[fd - 3].pos += (u32)off;
    return (int)fds[fd - 3].pos;
}
static int __cdecl f_sys_stat(const char *path, OS32_Stat *st);
static int __cdecl f_sys_stat(const char *path, OS32_Stat *st)
{
    int fi = fx_find(path);
    if (fi < 0) return -2;
    memset(st, 0, sizeof(*st));
    st->st_size = fx[fi].vsize ? fx[fi].vsize : fx[fi].size;
    if (inj_stat_wrong && h_strcmp(inj_stat_wrong, path) == 0) st->st_size++;
    st->st_mode = (u16)(fx[fi].is_dir ? OS_S_IFDIR : OS_S_IFREG);
    return 0;
}
static int __cdecl f_sys_mkdir(const char *path)
{
    int fi;
    CHECK(h_strncmp(path, "/hd0", 4) == 0);
    fi = fx_find(path);
    if (fi >= 0) return -6;
    fi = fx_new(path);
    fx[fi].is_dir = 1;
    return 0;
}

/* ======================================================================== */
/*  hd0 (LBA 0〜31 の模型) と手順の記録                                      */
/* ======================================================================== */

#define DISK_SECTS 32
static u8 disk[DISK_SECTS][512];
static HddGeom geom;
static char ev[2048];
static u32 ev_len;
static int writes, fmt_calls;
static int first_write_mounts;          /* 最初の書き込みの時点の hd0 のマウント数 */
static u32 fmt_start, fmt_len;

static void ev_add(const char *e)
{
    while (*e && ev_len + 2 < sizeof(ev)) ev[ev_len++] = *e++;
    ev[ev_len++] = ' ';
    ev[ev_len] = '\0';
}
static void ev_lba(u32 lba)
{
    char b[8];
    b[0] = 'W';
    if (lba >= 10) { b[1] = (char)('0' + lba / 10); b[2] = (char)('0' + lba % 10); b[3] = '\0'; }
    else { b[1] = (char)('0' + lba); b[2] = '\0'; }
    ev_add(b);
}

static int __cdecl f_ide_write_sector(int drv, u32 lba, const void *buf)
{
    CHECK(drv == 0);
    CHECK(lba < DISK_SECTS);
    if (inj_write_fail_lba >= 0 && (u32)inj_write_fail_lba == lba) return -5;
    if (writes == 0) first_write_mounts = hd0_mounts;
    memcpy(disk[lba], buf, 512);
    writes++;
    ev_lba(lba);
    return 0;
}
static int __cdecl f_ide_read_sector(int drv, u32 lba, void *buf)
{
    CHECK(drv == 0);
    CHECK(lba < DISK_SECTS);
    if (inj_read_fail_lba >= 0 && (u32)inj_read_fail_lba == lba &&
        (!inj_read_fail_after_write || writes > 0))
        return -5;
    memcpy(buf, disk[lba], 512);
    if (inj_readback_bad >= 0 && (u32)inj_readback_bad == lba && writes > 0)
        ((u8 *)buf)[inj_readback_off] ^= 0x33;
    return 0;
}
static int __cdecl f_hdd_geom_info(int drv, HddGeom *out) { CHECK(drv == 0); *out = geom; return 0; }
static int __cdecl f_ext2_format_at(int drv, u32 start, u32 len)
{
    CHECK(drv == 0);
    fmt_calls++;
    fmt_start = start;
    fmt_len = len;
    ev_add("F");
    return inj_format_fail ? -5 : 0;
}
static int __cdecl f_sys_mount(const char *pre, const char *dev, const char *fs)
{
    (void)fs;
    if (h_strcmp(pre, "/cd0") == 0) return 0;
    CHECK(h_strcmp(pre, "/hd0") == 0 && h_strcmp(dev, "hd0") == 0);
    ev_add("M");
    if (inj_mount_fail) return -5;
    hd0_mounts++;
    hd0_at_hd0 = 1;
    return 0;
}
static int __cdecl f_sys_umount_checked(const char *pre)
{
    ev_add("U");
    if (inj_umount_sync && h_strcmp(pre, "/hd0") == 0 && hd0_at_hd0) umount_syncs++;
    if (inj_umount_fail) return -5;
    if (h_strcmp(pre, "/hd0") == 0 && hd0_at_hd0) {
        hd0_at_hd0 = 0;
        if (!inj_mount_stays) hd0_mounts--;
        return 0;
    }
    return -2;
}
static int __cdecl f_sys_is_mounted(const char *pre)
{ return (h_strcmp(pre, "/hd0") == 0 && (hd0_at_hd0 || hd0_other_dev)) ? 1 : 0; }
static int __cdecl f_dev_mount_count(int drv) { CHECK(drv == 0); return hd0_mounts; }
static const char *__cdecl f_vfs_devname(const char *pre)
{
    if (h_strcmp(pre, "/") == 0) return inj_root_hd0 ? "hd0" : "fd0";
    if (h_strcmp(pre, "/hd0") == 0 && hd0_at_hd0) return "hd0";
    if (h_strcmp(pre, "/hd0") == 0 && hd0_other_dev) return "hd1";
    return "";                              /* 実物 (fs/vfs.c) と同じく未マウントは "" */
}
static int __cdecl f_vfs_sync(void) { return inj_sync_fail ? -5 : 0; }
static int keys_next(void)
{
    int ch;
    /* どの鍵も書く前に読まれる (行末を返す前も後も write・format・umount は 0 回) */
    CHECK(writes == 0 && fmt_calls == 0 && !h_strstr(ev, "U"));
    if (keys_gap_left > 0) { keys_gap_left--; return -1; }
    keys_gap_left = keys_gap;
    if (keys_pos >= keys_len) {
        if (++keys_idle > 100000L) fail("key script exhausted (the installer asked more)", __LINE__);
        return -1;
    }
    if (keys_hook && keys_pos == keys_hook_at) keys_hook();
    keys_idle = 0;
    ch = keys[keys_pos++];
    return ch;
}
/* 台本を出さない側の -1 も数える (serial を読まない実装が kbd だけを回して
 * 止まらないように: 尽きた後と同じ上限で落とす) */
static int keys_none(void)
{
    if (++keys_idle > 100000L) fail("key script not read (the installer polls only one side)", __LINE__);
    return -1;
}
static int __cdecl f_kbd_trygetchar(void) { return keys_serial ? keys_none() : keys_next(); }
static int __cdecl f_serial_trygetchar(void) { return keys_serial ? keys_next() : keys_none(); }

/* ======================================================================== */
/*  実物                                                                     */
/* ======================================================================== */

/* cdinst.c の DBG を空にする (vfs_fd_path_host.c と同じ手) */
#include "rt/dbgserial.h"
#define main cdinst_main
#include "../../userland/system/cdinst.c"
#undef main
#include "../../userland/system/inst_hdd.c"
#include "../../userland/system/inst_disk.c"
#include "../../userland/lib/rt/pkg.c"
#include "../../userland/shell/hdprep_plan.c"
#include "../../drivers/pc98pt.c"
#include "../../fs/ext2_layout.c"

static KernelAPI g_api;

/* ======================================================================== */
/*  PKG を組む (無圧縮)                                                      */
/* ======================================================================== */

typedef struct { const char *path; u32 size; int dir; } PE;

static u8 pat(u32 i, u32 seed) { return (u8)((i * 13u + seed * 7u + 1u) & 0xFF); }

/* nodata = 1 なら表だけ (大きさを名乗るだけで中身を持たない。事前検査は
 * 表しか読まないので、300MB を名乗る項目も置ける) */
static void pkg_put(const char *cdpath, const PE *e, int n, int lzss, int nodata, u32 seed)
{
    u32 tbl = 0, data = 0, off, k;
    u8 *b;
    int i, fi;

    for (i = 0; i < n; i++) {
        tbl += 1u + strlen(e[i].path) + 5u;
        if (!e[i].dir) data += e[i].size;
    }
    tbl += 1u;
    b = (u8 *)arena_get(PKG_HEADER_SIZE + tbl + (nodata ? 0u : data));
    memset(b, 0, PKG_HEADER_SIZE);
    b[0] = 'P'; b[1] = 'K'; b[2] = 'G'; b[3] = '1';
    b[12] = 1;                                   /* version */
    b[13] = (u8)(lzss ? PKG_FLAG_LZSS : 0);
    b[14] = 64;                                  /* kapi_ver */
    b[16] = (u8)n;                               /* entry_count */
    b[18] = (u8)data; b[19] = (u8)(data >> 8); b[20] = (u8)(data >> 16); b[21] = (u8)(data >> 24);
    b[22] = b[18]; b[23] = b[19]; b[24] = b[20]; b[25] = b[21];
    off = PKG_HEADER_SIZE;
    for (i = 0; i < n; i++) {
        u32 pl = strlen(e[i].path);
        b[off++] = (u8)pl;
        memcpy(b + off, e[i].path, pl);
        off += pl;
        b[off++] = (u8)e[i].size; b[off++] = (u8)(e[i].size >> 8);
        b[off++] = (u8)(e[i].size >> 16); b[off++] = (u8)(e[i].size >> 24);
        /* dir: 0 = ファイル、1 = ディレクトリ、2 以上 = その値を型としてそのまま */
        b[off++] = (u8)(e[i].dir == 0 ? PKG_TYPE_FILE : e[i].dir == 1 ? PKG_TYPE_DIR : e[i].dir);
    }
    b[off++] = 0;
    if (!nodata) {
        for (i = 0; i < n; i++) {
            if (e[i].dir) continue;
            for (k = 0; k < e[i].size; k++) b[off + k] = pat(k, seed + (u32)i);
            off += e[i].size;
        }
    }
    fi = fx_find(cdpath);
    if (fi < 0) fi = fx_new(cdpath);
    fx[fi].data = b;
    fx[fi].size = off;
    fx[fi].vsize = nodata ? off + data : 0;
}

/* 最後に組んだ PKG のデータ部を n バイト切る / ヘッダの orig_size を 0 にする */
static void pkg_truncate(const char *cd, u32 n)
{
    int fi = fx_find(cd);
    CHECK(fi >= 0 && fx[fi].size > n);
    fx[fi].size -= n;
}
static void pkg_zero_orig(const char *cd)
{
    int fi = fx_find(cd);
    CHECK(fi >= 0);
    fx[fi].data[18] = fx[fi].data[19] = fx[fi].data[20] = fx[fi].data[21] = 0;
}

#define KERNEL_LEN 470000u
#define LOADER_LEN 8192u
#define IPL_LEN    512u

static u32 boot_ipl_len, boot_loader_len;

static void boot_pkg(u32 ipl_len, u32 loader_len, int with_ipl, int lzss)
{
    PE e[2];
    int n = 0;
    if (with_ipl) { e[n].path = "/boot/boot_hdd.bin"; e[n].size = ipl_len; e[n].dir = 0; n++; }
    e[n].path = "/boot/loader_hdd.bin"; e[n].size = loader_len; e[n].dir = 0; n++;
    pkg_put("/cd0/BOOT.PKG", e, n, lzss, 0, 100u);
    boot_ipl_len = ipl_len;
    boot_loader_len = loader_len;
}

static void minimal_pkg(u32 kernel_len, int with_kernel, int with_shell, int nodata)
{
    PE e[6];
    int n = 0;
    e[n].path = "/boot"; e[n].size = 0; e[n].dir = 1; n++;
    e[n].path = "/sys"; e[n].size = 0; e[n].dir = 1; n++;
    if (with_kernel) { e[n].path = "/boot/vmkernel.lz4"; e[n].size = kernel_len; e[n].dir = 0; n++; }
    if (with_shell) { e[n].path = "/sys/shell.bin"; e[n].size = 1000; e[n].dir = 0; n++; }
    e[n].path = "/bin/ls.bin"; e[n].size = 300; e[n].dir = 0; n++;
    pkg_put("/cd0/MINIMAL.PKG", e, n, 0, nodata, 200u);
}

static void small_pkg(const char *cd, const char *file, u32 size, int nodata)
{
    PE e[1];
    e[0].path = file; e[0].size = size; e[0].dir = 0;
    pkg_put(cd, e, 1, 0, nodata, 300u);
}

static void geom_817(void)
{
    memset(&geom, 0, sizeof(geom));
    geom.bios_queried = 1; geom.bios_valid = 1; geom.bios_heads = 8; geom.bios_spt = 17;
    geom.bios_cyl = 3011; geom.bios_seclen = 512;
    /* ドライブ自身は 16/63 を申告 (BIOS の 8/17 変換と違う)。区画表と IPL は
     * BIOS 幾何で書かれなければならない (F15) */
    geom.ata_def_cyl = 16383; geom.ata_def_heads = 16; geom.ata_def_spt = 63;
    geom.ata_cur_cyl = 16383; geom.ata_cur_heads = 16; geom.ata_cur_spt = 63;
    geom.ata_w49 = 0x0200; geom.ata_w53 = 1; geom.ata_total = 409600;
    geom.ata_present = 1; geom.addr_mode = HDD_AMODE_LBA28; geom.bios_da = 0x80;
}
static void geom_1663(void)
{
    geom_817();
    geom.bios_heads = 16; geom.bios_spt = 63; geom.bios_cyl = 16382;
    geom.ata_def_cyl = 16382; geom.ata_def_heads = 16; geom.ata_def_spt = 63;
    geom.ata_cur_cyl = 16382; geom.ata_cur_heads = 16; geom.ata_cur_spt = 63;
    geom.ata_total = 16514063;
}

static void setup(void)
{
    int i;
    for (i = 0; i < FX_MAX; i++) fx[i].used = 0;
    for (i = 0; i < FD_MAX; i++) fds[i].used = 0;
    arena_used = 0;
    cap_len = 0; cap[0] = '\0';
    ev_len = 0; ev[0] = '\0';
    memset(disk, 0, sizeof(disk));
    writes = fmt_calls = 0;
    first_write_mounts = -1;
    fmt_start = fmt_len = 0;
    inj_open_fail = 0;
    inj_stat_wrong = 0;
    inj_sync_fail = inj_mount_fail = inj_format_fail = 0;
    inj_readback_bad = -1;
    inj_readback_off = 3;
    inj_write_fail_lba = inj_read_fail_lba = -1;
    inj_read_fail_after_write = 0;
    hd0_mounts = hd0_at_hd0 = 0;
    hd0_other_dev = 0;
    inj_root_hd0 = inj_umount_fail = 0;
    inj_mount_stays = inj_umount_sync = umount_syncs = 0;
    keys_serial = keys_gap = 0;
    keys_hook = 0;
    keys_hook_at = -1;
    KEYS("1y");
    geom_817();
    boot_pkg(IPL_LEN, LOADER_LEN, 1, 0);
    minimal_pkg(KERNEL_LEN, 1, 1, 0);
    small_pkg("/cd0/GUI.PKG", "/bin/gshell.bin", 2000, 0);
    small_pkg("/cd0/NORMAL.PKG", "/usr/bin/edit.bin", 3000, 0);
    small_pkg("/cd0/DEBUG.PKG", "/usr/bin/t.bin", 100, 0);

    memset(&g_api, 0, sizeof(g_api));
    g_api.kprintf = f_kprintf;
    g_api.mem_alloc = f_mem_alloc;
    g_api.mem_free = f_mem_free;
    g_api.sys_open = f_sys_open;
    g_api.sys_close = f_sys_close;
    g_api.sys_read = f_sys_read;
    g_api.sys_write = f_sys_write;
    g_api.sys_lseek = f_sys_lseek;
    g_api.sys_stat = f_sys_stat;
    g_api.sys_mkdir = f_sys_mkdir;
    g_api.sys_mount = f_sys_mount;
    g_api.sys_umount_checked = f_sys_umount_checked;
    g_api.sys_is_mounted = f_sys_is_mounted;
    g_api.dev_mount_count = f_dev_mount_count;
    g_api.vfs_devname = f_vfs_devname;
    g_api.vfs_sync = f_vfs_sync;
    g_api.ide_read_sector = f_ide_read_sector;
    g_api.ide_write_sector = f_ide_write_sector;
    g_api.hdd_geom_info = f_hdd_geom_info;
    g_api.ext2_format_at = f_ext2_format_at;
    g_api.kbd_trygetchar = f_kbd_trygetchar;
    g_api.serial_trygetchar = f_serial_trygetchar;
}

static void run(void) { cdinst_main(1, (char **)0, &g_api); }

#define CHECK_NOTHING_WRITTEN() do { \
    CHECK(writes == 0); CHECK(fmt_calls == 0); CHECK(ev_len == 0); \
    CHECK_STR("Nothing was written"); CHECK_NOSTR("Installation Complete"); } while (0)

/* 区画表 (標準配置、項目 1 つ)・IPL の幾何・中身・ローダの中身・format の範囲 */
static void check_disk(u32 start, u32 len, u32 heads, u32 spt, u32 total)
{
    unsigned long st = 0, ln = 0;
    int idx = -1;
    u32 k;
    CHECK(pc98pt_count_used(disk[1]) == 1);
    CHECK(pc98pt_find_os32(disk[1], heads, spt, total, &idx, &st, &ln) == PC98PT_OK);
    CHECK(idx == 0 && st == start && ln == len);
    CHECK(disk[0][8] == heads && disk[0][9] == spt);
    CHECK(disk[0][510] == 0x55 && disk[0][511] == 0xAA);
    /* IPL = BOOT.PKG の 1 本目 (seed 100) の中身、[8]/[9] と 55AA 以外 */
    for (k = 0; k < 510; k++) {
        if (k == 8 || k == 9) continue;
        CHECK(disk[0][k] == (k < boot_ipl_len ? pat(k, 100u) : 0));
    }
    /* ローダ = 2 本目 (seed 101)、端数は 0 */
    for (k = 0; k < ((boot_loader_len + 511u) / 512u) * 512u; k++)
        CHECK(disk[2 + k / 512u][k % 512u] == (k < boot_loader_len ? pat(k, 101u) : 0));
    CHECK(fmt_calls == 1 && fmt_start == start && fmt_len == len);
}

static void check_order(const char *prefix)
{
    char want[256];
    u32 k, n = 0;
    const char *p;
    for (p = prefix; *p; p++) want[n++] = *p;
    for (p = "F W1 M "; *p; p++) want[n++] = *p;
    for (k = 2; k < 2u + (boot_loader_len + 511u) / 512u; k++) {
        want[n++] = 'W';
        if (k >= 10) want[n++] = (char)('0' + k / 10);
        want[n++] = (char)('0' + k % 10);
        want[n++] = ' ';
    }
    want[n++] = 'W'; want[n++] = '0'; want[n++] = ' '; want[n] = '\0';
    if (h_strcmp(ev, want) != 0) {
        report("order got: "); report(ev); report("\nwant:      "); report(want); report("\n");
        fail("order", __LINE__);
    }
}

static int hd0_size(const char *p)
{
    int fi = fx_find(p);
    return fi < 0 ? -1 : (int)fx[fi].size;
}

/* ---- 8/17 の NHD、Minimal ---- */
static void case_ok817(void)
{
    setup();
    run();
    check_disk(1632u, 407864u, 8, 17, 409600u);
    check_order("");
    CHECK(hd0_size("/hd0/boot/vmkernel.lz4") == (int)KERNEL_LEN);
    CHECK(hd0_size("/hd0/sys/shell.bin") == 1000);
    CHECK(hd0_size("/hd0/bin/gshell.bin") < 0);            /* Minimal は GUI を入れない */
    CHECK_STR("empty disk");
    CHECK_STR("Installation Complete");
    CHECK_NOSTR("INCOMPLETE");
}

/* ---- 16/63 (実機)、Full、ローダは 1 セクタの端数あり ---- */
static void case_ok1663(void)
{
    setup();
    geom_1663();
    boot_pkg(IPL_LEN - 12u, 5000u, 1, 0);
    KEYS("3y");
    run();
    check_disk(2016u, 524160u, 16, 63, 16514063u);
    check_order("");
    CHECK(hd0_size("/hd0/bin/gshell.bin") == 2000);
    CHECK(hd0_size("/hd0/usr/bin/t.bin") == 100);
    CHECK_STR("Installation Complete");
}

/* ---- モード: 旧配置 8/17 は作り直す (マウント中なら外してから)、他は断る ---- */
static void case_modes(void)
{
    setup();
    disk[1][0] = 0x80; disk[1][1] = 0xE2; disk[1][8] = 12;      /* 旧 cdinst の表 */
    disk[1][10] = 16; disk[1][11] = 7;
    disk[1][12] = (u8)((409600u / 136u - 1u) & 0xFF); disk[1][13] = (u8)((409600u / 136u - 1u) >> 8);
    memcpy(disk[1] + 16, "OS32            ", 16);
    disk[0][510] = 0x55; disk[0][511] = 0xAA;
    hd0_mounts = 1; hd0_at_hd0 = 1;
    run();
    CHECK_STR("old table layout");
    CHECK_STR("ALL FILES IN IT WILL BE LOST");
    check_disk(1632u, 407864u, 8, 17, 409600u);
    check_order("U ");
    CHECK_STR("Installation Complete");

    /* 未知の区画 */
    setup();
    disk[1][0] = 0x80; disk[1][1] = 0xA1; disk[1][10] = 12; disk[1][14] = 100;
    memcpy(disk[1] + 16, "MS-DOS          ", 16);
    KEYS("1\r\r");            /* 選択の行末は捨て、確認に Enter = 取り消し */
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("OS32 did not create");
    CHECK_STR("holds another system's partitions");
    CHECK_NOSTR("nhd-init");
    CHECK_STR("Current contents of hd0:");
    CHECK_STR("After y, type ERASE");
    CHECK_NOSTR("Type ERASE:");                     /* y でなければ ERASE は聞かない */

    /* 2 項目 */
    setup();
    disk[1][1] = 0xE2; disk[1][32 + 1] = 0xE2;
    KEYS("1\n\n");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("two or more");
    CHECK_STR("holds another system's partitions");
    CHECK_NOSTR("Type ERASE:");
}

/* ---- 事前検査の各失敗: 1 セクタも書かない ---- */
static void case_preflight(void)
{
    setup();                                        /* ローダ 8193 */
    boot_pkg(IPL_LEN, 8193u, 1, 0);
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("larger than 8192");

    setup();                                        /* IPL 513 */
    boot_pkg(513u, LOADER_LEN, 1, 0);
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("larger than 512");

    setup();                                        /* IPL が無い */
    boot_pkg(IPL_LEN, LOADER_LEN, 0, 0);
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("BOOT.PKG lacks boot_hdd.bin");

    setup();                                        /* BOOT.PKG が LZSS */
    boot_pkg(IPL_LEN, LOADER_LEN, 1, 1);
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("must not be LZSS");

    setup();                                        /* vmkernel 508KiB ちょうどは通る */
    minimal_pkg(508u * 1024u, 1, 1, 0);
    run();
    CHECK_STR("Installation Complete");
    CHECK(hd0_size("/hd0/boot/vmkernel.lz4") == 508 * 1024);

    setup();                                        /* vmkernel 508KiB + 1 */
    minimal_pkg(508u * 1024u + 1u, 1, 1, 1);
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("larger than 508 KiB");

    setup();                                        /* vmkernel が無い */
    minimal_pkg(KERNEL_LEN, 0, 1, 0);
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("leave /boot/vmkernel.lz4 missing or empty");

    setup();                                        /* shell が無い */
    minimal_pkg(KERNEL_LEN, 1, 0, 0);
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("leave /sys/shell.bin missing or empty");

    setup();                                        /* 容量: 13MB の区画に NORMAL が 30MB */
    geom.ata_total = 1632u + 136u * 200u;
    small_pkg("/cd0/NORMAL.PKG", "/usr/share/big.dat", 30u * 1024u * 1024u, 1);
    KEYS("2y");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("do not fit");

    setup();                                        /* Minimal なら NORMAL は数えない */
    geom.ata_total = 1632u + 136u * 200u;
    small_pkg("/cd0/NORMAL.PKG", "/usr/share/big.dat", 30u * 1024u * 1024u, 1);
    run();
    CHECK_STR("Installation Complete");

    setup();                                        /* 1 ファイルの上限 + 1 (P2-4) */
    small_pkg("/cd0/NORMAL.PKG", "/usr/share/big.dat", 67383297u, 1);
    KEYS("2y");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("larger than ext2 can hold");

    /* データ部が切れた媒体 (P1-1): 表とヘッダは正しく、中身が 10 バイト足りない */
    setup();
    small_pkg("/cd0/NORMAL.PKG", "/usr/bin/edit.bin", 3000, 0);
    pkg_truncate("/cd0/NORMAL.PKG", 10);
    KEYS("2y");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("truncated media");

    setup();                                        /* BOOT.PKG が切れている */
    pkg_truncate("/cd0/BOOT.PKG", 1);
    run();
    CHECK_NOTHING_WRITTEN();

    setup();                                        /* BOOT.PKG のヘッダが表と食い違う */
    pkg_zero_orig("/cd0/BOOT.PKG");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("header says 0");

    /* orig_size = 0 のヘッダ (P1-1): 以前は pkg_extract が何も書かずに OK を返し、
     * 空の /boot/vmkernel.lz4 で「完了」になり得た */
    setup();
    pkg_zero_orig("/cd0/MINIMAL.PKG");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("header says 0");

    setup();                                        /* 空の shell.bin (必須は空でないこと) */
    {
        PE e[3];
        e[0].path = "/boot/vmkernel.lz4"; e[0].size = KERNEL_LEN; e[0].dir = 0;
        e[1].path = "/sys/shell.bin"; e[1].size = 0; e[1].dir = 0;
        e[2].path = "/bin/ls.bin"; e[2].size = 300; e[2].dir = 0;
        pkg_put("/cd0/MINIMAL.PKG", e, 3, 0, 0, 200u);
    }
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("leave /sys/shell.bin missing or empty");

    setup();                                        /* 前置で溢れる項目 (展開の途中ではなく書く前に) */
    {
        static char longp[125];
        u32 k;
        longp[0] = '/';
        for (k = 1; k < 124u; k++) longp[k] = 'a';
        longp[124] = '\0';
        small_pkg("/cd0/NORMAL.PKG", longp, 10, 0);
    }
    KEYS("2y");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("PATH TOO LONG");

    setup();                                        /* 16/63 の幾何が無い */
    geom.bios_valid = 0;
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("no BIOS geometry");

    setup();                                        /* 断れば何も書かない */
    KEYS("1n");
    run();
    CHECK_NOTHING_WRITTEN();

    setup();                                        /* 確認への NUL は答え (= N)、読み捨てない */
    KEYS("1\0y");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("Installation cancelled");
    CHECK(keys_pos == 2);

    setup();                                        /* ルートが hd0 */
    inj_root_hd0 = 1;
    hd0_mounts = 1;
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("root file system");

    setup();                                        /* umount_checked の失敗 */
    hd0_mounts = 1; hd0_at_hd0 = 1;
    inj_umount_fail = 1;
    inj_umount_sync = 1;                            /* 失敗する前に sync が書いた */
    run();
    CHECK(writes == 0 && fmt_calls == 0);
    CHECK_STR("umount /hd0 failed");
    CHECK(umount_syncs == 1);                       /* umount の sync は書いた */
    CHECK_STR("Nothing was erased or formatted");
    CHECK_STR("Unmounting may have flushed");
    CHECK_NOSTR("Nothing was written");
    CHECK_NOSTR("INCOMPLETE");
    CHECK_NOSTR("Installation Complete");

    /* umount は成功したのにマウントが残る: umount の後の断り (sync はあり得る) */
    setup();
    hd0_mounts = 1; hd0_at_hd0 = 1;
    inj_mount_stays = 1;
    inj_umount_sync = 1;
    run();
    CHECK(writes == 0 && fmt_calls == 0);
    CHECK_STR("still mounted");
    CHECK(umount_syncs == 1);                       /* umount の sync は書いた */
    CHECK_STR("Nothing was erased or formatted");
    CHECK_STR("Unmounting may have flushed");
    CHECK_NOSTR("Nothing was written");
    CHECK_NOSTR("Installation Complete");

    setup();                                        /* 別の prefix にもマウント */
    hd0_mounts = 2; hd0_at_hd0 = 1;
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("mounted somewhere other than /hd0");
}

/* ---- /hd0 の外へ出るパス・深すぎるパス (P1-2 / P2-5): 1 バイトも書かない ---- */
static int any_outside(void)
{
    int i;
    for (i = 0; i < FX_MAX; i++) {
        if (!fx[i].used) continue;
        if (h_strncmp(fx[i].path, "/cd0/", 5) == 0) continue;
        if (h_strncmp(fx[i].path, "/hd0/", 5) != 0) return 1;
        if (h_strstr(fx[i].path, "/../") || h_strstr(fx[i].path, "/./")) return 1;
    }
    return 0;
}

static void deep_path(char *out, int parts)
{
    int i, n = 0;
    for (i = 0; i < parts; i++) { out[n++] = '/'; out[n++] = (char)('a' + i % 26); }
    out[n] = '\0';
}

static void case_paths(void)
{
    static const char *const bad[] = {
        "/../hd1/evil.bin", "/usr/../../hd1/x", "/usr/./x", "//x", "/x/", "x/y", "/.", "/.."
    };
    static char p31[80], p32[80];
    unsigned k;

    for (k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        setup();
        small_pkg("/cd0/NORMAL.PKG", bad[k], 10, 0);
        KEYS("2y");
        run();
        CHECK_NOTHING_WRITTEN();
        CHECK(!any_outside());
        CHECK_STR("a package path is not absolute");
    }
    /* MINIMAL の中の ".." も (必須の型の 1 本目) */
    setup();
    {
        PE e[4];
        e[0].path = "/boot/vmkernel.lz4"; e[0].size = KERNEL_LEN; e[0].dir = 0;
        e[1].path = "/sys/shell.bin"; e[1].size = 1000; e[1].dir = 0;
        e[2].path = "/sys/../../hd1/boot/vmkernel.lz4"; e[2].size = 5; e[2].dir = 0;
        e[3].path = "/bin/ls.bin"; e[3].size = 300; e[3].dir = 0;
        pkg_put("/cd0/MINIMAL.PKG", e, 4, 0, 0, 200u);
    }
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK(!any_outside());

    /* 深さ: 31 要素は "/hd0" と合わせて 32 = VFS の上限ちょうどで通る、32 要素は断る */
    deep_path(p31, 31);
    deep_path(p32, 32);
    setup();
    small_pkg("/cd0/NORMAL.PKG", p31, 10, 0);
    KEYS("2y");
    run();
    CHECK_STR("Installation Complete");
    CHECK(hd0_size("/hd0/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/a/b/c/d/e") == 10);
    setup();
    small_pkg("/cd0/NORMAL.PKG", p32, 10, 0);
    KEYS("2y");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("deeper than the VFS allows");
}

/* ---- 書いた後の失敗: INCOMPLETE、完了とは言わない ---- */
static void case_incomplete(void)
{
    setup();
    inj_format_fail = 1;
    run();
    CHECK(writes == 0);
    CHECK_STR("INCOMPLETE");
    CHECK_NOSTR("Installation Complete");

    setup();
    inj_readback_bad = 1;
    run();
    CHECK(writes == 1);
    CHECK(!h_strstr(ev, "M"));
    CHECK_STR("INCOMPLETE: partition table");
    CHECK_NOSTR("Installation Complete");

    setup();
    inj_mount_fail = 1;
    run();
    CHECK(writes == 1 && disk[0][510] == 0);
    CHECK_STR("INCOMPLETE: mount /hd0");
    CHECK_NOSTR("Installation Complete");

    setup();
    inj_readback_bad = 0;
    run();
    CHECK(hd0_size("/hd0/boot/vmkernel.lz4") < 0);
    CHECK_STR("INCOMPLETE: IPL");
    CHECK_NOSTR("Installation Complete");

    setup();                                        /* 追加パッケージ (NORMAL) の展開の失敗 */
    inj_open_fail = "/hd0/usr/bin/edit.bin";
    KEYS("2y");
    run();
    CHECK_STR("INCOMPLETE: package installation failed");
    CHECK_NOSTR("Installation Complete");

    setup();                                        /* sync の失敗 */
    inj_sync_fail = 1;
    run();
    CHECK_STR("INCOMPLETE");
    CHECK_STR("REBOOT, then run the installer again");
    CHECK_NOSTR("Installation Complete");

    /* ---- LBA ごとの write の失敗: その先は 1 セクタも書かない ---- */
    setup();                                        /* 区画表 (LBA 1) */
    inj_write_fail_lba = 1;
    run();
    CHECK(writes == 0 && h_strcmp(ev, "F ") == 0);
    CHECK_STR("INCOMPLETE: partition table write/readback failed (rc=-5)");
    CHECK_NOSTR("Installation Complete");
    setup();                                        /* ローダの 1 本目 (LBA 2) */
    inj_write_fail_lba = 2;
    run();
    CHECK(writes == 1 && h_strcmp(ev, "F W1 M ") == 0);
    CHECK_STR("INCOMPLETE: loader write/readback failed (rc=-5)");
    CHECK(disk[0][510] == 0);
    setup();                                        /* ローダの途中 (LBA 5) */
    inj_write_fail_lba = 5;
    run();
    CHECK(writes == 4 && h_strcmp(ev, "F W1 M W2 W3 W4 ") == 0);
    CHECK_STR("INCOMPLETE: loader write/readback failed (rc=-5)");
    CHECK(!h_strstr(ev, "W5 ") && !h_strstr(ev, "W0 "));
    setup();                                        /* ローダの最後 (LBA 17) */
    inj_write_fail_lba = 17;
    run();
    CHECK(writes == 16 && !h_strstr(ev, "W17 ") && !h_strstr(ev, "W0 "));
    CHECK_STR("INCOMPLETE: loader write/readback failed (rc=-5)");
    CHECK(disk[0][510] == 0);
    setup();                                        /* IPL (LBA 0) */
    inj_write_fail_lba = 0;
    run();
    CHECK(writes == 17 && disk[0][510] == 0);
    CHECK_STR("INCOMPLETE: IPL write/readback failed (rc=-5)");
    CHECK(hd0_size("/hd0/boot/vmkernel.lz4") < 0);
    CHECK_NOSTR("Installation Complete");

    /* ---- LBA ごとの read の失敗 (読み戻し): 同じく止まる ---- */
    setup();
    inj_read_fail_lba = 1; inj_read_fail_after_write = 1;
    run();
    CHECK(writes == 1 && !h_strstr(ev, "M"));
    CHECK_STR("INCOMPLETE: partition table write/readback failed (rc=-5)");
    setup();
    inj_read_fail_lba = 5; inj_read_fail_after_write = 1;
    run();
    CHECK(writes == 5 && !h_strstr(ev, "W6 ") && !h_strstr(ev, "W0 "));
    CHECK_STR("INCOMPLETE: loader write/readback failed (rc=-5)");
    setup();
    inj_read_fail_lba = 0; inj_read_fail_after_write = 1;
    run();
    CHECK(writes == 18);                            /* 区画表 + ローダ 16 + IPL */
    CHECK_STR("INCOMPLETE: IPL write/readback failed (rc=-5)");
    CHECK(hd0_size("/hd0/boot/vmkernel.lz4") < 0);
    /* 検査の read (LBA 0 / 1) の失敗: 何も書かない */
    setup();
    inj_read_fail_lba = 0;
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("cannot read LBA 0/1 of hd0 (rc=-5)");
    setup();
    inj_read_fail_lba = 1;
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("cannot read LBA 0/1 of hd0 (rc=-5)");

    /* ---- 読み戻しの 511 バイト目 (末尾) だけが違う: 区画表 / ローダ / IPL ---- */
    setup();
    inj_readback_bad = 1; inj_readback_off = 511;
    run();
    CHECK(writes == 1 && !h_strstr(ev, "M"));
    CHECK_STR("INCOMPLETE: partition table");
    setup();
    inj_readback_bad = 5; inj_readback_off = 511;
    run();
    CHECK(writes == 5 && !h_strstr(ev, "W0 "));
    CHECK_STR("INCOMPLETE: loader");
    setup();
    inj_readback_bad = 0; inj_readback_off = 511;
    run();
    CHECK(writes == 18);
    CHECK_STR("INCOMPLETE: IPL");
    CHECK(hd0_size("/hd0/boot/vmkernel.lz4") < 0);
    CHECK_NOSTR("Installation Complete");
}

/* ---- 必須のファイルは最終の状態で判定する (往復 2 P1-1 / P1-2) ---- */
static void minimal_with(const PE *extra, int n_extra)
{
    PE e[8];
    int n = 0, k;
    e[n].path = "/boot/vmkernel.lz4"; e[n].size = KERNEL_LEN; e[n].dir = 0; n++;
    e[n].path = "/sys/shell.bin"; e[n].size = 1000; e[n].dir = 0; n++;
    for (k = 0; k < n_extra; k++) e[n++] = extra[k];
    pkg_put("/cd0/MINIMAL.PKG", e, n, 0, 0, 200u);
}

static void case_final(void)
{
    PE x[2];

    /* 型 2 の /sys/shell.bin: 以前は必須として通り、展開では無視された */
    setup();
    {
        PE e[2];
        e[0].path = "/boot/vmkernel.lz4"; e[0].size = KERNEL_LEN; e[0].dir = 0;
        e[1].path = "/sys/shell.bin"; e[1].size = 1000; e[1].dir = 2;
        pkg_put("/cd0/MINIMAL.PKG", e, 2, 0, 0, 200u);
    }
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("unknown entry type 2");

    setup();                                        /* 必須でない項目の未知の型も断る */
    x[0].path = "/etc/odd"; x[0].size = 0; x[0].dir = 7;
    small_pkg("/cd0/NORMAL.PKG", "/usr/bin/edit.bin", 3000, 0);
    minimal_with(x, 1);
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("unknown entry type 7");

    /* 後の NORMAL の大きさ 0 の shell が MINIMAL の shell を空にする */
    setup();
    small_pkg("/cd0/NORMAL.PKG", "/sys/shell.bin", 0, 0);
    KEYS("2y");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("leave /sys/shell.bin missing or empty");
    /* Minimal なら NORMAL は展開しないので通る */
    setup();
    small_pkg("/cd0/NORMAL.PKG", "/sys/shell.bin", 0, 0);
    run();
    CHECK_STR("Installation Complete");
    CHECK(hd0_size("/hd0/sys/shell.bin") == 1000);

    /* 同じ PKG の中の重複 (後の方が勝つ) */
    setup();
    x[0].path = "/sys/shell.bin"; x[0].size = 0; x[0].dir = 0;
    minimal_with(x, 1);
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("leave /sys/shell.bin missing or empty");

    /* 後の PKG が vmkernel を上限を超える大きさで置き換える → 断る */
    setup();
    small_pkg("/cd0/NORMAL.PKG", "/boot/vmkernel.lz4", 508u * 1024u + 1u, 1);
    KEYS("2y");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("larger than 508 KiB");

    /* 正しい上書き (NORMAL の 500 バイトの shell) は最終の大きさで入る */
    setup();
    small_pkg("/cd0/NORMAL.PKG", "/sys/shell.bin", 500, 0);
    KEYS("2y");
    run();
    CHECK_STR("Installation Complete");
    CHECK(hd0_size("/hd0/sys/shell.bin") == 500);

    /* 展開の後に shell が事前検査の大きさでない → 完了と言わない */
    setup();
    inj_stat_wrong = "/hd0/sys/shell.bin";
    run();
    CHECK_STR("/hd0/sys/shell.bin is missing or has the wrong size");
    CHECK_STR("INCOMPLETE");
    CHECK_NOSTR("Installation Complete");
}

/* ======================================================================== */
/*  ERASE (N4 の例外): 表が使えないディスクは要約を出して「消した後の空の     */
/*  ディスク」として全検査 → 確認画面 → y → ERASE の打鍵 → 消去 → format   */
/* ======================================================================== */

#define BAD_FOREIGN 0
#define BAD_MULTI   1
#define BAD_MBRSIG  2
#define BAD_BROKEN  3
#define BAD_START   4
#define BAD_KINDS   5

static void put_entry(int idx, u8 mid, u8 sid, const char *name,
                      u32 scyl, u32 ecyl, u32 heads, u32 spt)
{
    u8 *e = disk[1] + idx * 32;
    u32 k;
    memset(e, 0, 32);
    e[0] = mid; e[1] = sid;
    e[10] = (u8)scyl; e[11] = (u8)(scyl >> 8);
    e[12] = (u8)(spt - 1u); e[13] = (u8)(heads - 1u);
    e[14] = (u8)ecyl; e[15] = (u8)(ecyl >> 8);
    for (k = 0; k < 16; k++) e[16 + k] = (u8)(*name ? *name++ : ' ');
}

/* 実機で見た形: 他の OS (MS-DOS) の区画と、その IPL (55AA) */
static void bad_disk(int kind)
{
    u32 h = geom.bios_heads, sp = geom.bios_spt;
    u32 k;
    for (k = 0; k < 510; k++) disk[0][k] = (u8)(0xEB ^ k);
    disk[0][510] = 0x55; disk[0][511] = 0xAA;
    switch (kind) {
    case BAD_FOREIGN:
        put_entry(0, 0xA0, 0xA1, "MS-DOS 6.20", 1, 1000, h, sp);
        break;
    case BAD_MULTI:
        put_entry(0, 0xA0, 0xA1, "MS-DOS 6.20", 1, 1000, h, sp);
        put_entry(1, 0x20, 0xA1, "DATA", 1001, 1500, h, sp);
        break;
    case BAD_MBRSIG:
        break;                                     /* 項目 0、LBA 0 に 55AA */
    case BAD_BROKEN:
        disk[1][0] = 0x80; disk[1][1] = 0xE2; disk[1][8] = 200;
        memcpy(disk[1] + 16, "OS32            ", 16);
        break;
    default:                                       /* OS32 の項目だが開始が違う */
        put_entry(0, 0x80, 0xE2, "OS32", 40, 100, h, sp);
        break;
    }
}

static const char *const bad_reason[BAD_KINDS] = {
    "OS32 did not create", "two or more", "LBA 0 ends with 55AA", "entry is broken",
    "does not start where"
};

typedef struct { const char *s; int len; } KeyStr;
#define KS(lit) { lit, (int)sizeof(lit) - 1 }

/* ERASE でない入力: 空行・小文字・末尾/先頭の空白・y・短い/長い・BS 入り・ESC・LF だけ・
 * 20 文字・NUL 入り (先頭・途中・末尾: 実物のドライバは受けた NUL を 0 で返す。
 * 0 を「入力なし」と読み捨てる実装は ERA<NUL>SE で消してしまう) */
static const KeyStr not_erase[] = {
    KS("\r"), KS("erase\r"), KS("ERASE \r"), KS(" ERASE\r"), KS("y\r"), KS("Erase\r"),
    KS("ERAS\r"), KS("ERASEE\r"), KS("ERA\bSE\r"), KS("ERASE\x1b"), KS("\n"),
    KS("ERASEERASEERASEERASE\r"), KS("ERA\0SE\r"), KS("\0ERASE\r"), KS("ERASE\0\r"),
    KS("ERASE\0\n"), KS("\0\r")
};
#define NOT_ERASE ((int)(sizeof(not_erase) / sizeof(not_erase[0])))
/* 通る行末: CR、LF、CRLF (LF は同じ行末の一部) */
static const KeyStr erase_ok[] = { KS("ERASE\r"), KS("ERASE\n"), KS("ERASE\r\n") };
#define ERASE_OK ((int)(sizeof(erase_ok) / sizeof(erase_ok[0])))

static u8 keybuf[64];
/* pre (NUL 終端) + 行 (長さ付き) を台本にする */
static void keys_line(const char *pre, const KeyStr *k)
{
    int n = 0, i;
    while (*pre) keybuf[n++] = (u8)*pre++;
    for (i = 0; i < k->len; i++) keybuf[n++] = (u8)k->s[i];
    set_keys(keybuf, n);
}

static int h_memeq(const u8 *a, const u8 *b, u32 n)
{
    u32 k;
    for (k = 0; k < n; k++) if (a[k] != b[k]) return 0;
    return 1;
}

static int disk_zero(u32 lba)
{
    u32 k;
    for (k = 0; k < 512; k++) if (disk[lba][k]) return 0;
    return 1;
}

/* a が b より前に出ている */
static void check_before(const char *a, const char *b, int line)
{
    const char *pa = h_strstr(cap, a), *pb = h_strstr(cap, b);
    if (!pa) fail("first marker missing", line);
    if (!pb) fail("second marker missing", line);
    if (pa > pb) fail("order of messages", line);
}

static void one_geom(int g) { if (g) geom_1663(); }

/* 表が使えないディスクの共通の表示: 理由 → 要約 → 確認画面 (y の後に ERASE) */
static void check_erase_screen(int kind)
{
    CHECK(h_strstr(cap, bad_reason[kind]) != 0);
    CHECK_STR("cannot be used as it is");
    CHECK_STR("Current contents of hd0:");
    CHECK_STR("Target: hd0 (IDE drive 0 = BIOS DA 80h), empty disk: create the OS32 area");
    CHECK_NOSTR("re-create");
    CHECK_STR("After y, type ERASE");
    CHECK_STR("EVERYTHING ON hd0 WILL BE LOST");
    CHECK_STR("Continue? [y/N]");
    check_before("Current contents of hd0:", "Continue? [y/N]", __LINE__);
    if (kind == BAD_FOREIGN || kind == BAD_MULTI || kind == BAD_MBRSIG) {
        CHECK_STR("holds another system's partitions");
        CHECK_NOSTR("nhd-init");
    } else {
        CHECK_STR("nhd-init");
    }
}

static void case_erase(void)
{
    static u8 keep[DISK_SECTS][512];
    int g, kind, i;

    for (g = 0; g < 2; g++) {
        for (kind = 0; kind < BAD_KINDS; kind++) {
            for (i = 0; i < NOT_ERASE; i++) {
                setup();
                one_geom(g);
                bad_disk(kind);
                memcpy(keep, disk, sizeof(keep));
                keys_serial = i & 1;              /* kbd と serial を交互に */
                keys_gap = i % 3;                 /* 鍵の間に「入力なし」 */
                /* y の行末を付ける (y の後の最初の行末は y のものとして読み捨てる) */
                keys_line(((i >> 1) & 1) ? "1y\n" : "1y\r\n", &not_erase[i]);
                run();
                CHECK_NOTHING_WRITTEN();
                CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
                check_erase_screen(kind);
                CHECK_STR("Type ERASE:");
                CHECK_STR("Not erased. Nothing was written.");
                check_before("Continue? [y/N]", "Type ERASE:", __LINE__);
                CHECK_NOSTR("INCOMPLETE");
                CHECK_NOSTR("erased and verified");
                CHECK_NOSTR("Installing OS32");
                CHECK(keys_pos == keys_len);       /* 行の終わりまで読んで、それ以上は聞かない */
            }
            /* ERASE → (y の後で) LBA 0/1 を 0 → 空のディスクとして最後まで */
            for (i = 0; i < ERASE_OK; i++) {
                setup();
                one_geom(g);
                bad_disk(kind);
                keys_serial = i & 1;
                keys_line("1y", &erase_ok[i]);
                run();
                check_erase_screen(kind);
                CHECK_STR("Type ERASE:");
                CHECK_STR("erased and verified (all zero)");
                CHECK_STR("Installation Complete");
                CHECK_NOSTR("INCOMPLETE");
                CHECK_NOSTR("Not erased");
                /* 順序: 確認 → ERASE の問い → 消去 → format */
                check_before("Continue? [y/N]", "Type ERASE:", __LINE__);
                check_before("Type ERASE:", "erased and verified", __LINE__);
                check_before("erased and verified", "Formatting ext2", __LINE__);
                check_order("W0 W1 ");
                CHECK(keys_pos == keys_len);       /* CRLF の LF も同じ行末として読む */
                if (g) check_disk(2016u, 524160u, 16, 63, 16514063u);
                else   check_disk(1632u, 407864u, 8, 17, 409600u);
            }
        }
    }

    /* y の後の行末 (Codex 2 回目 P2): 端末が y と一緒に送る CR / LF / CRLF、または
     * 後から押した Enter は y の行末として 1 回だけ読み捨てる。ERASE + 行末で消去まで
     * 進む (kbd / serial、鍵の間の「入力なし」0〜2 = もう届いている / 後から届く) */
    {
        static const char *const yeol[] = { "1y\r\n", "1y\r", "1y\n", "1y" };
        /* y の行末の後に Enter だけ = ERASE の空行 → 取り消し */
        static const KeyStr ycancel[] = {
            KS("1y\r\n\r"), KS("1y\r\r"), KS("1y\n\r"), KS("1y\n\n"),
            KS("1y\r\n\n"), KS("1y\r\n\r\n"), KS("1y\r\r\n")
        };
        int ser, gap, y, e;
        for (ser = 0; ser < 2; ser++) {
            for (gap = 0; gap < 3; gap++) {
                for (y = 0; y < 4; y++) {
                    for (e = 0; e < ERASE_OK; e++) {
                        setup();
                        bad_disk(BAD_FOREIGN);
                        keys_serial = ser;
                        keys_gap = gap;
                        keys_line(yeol[y], &erase_ok[e]);
                        run();
                        CHECK_STR("Type ERASE:");
                        CHECK_STR("erased and verified (all zero)");
                        CHECK_STR("Installation Complete");
                        CHECK_NOSTR("Not erased");
                        CHECK(keys_pos == keys_len || (gap > 0 && keys_pos == keys_len - 1 &&
                                                       keybuf[keys_len - 1] == '\n'));
                    }
                }
                for (y = 0; y < (int)(sizeof(ycancel) / sizeof(ycancel[0])); y++) {
                    setup();
                    bad_disk(BAD_FOREIGN);
                    memcpy(keep, disk, sizeof(keep));
                    keys_serial = ser;
                    keys_gap = gap;
                    keys_line("", &ycancel[y]);
                    run();
                    CHECK_NOTHING_WRITTEN();
                    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
                    CHECK_STR("Not erased. Nothing was written.");
                    CHECK_NOSTR("erased and verified");
                    CHECK_NOSTR("Installing OS32");
                    /* 2 つめの行末で止まり、それ以上は聞かない (末尾の CRLF の LF は、
                     * 後から届くなら読まれずに残る) */
                    CHECK(keys_pos == keys_len || (gap > 0 && keys_pos == keys_len - 1 &&
                                                   keybuf[keys_len - 1] == '\n'));
                }
            }
        }
    }

    /* [0-3] の選択の後の行末 (Codex 3 回目 P2): 「1 + Enter」の Enter は選択の行末と
     * して 1 回だけ捨てる (Continue の取り消しにしない)。1\r\n → y\r\n → ERASE\r\n で
     * 消去まで進み、選択の行末の後に Enter だけなら取り消し。NUL・ESC・n は捨てずに
     * 答え (= 取り消し)。kbd / serial × 鍵の間の「入力なし」0〜2 */
    {
        static const char *const ceol[] = { "1\r\n", "1\r", "1\n", "1" };
        static const char *const yeol2[] = { "y\r\n", "y\n", "y" };
        static const KeyStr ccancel[] = {
            KS("1\r\n\r"), KS("1\r\r"), KS("1\n\n"), KS("1\r\n\n"), KS("1\n\r"),
            KS("1\r\n\r\n"), KS("1\r\r\n"), KS("1\r\nn"), KS("1\r\n\x1b"), KS("1\r\n\0"),
            KS("1\0"), KS("1\x1b")
        };
        static char pre[16];
        int ser, gap, c, y, e, n;
        const char *q;
        for (ser = 0; ser < 2; ser++) {
            for (gap = 0; gap < 3; gap++) {
                for (c = 0; c < 4; c++) {
                    for (y = 0; y < 3; y++) {
                        for (e = 0; e < ERASE_OK; e++) {
                            n = 0;
                            for (q = ceol[c]; *q; q++) pre[n++] = *q;
                            for (q = yeol2[y]; *q; q++) pre[n++] = *q;
                            pre[n] = '\0';
                            setup();
                            bad_disk(BAD_FOREIGN);
                            keys_serial = ser;
                            keys_gap = gap;
                            keys_line(pre, &erase_ok[e]);
                            run();
                            CHECK_STR("Type ERASE:");
                            CHECK_STR("erased and verified (all zero)");
                            CHECK_STR("Installation Complete");
                            CHECK_NOSTR("Installation cancelled");
                            CHECK_NOSTR("Not erased");
                            CHECK(keys_pos == keys_len || (gap > 0 && keys_pos == keys_len - 1 &&
                                                           keybuf[keys_len - 1] == '\n'));
                        }
                    }
                }
                for (y = 0; y < (int)(sizeof(ccancel) / sizeof(ccancel[0])); y++) {
                    setup();
                    bad_disk(BAD_FOREIGN);
                    memcpy(keep, disk, sizeof(keep));
                    keys_serial = ser;
                    keys_gap = gap;
                    keys_line("", &ccancel[y]);
                    run();
                    CHECK_NOTHING_WRITTEN();
                    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
                    CHECK_STR("Installation cancelled. Nothing was written.");
                    CHECK_NOSTR("Type ERASE:");
                    /* 取り消しの CR の後の LF は読まずに終わってよい (もう聞かない) */
                    CHECK(keys_pos == keys_len || (keys_pos == keys_len - 1 &&
                                                   keybuf[keys_len - 1] == '\n'));
                }
                /* 空のディスク (ERASE を聞かない) でも「1 + Enter → y + Enter」で入る */
                setup();
                keys_serial = ser;
                keys_gap = gap;
                KEYS("1\r\ny\r\n");
                run();
                CHECK_STR("Installation Complete");
                CHECK_NOSTR("Type ERASE:");
            }
        }
    }

    /* 鍵の間に「入力なし」が挟まっても (シリアルの到着の間) ERASE は通る */
    setup();
    bad_disk(BAD_FOREIGN);
    keys_gap = 3;
    KEYS("1yERASE\r");
    run();
    CHECK_STR("erased and verified (all zero)");
    CHECK_STR("Installation Complete");

    /* 要約: 55AA、項目の数、mid・sid・名前・開始と終了のシリンダ、LBA (16/63) */
    setup();
    geom_1663();
    bad_disk(BAD_FOREIGN);
    KEYS("1n");
    run();
    CHECK_STR("first bytes eb ea e9 e8, boot signature 55AA: yes");
    CHECK_STR("LBA 1 (partition table): 1 entry");
    CHECK_STR("#0 mid a0 sid a1 name \"MS-DOS 6.20     \"");
    CHECK_STR("cyl 1..1000 (start C/H/S 1/0/0, end C/H/S 1000/15/62)");
    CHECK_STR("LBA 1008, 1008000 sectors (492 MB)");
    CHECK_STR("code -40");
    setup();
    bad_disk(BAD_MULTI);
    KEYS("1n");
    run();
    CHECK_STR("2 entries");
    CHECK_STR("#1 mid 20 sid a1 name \"DATA            \"");
    CHECK_STR("cyl 1001..1500");
    setup();
    bad_disk(BAD_MBRSIG);
    KEYS("1n");
    run();
    CHECK_STR("boot signature 55AA: yes");
    CHECK_STR("0 entries");
    setup();
    bad_disk(BAD_BROKEN);
    KEYS("1n");
    run();
    CHECK_STR("sid e2 name \"OS32            \"");
    CHECK_STR("not a valid range at 8 heads x 17 sectors");

    /* 空のディスク・作り直しでは ERASE を聞かない */
    setup();
    run();
    CHECK_NOSTR("Type ERASE");
    CHECK_NOSTR("After y");
    CHECK_STR("Installation Complete");
    setup();
    disk[1][0] = 0x80; disk[1][1] = 0xE2; disk[1][8] = 12;      /* 旧 cdinst の表 */
    disk[1][10] = 16; disk[1][11] = 7;
    disk[1][12] = (u8)((409600u / 136u - 1u) & 0xFF); disk[1][13] = (u8)((409600u / 136u - 1u) >> 8);
    memcpy(disk[1] + 16, "OS32            ", 16);
    disk[0][510] = 0x55; disk[0][511] = 0xAA;
    run();
    CHECK_NOSTR("Type ERASE");
    CHECK_STR("Installation Complete");
}

/* 消す前に止まる (N・大きさ・容量・ERASE でない) と、消した後の失敗の案内 */
static void case_erase_fail(void)
{
    static u8 keep[DISK_SECTS][512];
    int off;

    /* y/N で N → まだ何も消していない (ERASE も聞かない) */
    setup();
    geom_1663();
    bad_disk(BAD_FOREIGN);
    memcpy(keep, disk, sizeof(keep));
    KEYS("1n");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
    check_erase_screen(BAD_FOREIGN);
    CHECK_STR("Installation cancelled. Nothing was written.");
    CHECK_NOSTR("Type ERASE:");
    CHECK_NOSTR("INCOMPLETE");
    CHECK_NOSTR("ERASED");

    /* 大きさの検査で断る (IPL 513 B) → 消す前 (確認も ERASE も無い) */
    setup();
    bad_disk(BAD_FOREIGN);
    memcpy(keep, disk, sizeof(keep));
    boot_pkg(513u, LOADER_LEN, 1, 0);
    KEYS("1yERASE\r");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
    CHECK_STR("larger than 512");
    CHECK_STR("Current contents of hd0:");
    CHECK_NOSTR("Continue? [y/N]");
    CHECK_NOSTR("Type ERASE:");
    CHECK_NOSTR("INCOMPLETE");

    /* 容量の検査で断る (13MB の区画に NORMAL が 30MB) → 消す前 */
    setup();
    bad_disk(BAD_MULTI);
    memcpy(keep, disk, sizeof(keep));
    geom.ata_total = 1632u + 136u * 200u;
    small_pkg("/cd0/NORMAL.PKG", "/usr/share/big.dat", 30u * 1024u * 1024u, 1);
    KEYS("2yERASE\r");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
    CHECK_STR("do not fit");
    CHECK_NOSTR("Continue? [y/N]");
    CHECK_NOSTR("Type ERASE:");

    /* format の失敗 (消した後、区画表はまだ 0) → 空のディスクとして入れ直せる */
    setup();
    bad_disk(BAD_MULTI);
    inj_format_fail = 1;
    KEYS("1yERASE\r");
    run();
    CHECK(writes == 2 && disk_zero(0) && disk_zero(1));
    CHECK_STR("erased and verified");
    CHECK_STR("INCOMPLETE: ext2_format_at failed");
    CHECK_STR("REBOOT");
    CHECK_STR("partition table was ERASED");
    CHECK_STR("installs onto hd0 as an empty disk");
    CHECK_NOSTR("Installation Complete");
    memcpy(keep, disk, sizeof(keep));
    setup();                                        /* 次の実行は聞かずに空のディスク */
    memcpy(disk, keep, sizeof(keep));
    KEYS("1y");
    run();
    CHECK_NOSTR("Type ERASE");
    CHECK_STR("empty disk");
    CHECK_STR("Installation Complete");
    check_disk(1632u, 407864u, 8, 17, 409600u);
    /* 完了した後 (区画表を書いた後) の同じ起動でもう一度: 消して format が失敗 →
     * 案内は今回の消去で決まる (前の実行の「区画表を書いた」を持ち越さない) */
    setup();
    bad_disk(BAD_MULTI);
    inj_format_fail = 1;
    KEYS("1yERASE\r");
    run();
    CHECK_STR("INCOMPLETE: ext2_format_at failed");
    CHECK_STR("partition table was ERASED");
    /* 消した実行の後の、消さない実行の format の失敗 → 「空のディスク」の案内は出ない */
    setup();
    inj_format_fail = 1;
    run();
    CHECK_STR("INCOMPLETE: ext2_format_at failed");
    CHECK_NOSTR("was ERASED");

    /* 消す書き込みの読み戻しが違う (LBA 1 / LBA 0、先頭側と 511 バイト目) → format に進まない */
    for (off = 3; off <= 511; off += 508) {
        setup();
        bad_disk(BAD_FOREIGN);
        inj_readback_bad = 1;
        inj_readback_off = off;
        KEYS("1yERASE\r");
        run();
        CHECK(writes == 2 && fmt_calls == 0);
        CHECK_STR("INCOMPLETE: erasing LBA 0 and 1 of hd0 failed");
        CHECK_STR("type ERASE again");
        CHECK_NOSTR("Installation Complete");
        setup();
        bad_disk(BAD_FOREIGN);
        inj_readback_bad = 0;
        inj_readback_off = off;
        KEYS("1yERASE\r");
        run();
        CHECK(writes == 1 && fmt_calls == 0);
        CHECK_STR("INCOMPLETE: erasing LBA 0 and 1 of hd0 failed");
    }
    /* 消す書き込みの失敗 (LBA 0 / LBA 1) */
    setup();
    bad_disk(BAD_FOREIGN);
    inj_write_fail_lba = 0;
    KEYS("1yERASE\r");
    run();
    CHECK(writes == 0 && fmt_calls == 0);
    CHECK_STR("INCOMPLETE: erasing LBA 0 and 1 of hd0 failed (rc=-5)");
    setup();
    bad_disk(BAD_FOREIGN);
    inj_write_fail_lba = 1;
    KEYS("1yERASE\r");
    run();
    CHECK(writes == 1 && fmt_calls == 0 && disk_zero(0) && !disk_zero(1));
    CHECK_STR("INCOMPLETE: erasing LBA 0 and 1 of hd0 failed (rc=-5)");

    /* 区画表を書いた後の失敗 (マウント・sync) は「空のディスク」と言わない
     * (次の実行は OS32 の区域の作り直し) */
    setup();
    bad_disk(BAD_FOREIGN);
    inj_mount_fail = 1;
    KEYS("1yERASE\r");
    run();
    CHECK_STR("INCOMPLETE: mount /hd0");
    CHECK_NOSTR("was ERASED");
    setup();
    bad_disk(BAD_FOREIGN);
    inj_sync_fail = 1;
    KEYS("1yERASE\r");
    run();
    CHECK_STR("INCOMPLETE");
    CHECK_NOSTR("was ERASED");
    CHECK_NOSTR("Installation Complete");
}

/* ERASE の入力中にマウントが変わる贋の手 */
static void hook_mount_other(void) { hd0_mounts = 1; hd0_at_hd0 = 0; }
static void hook_mount_hd0(void)   { hd0_mounts = 1; hd0_at_hd0 = 1; }
static void hook_swap_hd0(void)    { hd0_at_hd0 = 0; hd0_other_dev = 1; }
static void hook_gone_hd0(void)    { hd0_at_hd0 = 0; hd0_mounts = 0; }

/* マウント中は消さない (今までのマウントの検査を先に通す) */
static void case_erase_mount(void)
{
    static u8 keep[DISK_SECTS][512];

    /* ルートが hd0: 確認も ERASE も無しに断る */
    setup();
    bad_disk(BAD_FOREIGN);
    inj_root_hd0 = 1;
    hd0_mounts = 1;
    KEYS("1yERASE\r");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("root file system");
    CHECK_NOSTR("Continue? [y/N]");
    CHECK_NOSTR("Type ERASE");

    /* 別の prefix (だけ / にも) */
    setup();
    bad_disk(BAD_MULTI);
    hd0_mounts = 1; hd0_at_hd0 = 0;
    KEYS("1yERASE\r");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_STR("mounted somewhere other than /hd0");
    CHECK_NOSTR("Type ERASE");
    setup();
    bad_disk(BAD_MULTI);
    hd0_mounts = 2; hd0_at_hd0 = 1;
    KEYS("1yERASE\r");
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK_NOSTR("Type ERASE");

    /* /hd0 の umount が失敗 → 消さない */
    setup();
    bad_disk(BAD_MULTI);
    hd0_mounts = 1; hd0_at_hd0 = 1;
    memcpy(keep, disk, sizeof(keep));
    KEYS("1yERASE\r");
    inj_umount_fail = 1;
    inj_umount_sync = 1;
    run();
    CHECK(writes == 0 && fmt_calls == 0);
    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
    CHECK_STR("Type ERASE:");
    CHECK_STR("umount /hd0 failed");
    CHECK(umount_syncs == 1);                       /* umount の sync は書いた */
    CHECK_STR("Nothing was erased or formatted");
    CHECK_STR("Unmounting may have flushed");
    CHECK_NOSTR("Nothing was written");
    CHECK_NOSTR("INCOMPLETE");
    CHECK_NOSTR("erased and verified");
    /* umount の後もマウントが残る → 消さない (sync はあり得る) */
    setup();
    bad_disk(BAD_MULTI);
    hd0_mounts = 1; hd0_at_hd0 = 1;
    memcpy(keep, disk, sizeof(keep));
    KEYS("1yERASE\r");
    inj_mount_stays = 1;
    inj_umount_sync = 1;
    run();
    CHECK(writes == 0 && fmt_calls == 0);
    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
    CHECK_STR("still mounted");
    CHECK(umount_syncs == 1);                       /* umount の sync は書いた */
    CHECK_STR("Nothing was erased or formatted");
    CHECK_STR("Unmounting may have flushed");
    CHECK_NOSTR("Nothing was written");
    CHECK_NOSTR("erased and verified");

    /* /hd0 にだけマウント → 確認画面に「外す」、ERASE の後で外してから消し、そのまま入れる */
    setup();
    geom_1663();
    bad_disk(BAD_MULTI);
    hd0_mounts = 1; hd0_at_hd0 = 1;
    KEYS("1yERASE\r");
    run();
    CHECK(first_write_mounts == 0);
    check_order("U W0 W1 ");
    check_disk(2016u, 524160u, 16, 63, 16514063u);
    CHECK_STR("will be unmounted first");
    CHECK_STR("Installation Complete");

    /* ERASE の入力中に別の prefix / /hd0 にマウントされる → 消さない (何も書かない) */
    setup();
    bad_disk(BAD_FOREIGN);
    memcpy(keep, disk, sizeof(keep));
    KEYS("1yERASE\r");
    keys_hook = hook_mount_other; keys_hook_at = 4;      /* "ERASE" の 'A' の前 */
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
    CHECK_STR("Type ERASE:");
    CHECK_STR("still mounted");
    CHECK_NOSTR("erased and verified");
    setup();
    bad_disk(BAD_FOREIGN);
    memcpy(keep, disk, sizeof(keep));
    KEYS("1yERASE\r");
    keys_hook = hook_mount_hd0; keys_hook_at = 7;        /* CR の前 */
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
    CHECK_STR("still mounted");
    CHECK_NOSTR("erased and verified");

    /* ERASE の入力中に /hd0 の相手が差し替わる (hd1 になる / 外れる) → 消さない */
    setup();
    bad_disk(BAD_FOREIGN);
    hd0_mounts = 1; hd0_at_hd0 = 1;
    memcpy(keep, disk, sizeof(keep));
    KEYS("1yERASE\r");
    keys_hook = hook_swap_hd0; keys_hook_at = 3;
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
    CHECK_STR("/hd0 no longer holds hd0");
    CHECK_NOSTR("Nothing was erased or formatted");   /* umount の前: 何も書いていない */
    CHECK_NOSTR("erased and verified");
    setup();
    bad_disk(BAD_FOREIGN);
    hd0_mounts = 1; hd0_at_hd0 = 1;
    memcpy(keep, disk, sizeof(keep));
    KEYS("1yERASE\r");
    keys_hook = hook_gone_hd0; keys_hook_at = 7;
    run();
    CHECK_NOTHING_WRITTEN();
    CHECK(h_memeq(&keep[0][0], &disk[0][0], sizeof(keep)));
    CHECK_STR("/hd0 no longer holds hd0");
    CHECK_NOSTR("Nothing was erased or formatted");   /* umount の前: 何も書いていない */
    CHECK_NOSTR("erased and verified");
}

/* ======================================================================== */
/*  鍵の読み方 (inst_hdd_getkey / ih_read_line を直に): 0 以上はすべて入力、  */
/*  -1 は待つ、CR の直後の LF は捨てる (すぐ届いても後から届いても)          */
/* ======================================================================== */

static void keys_reset(void) { ih_last_cr = 0; ih_pushed = -1; }

static void case_keys(void)
{
    static char line[INST_LINE_MAX];
    int g;

    setup();
    keys_reset();
    KEYS("\r\n\nx");                                /* CRLF → CR、次の LF は生きている */
    CHECK(inst_hdd_getkey(&g_api) == '\r');
    CHECK(inst_hdd_getkey(&g_api) == '\n');
    CHECK(inst_hdd_getkey(&g_api) == 'x');
    CHECK(keys_pos == keys_len);

    keys_reset();
    KEYS("\n\r\r\n");                               /* LF CR CR LF → LF CR CR */
    CHECK(inst_hdd_getkey(&g_api) == '\n');
    CHECK(inst_hdd_getkey(&g_api) == '\r');
    CHECK(inst_hdd_getkey(&g_api) == '\r');
    keys_gap = 0;
    CHECK(keys_pos == keys_len - 1);

    keys_reset();
    KEYS("\0y\0");                                  /* NUL は 0 として届く */
    CHECK(inst_hdd_getkey(&g_api) == 0);
    CHECK(inst_hdd_getkey(&g_api) == 'y');
    CHECK(inst_hdd_getkey(&g_api) == 0);

    keys_reset();
    keys_gap = 2;                                   /* 後から届く LF も捨てる */
    KEYS("\r\nx");
    CHECK(inst_hdd_getkey(&g_api) == '\r');
    CHECK(inst_hdd_getkey(&g_api) == 'x');

    keys_reset();
    keys_gap = 0;
    keys_serial = 1;                                /* serial からも同じ */
    KEYS("a\0\r\nb");
    CHECK(inst_hdd_getkey(&g_api) == 'a');
    CHECK(inst_hdd_getkey(&g_api) == 0);
    CHECK(inst_hdd_getkey(&g_api) == '\r');
    CHECK(inst_hdd_getkey(&g_api) == 'b');
    keys_serial = 0;

    /* 1 行読み: CRLF は 1 つの行末、CR の後の別の字は次の読みに戻す、NUL 入りは不一致 */
    keys_reset();
    KEYS("ERASE\r\n");
    CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 0) == 0 && h_strcmp(line, "ERASE") == 0);
    CHECK(keys_pos == keys_len);
    keys_reset();
    KEYS("ERASE\rq");
    CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 0) == 0 && h_strcmp(line, "ERASE") == 0);
    CHECK(inst_hdd_getkey(&g_api) == 'q');
    keys_reset();
    KEYS("ERASE\n");
    CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 0) == 0 && h_strcmp(line, "ERASE") == 0);
    keys_reset();
    KEYS("ERA\0SE\r");
    CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 0) == -1);
    CHECK(keys_pos == keys_len);
    keys_reset();
    keys_gap = 1;
    KEYS("E\0RASE\n");
    CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 0) == -1);
    keys_gap = 0;
    keys_reset();
    KEYS("ERASE\x1b" "y");                          /* ESC はその場で打ち切り、y は残る */
    CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 0) == -1);
    CHECK(inst_hdd_getkey(&g_api) == 'y');

    /* skip_eol (y の後の ERASE の行): 最初の行末 1 つだけ映さずに読み捨てる。
     * もう届いていても後から届いても同じ。行末以外 (NUL・ESC・字) は捨てない */
    for (g = 0; g < 3; g++) {
        keys_gap = g;
        keys_reset();
        KEYS("\r\nERASE\r");
        CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == 0 && h_strcmp(line, "ERASE") == 0);
        keys_reset();
        KEYS("\rERASE\r");
        CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == 0 && h_strcmp(line, "ERASE") == 0);
        keys_reset();
        KEYS("\nERASE\n");
        CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == 0 && h_strcmp(line, "ERASE") == 0);
        keys_reset();
        KEYS("ERASE\r");
        CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == 0 && h_strcmp(line, "ERASE") == 0);
        keys_reset();
        KEYS("\r\r");                               /* 2 つめの行末は空行 */
        CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == 0 && line[0] == '\0');
        CHECK(keys_pos == keys_len);
        keys_reset();
        KEYS("\r\n\n");                             /* CRLF の後の LF は空行 */
        CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == 0 && line[0] == '\0');
        CHECK(keys_pos == keys_len);
        keys_reset();
        KEYS("\0\rERASE\r");                          /* NUL は入力: 捨てず、その行は不一致 */
        CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == -1);
        CHECK(inst_hdd_getkey(&g_api) == 'E');      /* NUL の行で終わり、ERASE は次 */
        keys_reset();
        KEYS("E\rERASE\r");                           /* 最初の 1 字だけ: 後の行末は捨てない */
        CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == 0 && h_strcmp(line, "E") == 0);
        keys_reset();
        KEYS("\x1b" "ERASE\r");                       /* ESC は捨てずに打ち切り */
        CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == -1);
        CHECK(keys_pos == 1);
    }
    keys_gap = 0;
    /* 行末の読み捨ては映さない (y の後に余計な改行を出さない) */
    keys_reset();
    cap_len = 0; cap[0] = '\0';
    KEYS("\r\nAB\r");
    CHECK(ih_read_line(&g_api, line, INST_LINE_MAX, 1) == 0 && h_strcmp(line, "AB") == 0);
    CHECK(h_strcmp(cap, "AB\n") == 0);
}

int os32_main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (h_strcmp(c, "ok817") == 0) case_ok817();
    else if (h_strcmp(c, "ok1663") == 0) case_ok1663();
    else if (h_strcmp(c, "modes") == 0) case_modes();
    else if (h_strcmp(c, "preflight") == 0) case_preflight();
    else if (h_strcmp(c, "incomplete") == 0) case_incomplete();
    else if (h_strcmp(c, "paths") == 0) case_paths();
    else if (h_strcmp(c, "final") == 0) case_final();
    else if (h_strcmp(c, "erase") == 0) case_erase();
    else if (h_strcmp(c, "erase_fail") == 0) case_erase_fail();
    else if (h_strcmp(c, "erase_mount") == 0) case_erase_mount();
    else if (h_strcmp(c, "keys") == 0) case_keys();
    else { report("unknown case\n"); return 2; }
    report("cdinst: PASS (");
    report(c);
    report(")\n");
    return 0;
}

void start_c(long *sp);

__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "  movl %esp, %eax\n"
        "  andl $-16, %esp\n"
        "  pushl %eax\n"
        "  call start_c\n"
        "  hlt\n");

void start_c(long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)&sp[1];
    die(os32_main(argc, argv));
}
