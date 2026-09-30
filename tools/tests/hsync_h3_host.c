/* =========================================================================
 *  HSYNC_H3_HOST.C — 票 H3 (mtime の取得と保存、日時の前置判定) を
 *                    **実物のソースで** 確かめる
 *
 *  対象票: docs/archive/shell/TASK_H3.md §6 / §8
 *          docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md §5 / §7.2 / §9 の A03 A04 A16
 *  実行:   python3 -B tools/tests/test_hsync_h3.py [--target]
 *  記録:   tools/tests/h3_tdd.md
 *
 *  userland/system/hsync.c を 1 行も写さずそのまま #include する (模型では
 *  ない)。差し替えるのは KernelAPI だけ — オンメモリの贋ファイルシステムに
 *  向け、**ノードごとに mtime を持たせ**、sys_set_mtime の成功 / NOSYS /
 *  I/O 失敗を注入できるようにしてある。sys_read の**呼び出し回数**も数える
 *  ので、「サイズも日時も同じなら 1 バイトも読まない」を直に押さえられる。
 *
 *  fs/hostdrv_stat_rules.inc の FILETIME 変換 (hdrv_filetime_to_unix /
 *  hdrv_stat_mtime) も同じ翻訳単位で直接叩く (A16)。純関数なので境界
 *  (1970 年より前・未提供・os_time_t 超過) をそのまま確かめられる。
 *
 *  **この票の一番大事な規則**: 「証拠が無いことを同一の根拠にしない」。
 *  日時が不明 (0) のときに黙ってスキップする実装は不可なので、そこには
 *  変異試験 (test_hsync_h3.py の --mutate) を必ず当てる。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "os32api.h"      /* KernelAPI / OS32_Stat / OS32_ERR_* / u8,u32 */

/* HostDrv の stat 規則 (票 H1 の是正 + 票 H3 の時刻変換)。純関数。 */
#include "../../fs/hostdrv_stat_rules.inc"

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* ========================================================================= */
/*  贋ファイルシステム (H1 のものに mtime と set_mtime を足した形)            */
/* ========================================================================= */

#define FS_MAX_NODES 64
#define FS_MAX_FDS   16
#define FS_PATH_CAP  256

typedef struct {
    int  used;
    char path[FS_PATH_CAP];
    int  is_dir;
    u8  *data;
    u32  size;
    u32  cap;
    u32  dev;
    u32  ino;
    u32  mtime;          /* 0 = 不明 (票 H3: 同一判定の根拠に使えない) */

    /* --- 注入 --- */
    int  stat_err;
    int  set_mtime_rc;   /* != 0 … sys_set_mtime がこの値を返す */
} FNode;

typedef struct {
    int used;
    int node;
    u32 pos;
} FFd;

static FNode fs_nodes[FS_MAX_NODES];
static FFd   fs_fds[FS_MAX_FDS];
static u32   fs_next_ino;
static int   fk_write_calls;
static int   fk_read_calls;      /* 「読まずに省略した」を押さえる要 */
static int   fk_set_mtime_calls;
static u32   fk_last_set_mtime;  /* 直前に渡された mtime */
static int   fk_set_mtime_all_rc; /* != 0 … 全ノードでこの値を返す (NOSYS 等) */

static char  fk_log[65536];
static u32   fk_log_len;

static void fs_reset(void)
{
    int i;
    for (i = 0; i < FS_MAX_NODES; i++) {
        if (fs_nodes[i].data) free(fs_nodes[i].data);
        memset(&fs_nodes[i], 0, sizeof(FNode));
    }
    for (i = 0; i < FS_MAX_FDS; i++) memset(&fs_fds[i], 0, sizeof(FFd));
    fs_next_ino = 100;
    fk_write_calls = 0;
    fk_read_calls = 0;
    fk_set_mtime_calls = 0;
    fk_last_set_mtime = 0;
    fk_set_mtime_all_rc = 0;
    fk_log_len = 0;
    fk_log[0] = '\0';
}

static int fs_find(const char *path)
{
    int i;
    for (i = 0; i < FS_MAX_NODES; i++)
        if (fs_nodes[i].used && strcmp(fs_nodes[i].path, path) == 0) return i;
    return -1;
}

static int fs_new(const char *path, int is_dir)
{
    int i;
    for (i = 0; i < FS_MAX_NODES; i++) {
        if (fs_nodes[i].used) continue;
        memset(&fs_nodes[i], 0, sizeof(FNode));
        fs_nodes[i].used = 1;
        fs_nodes[i].is_dir = is_dir;
        strncpy(fs_nodes[i].path, path, FS_PATH_CAP - 1);
        fs_nodes[i].dev = 1;
        fs_nodes[i].ino = fs_next_ino++;
        return i;
    }
    printf("  (harness) node table full\n");
    exit(2);
}

static void fs_set_data(int n, const u8 *data, u32 size)
{
    if (fs_nodes[n].data) free(fs_nodes[n].data);
    fs_nodes[n].data = (u8 *)malloc(size ? size : 1);
    fs_nodes[n].cap = size ? size : 1;
    if (size) memcpy(fs_nodes[n].data, data, size);
    fs_nodes[n].size = size;
}

static int fs_add_file(const char *path, const u8 *data, u32 size)
{
    int n = fs_new(path, 0);
    fs_set_data(n, data, size);
    return n;
}

static int fs_add_dir(const char *path)
{
    return fs_new(path, 1);
}

static void fs_parent(const char *path, char *out)
{
    int last = -1;
    int i;
    for (i = 0; path[i]; i++) if (path[i] == '/') last = i;
    if (last <= 0) { strcpy(out, "/"); return; }
    memcpy(out, path, (size_t)last);
    out[last] = '\0';
}

static const char *fs_base(const char *path)
{
    const char *b = path;
    const char *p;
    for (p = path; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

/* ========================================================================= */
/*  贋 KernelAPI                                                              */
/* ========================================================================= */

static void *fk_mem_alloc(u32 size) { return malloc(size); }
static void  fk_mem_free(void *p)   { free(p); }

static void fk_kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    char line[2048];
    int n;

    (void)attr;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((u32)n >= sizeof(line)) n = (int)sizeof(line) - 1;
    if (fk_log_len + (u32)n + 1 < sizeof(fk_log)) {
        memcpy(fk_log + fk_log_len, line, (size_t)n);
        fk_log_len += (u32)n;
        fk_log[fk_log_len] = '\0';
    }
}

static int fk_sys_is_mounted(const char *prefix)
{
    return strcmp(prefix, "/host") == 0;
}

/* vfs_devname: 同期先は HDD (ルート hd0)。FD の判定は hsync_h2_host.c が見る */
static const char *fk_vfs_devname(const char *prefix)
{
    return strcmp(prefix, "/") == 0 ? "hd0" : "";
}

static int fk_vfs_sync(void) { return 0; }

static int fk_sys_mkdir(const char *path)
{
    if (fs_find(path) >= 0) return OS32_ERR_EXIST;
    fs_add_dir(path);
    return 0;
}

static int fk_sys_ls(const char *path, void *cb, void *ctx)
{
    DirCallback fn = (DirCallback)cb;
    char dir[FS_PATH_CAP];
    char parent[FS_PATH_CAP];
    DirEntry_Ext e;
    int i;
    int n;

    strncpy(dir, path && path[0] ? path : "/", FS_PATH_CAP - 1);
    dir[FS_PATH_CAP - 1] = '\0';
    n = (int)strlen(dir);
    while (n > 1 && dir[n - 1] == '/') dir[--n] = '\0';

    if (strcmp(dir, "/") != 0) {
        i = fs_find(dir);
        if (i < 0) return OS32_ERR_NOTFOUND;
        if (!fs_nodes[i].is_dir) return OS32_ERR_NOTDIR;
    }

    for (i = 0; i < FS_MAX_NODES; i++) {
        if (!fs_nodes[i].used) continue;
        fs_parent(fs_nodes[i].path, parent);
        if (strcmp(parent, dir) != 0) continue;
        memset(&e, 0, sizeof(e));
        strncpy(e.name, fs_base(fs_nodes[i].path), OS32_MAX_PATH - 1);
        e.size = fs_nodes[i].size;
        e.type = fs_nodes[i].is_dir ? OS32_FILE_TYPE_DIR : OS32_FILE_TYPE_FILE;
        fn(&e, ctx);
    }
    return 0;
}

static int fk_sys_stat(const char *path, OS32_Stat *buf)
{
    int n = fs_find(path);

    memset(buf, 0, sizeof(OS32_Stat));
    if (n < 0) return OS32_ERR_NOTFOUND;
    if (fs_nodes[n].stat_err) return fs_nodes[n].stat_err;

    buf->st_dev = fs_nodes[n].dev;
    buf->st_ino = fs_nodes[n].ino;
    buf->st_nlink = 1;
    buf->st_mode = (u16)(fs_nodes[n].is_dir ? (OS_S_IFDIR | 0755)
                                            : (OS_S_IFREG | 0644));
    buf->st_size = fs_nodes[n].size;
    buf->st_mtime = fs_nodes[n].mtime;   /* 0 = 不明 */
    return 0;
}

static int fk_sys_open(const char *path, int mode)
{
    int n = fs_find(path);
    int f;

    if (mode & KAPI_O_EXCL) {
        /* 票 H2 §2-1: O_CREAT と組でだけ有効、名前が在れば種別を
         * 問わず EXIST。ext2 と同じ契約。 */
        if (!(mode & KAPI_O_CREAT)) return OS32_ERR_INVAL;
        if (n >= 0) return OS32_ERR_EXIST;
    }
    if (n < 0) {
        if (!(mode & KAPI_O_CREAT)) return OS32_ERR_NOTFOUND;
        n = fs_add_file(path, 0, 0);
    } else if (fs_nodes[n].is_dir) {
        return OS32_ERR_ISDIR;
    }
    if (mode & KAPI_O_TRUNC) fs_nodes[n].size = 0;

    for (f = 0; f < FS_MAX_FDS; f++) {
        if (fs_fds[f].used) continue;
        fs_fds[f].used = 1;
        fs_fds[f].node = n;
        fs_fds[f].pos = 0;
        return f + 3;
    }
    return OS32_ERR_NOSPC;
}

static void fk_sys_close(int fd)
{
    if (fd < 3 || fd - 3 >= FS_MAX_FDS) return;
    fs_fds[fd - 3].used = 0;
}

static int fk_sys_read(int fd, void *buf, u32 size)
{
    FFd *h;
    FNode *nd;
    u32 avail;

    fk_read_calls++;
    if (fd < 3 || fd - 3 >= FS_MAX_FDS || !fs_fds[fd - 3].used)
        return OS32_ERR_IO;
    h = &fs_fds[fd - 3];
    nd = &fs_nodes[h->node];

    if (h->pos >= nd->size) return 0;
    avail = nd->size - h->pos;
    if (avail > size) avail = size;
    memcpy(buf, nd->data + h->pos, avail);
    h->pos += avail;
    return (int)avail;
}

static int fk_sys_write(int fd, const void *buf, u32 size)
{
    FFd *h;
    FNode *nd;

    fk_write_calls++;
    if (fd < 3 || fd - 3 >= FS_MAX_FDS || !fs_fds[fd - 3].used)
        return OS32_ERR_IO;
    h = &fs_fds[fd - 3];
    nd = &fs_nodes[h->node];

    if (h->pos + size > nd->cap) {
        u32 newcap = (h->pos + size) * 2 + 16;
        u8 *p = (u8 *)realloc(nd->data, newcap);
        if (!p) return OS32_ERR_NOSPC;
        nd->data = p;
        nd->cap = newcap;
    }
    memcpy(nd->data + h->pos, buf, size);
    h->pos += size;
    if (h->pos > nd->size) nd->size = h->pos;
    /* 本物と同じく、**書き込みは mtime をゲストの現在時刻で上書きする**。
     * だから hsync は「書き終えてから」mtime を設定しなければならない。 */
    nd->mtime = 555555;
    return (int)size;
}

/* sys_set_mtime (KAPI v52)。宛先 FS が持たない場合を NOSYS で模す。 */
static int fk_sys_set_mtime(const char *path, u32 mtime)
{
    int n;

    fk_set_mtime_calls++;
    fk_last_set_mtime = mtime;
    if (fk_set_mtime_all_rc) return fk_set_mtime_all_rc;
    if (mtime == 0) return OS32_ERR_INVAL;   /* カーネル側と同じ規則 */
    n = fs_find(path);
    if (n < 0) return OS32_ERR_NOTFOUND;
    if (fs_nodes[n].set_mtime_rc) return fs_nodes[n].set_mtime_rc;
    fs_nodes[n].mtime = mtime;
    return 0;
}

static KernelAPI g_fake;


/* ---- 票 H2: 一時ファイル方式に必要な口 -------------------------------
 *
 * hsync は KAPI v53 以降で宛先を **一時ファイル `.hs~<名前>` へ書いてから
 * rename で置き換える**。H1 / H3 が見ている判定 (サイズ・日時・内容比較・
 * mtime の保存) は何も変わらないが、**書く先が変わる**ので、贋 FS 側に
 * 排他的作成 (O_EXCL)・unlink・rename の 3 つを足す。
 * 故障は注入しない (それは tools/tests/hsync_h2_host.c の担当)。 */
static int fk_sys_unlink(const char *path)
{
    int n = fs_find(path);
    if (n < 0) return OS32_ERR_NOTFOUND;
    if (fs_nodes[n].is_dir) return OS32_ERR_ISDIR;
    if (fs_nodes[n].data) free(fs_nodes[n].data);
    memset(&fs_nodes[n], 0, sizeof(FNode));
    return 0;
}

static int fk_sys_rename(const char *oldpath, const char *newpath)
{
    int src_n = fs_find(oldpath);
    int dst_n;

    if (src_n < 0) return OS32_ERR_NOTFOUND;
    dst_n = fs_find(newpath);
    if (dst_n >= 0) {
        if (fs_nodes[dst_n].data) free(fs_nodes[dst_n].data);
        memset(&fs_nodes[dst_n], 0, sizeof(FNode));
    }
    strncpy(fs_nodes[src_n].path, newpath, FS_PATH_CAP - 1);
    fs_nodes[src_n].path[FS_PATH_CAP - 1] = '\0';
    return 0;
}

static void fake_api_init(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.mem_alloc = fk_mem_alloc;
    g_fake.mem_free = fk_mem_free;
    g_fake.kprintf = fk_kprintf;
    g_fake.sys_mkdir = fk_sys_mkdir;
    g_fake.sys_ls = fk_sys_ls;
    g_fake.sys_is_mounted = fk_sys_is_mounted;
    g_fake.vfs_devname = fk_vfs_devname;
    g_fake.vfs_sync = fk_vfs_sync;
    g_fake.sys_open = fk_sys_open;
    g_fake.sys_close = fk_sys_close;
    g_fake.sys_read = fk_sys_read;
    g_fake.sys_write = fk_sys_write;
    g_fake.sys_stat = fk_sys_stat;
    g_fake.sys_unlink = fk_sys_unlink;
    g_fake.sys_rename = fk_sys_rename;
    /* 動いているカーネルの KAPI 版。票 H2 の hsync は
     * v53 未満を kernel_too_old で断るので、**必ず立てる**。 */
    g_fake.version = KAPI_VERSION;
    g_fake.sys_set_mtime = fk_sys_set_mtime;
}

/* ========================================================================= */
/*  実物の hsync.c                                                            */
/* ========================================================================= */

#define main hsync_main
#include "../../userland/system/hsync.c"
#undef main

/* ========================================================================= */
/*  小道具                                                                    */
/* ========================================================================= */

/* 票 TASK_KAPI_DATA_FIELDS: この試験は配備の名札 (.deploy/manifest.txt) を
 * 置かないので、KAPI の門 (名札の kapi= を確かめられなければ断る) を
 * `--force-kapi` で明示して越える。門そのものは h4_manifest_host.c の
 * case_kapi が見る。 */
static int run_hsync(int argc, char **argv)
{
    char *av[16];
    int i;

    fk_log_len = 0;
    fk_log[0] = '\0';
    fk_write_calls = 0;
    fk_read_calls = 0;
    fk_set_mtime_calls = 0;
    fk_last_set_mtime = 0;
    for (i = 0; i < argc && i < 14; i++) av[i] = argv[i];
    av[i++] = (char *)"--force-kapi";
    av[i] = 0;
    return hsync_main(i, av, &g_fake);
}

static int run1(const char *a1)
{
    char *av[3];
    av[0] = (char *)"hsync";
    av[1] = (char *)a1;
    av[2] = 0;
    return run_hsync(2, av);
}

static int run2(const char *a1, const char *a2)
{
    char *av[4];
    av[0] = (char *)"hsync";
    av[1] = (char *)a1;
    av[2] = (char *)a2;
    av[3] = 0;
    return run_hsync(3, av);
}

static int log_has(const char *needle)
{
    return strstr(fk_log, needle) != 0;
}

static u8 *make_blob(u32 size, u32 seed)
{
    u8 *p = (u8 *)malloc(size ? size : 1);
    u32 i;
    for (i = 0; i < size; i++)
        p[i] = (u8)((i * 31u + seed * 7u + (i >> 8)) & 0xFF);
    return p;
}

/* /host/bin/a.bin と /bin/a.bin を作り、両側の mtime を指定する。 */
static void setup_pair(u32 src_size, u32 src_seed, u32 src_mtime,
                       u32 dst_size, u32 dst_seed, u32 dst_mtime)
{
    u8 *a;
    int n;

    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");

    a = make_blob(src_size, src_seed);
    n = fs_add_file("/host/bin/a.bin", a, src_size);
    fs_nodes[n].mtime = src_mtime;
    free(a);

    a = make_blob(dst_size, dst_seed);
    n = fs_add_file("/bin/a.bin", a, dst_size);
    fs_nodes[n].mtime = dst_mtime;
    free(a);
}

static u32 node_mtime(const char *path)
{
    int n = fs_find(path);
    return n < 0 ? 0xFFFFFFFFUL : fs_nodes[n].mtime;
}

static int node_equal(const char *pa, const char *pb)
{
    int a = fs_find(pa);
    int b = fs_find(pb);
    if (a < 0 || b < 0) return 0;
    if (fs_nodes[a].size != fs_nodes[b].size) return 0;
    if (fs_nodes[a].size == 0) return 1;
    return memcmp(fs_nodes[a].data, fs_nodes[b].data, fs_nodes[a].size) == 0;
}

/* ========================================================================= */
/*  A16 — FILETIME (1601 起点・100ns) -> Unix 秒                              */
/* ========================================================================= */

/* 基準値は python3 で独立に出したもの (tools/tests/h3_tdd.md に計算式)。
 *   2026-09-15 01:02:03 UTC -> 0x01DD44ADD1BF7780 / 1789434123
 *   2000-01-01 00:00:00 UTC -> 0x01BF53EB256D4000 /  946684800
 *   1970-01-01 00:00:00 UTC -> 0x019DB1DED53E8000 /          0  (= 起点差) */
#define FT_2026 0x01DD44ADD1BF7780ULL
#define FT_2000 0x01BF53EB256D4000ULL
#define FT_1970 0x019DB1DED53E8000ULL

static void case_a16(void)
{
    u32 out;
    int ok;

    printf("== A16: FILETIME -> Unix 秒 (既知日時・秒未満・境界) ==\n");

    /* 既知日時。**JST の 9 時間を足さない** (足すと 32400 ずれる) */
    out = 0xDEADBEEFUL;
    ok = hdrv_filetime_to_unix(FT_2026, &out);
    check(ok == 1 && out == 1789434123UL,
          "2026-09-15 01:02:03 UTC -> 1789434123 (UTC のまま)");
    check(out != 1789434123UL + 32400UL, "JST の 9 時間を足していない");

    ok = hdrv_filetime_to_unix(FT_2000, &out);
    check(ok == 1 && out == 946684800UL, "2000-01-01 00:00:00 UTC -> 946684800");

    /* 秒未満は切り捨て。9999999 (= 0.9999999 秒) 足しても秒は動かない */
    ok = hdrv_filetime_to_unix(FT_2000 + 9999999ULL, &out);
    check(ok == 1 && out == 946684800UL, "秒未満 (+0.9999999s) は切り捨て");
    ok = hdrv_filetime_to_unix(FT_2000 + 10000000ULL, &out);
    check(ok == 1 && out == 946684801UL, "ちょうど 1 秒は繰り上がる");

    /* 未提供 (0) */
    out = 0xDEADBEEFUL;
    ok = hdrv_filetime_to_unix(0ULL, &out);
    check(ok == 0 && out == 0, "未提供 (0) -> 判定できない、*out = 0");

    /* 1970 年より前。**回り込ませない** */
    out = 0xDEADBEEFUL;
    ok = hdrv_filetime_to_unix(FT_1970 - 1ULL, &out);
    check(ok == 0 && out == 0, "1970 の 100ns 前 -> 判定できない (wrap しない)");
    ok = hdrv_filetime_to_unix(FT_1970 - 10000000ULL, &out);
    check(ok == 0 && out == 0, "1969-12-31 23:59:59 -> 判定できない");
    ok = hdrv_filetime_to_unix(1ULL, &out);
    check(ok == 0 && out == 0, "1601-01-01 直後 -> 判定できない");

    /* ちょうど起点 = Unix epoch。変換はできるが値は 0 =
     * 現行 ABI では「不明」と区別が付かない (設計書 §5.1 / §10 の (2)) */
    ok = hdrv_filetime_to_unix(FT_1970, &out);
    check(ok == 1 && out == 0,
          "ちょうど 1970-01-01 00:00:00 UTC -> 0 (不明と同じ扱いになる)");

    /* os_time_t (u32) の上限ちょうど / 超過 */
    ok = hdrv_filetime_to_unix(FT_1970 + 0xFFFFFFFFULL * 10000000ULL, &out);
    check(ok == 1 && out == 0xFFFFFFFFUL, "u32 上限ちょうど -> 0xFFFFFFFF");
    ok = hdrv_filetime_to_unix(FT_1970 + 0xFFFFFFFFULL * 10000000ULL + 9999999ULL,
                               &out);
    check(ok == 1 && out == 0xFFFFFFFFUL, "上限 + 秒未満 -> 0xFFFFFFFF のまま");
    out = 0xDEADBEEFUL;
    ok = hdrv_filetime_to_unix(FT_1970 + 0x100000000ULL * 10000000ULL, &out);
    check(ok == 0 && out == 0, "u32 を 1 秒超える -> 判定できない (wrap しない)");
    out = 0xDEADBEEFUL;
    ok = hdrv_filetime_to_unix(0xFFFFFFFFFFFFFFFFULL, &out);
    check(ok == 0 && out == 0, "FILETIME の最大値 -> 判定できない (wrap しない)");

    /* NULL 出力先 */
    check(hdrv_filetime_to_unix(FT_2000, 0) == 0, "out が NULL なら 0");

    /* hdrv_stat_mtime: Basic が取れていなければ時刻も無い */
    check(hdrv_stat_mtime(0, FT_2000) == 946684800UL,
          "hdrv_stat_mtime: Basic 成功 -> 変換した値");
    check(hdrv_stat_mtime(-1, FT_2000) == 0,
          "hdrv_stat_mtime: Basic 失敗 -> 0 (値を信用しない)");
    check(hdrv_stat_mtime(0, 0ULL) == 0, "hdrv_stat_mtime: 未提供 -> 0");
    check(hdrv_stat_mtime(0, FT_1970 - 1ULL) == 0,
          "hdrv_stat_mtime: 1970 より前 -> 0");

    /* hdrv_stat_fill (票 H1) の規則は変えていない */
    {
        OS32_Stat st;
        memset(&st, 0, sizeof(st));
        check(hdrv_stat_fill(0, 0, 0, 100ULL, &st) == 0 && st.st_size == 100,
              "H1: 通常ファイルの stat は従来どおり成功する");
        check(hdrv_stat_fill(-1, 0, 0, 0ULL, &st) == OS32_ERR_IO,
              "H1: Basic 失敗は IO のまま");
        check(hdrv_stat_fill(0, 0, -1, 0ULL, &st) == OS32_ERR_IO,
              "H1: Standard 失敗は IO のまま");
        check(hdrv_stat_fill(0, 0, 0, 0x100000000ULL, &st) == OS32_ERR_IO,
              "H1: 4GiB 超の EndOfFile は拒否のまま");
    }
}

/* ========================================================================= */
/*  日時が不明 (0) なら必ず内容を比較する — この票の中心                      */
/* ========================================================================= */

static void case_unknown_mtime(void)
{
    int rc;

    printf("== 日時が不明 (両側 0) で内容が違う -> コピーする ==\n");
    /* **ここが「証拠が無いことを同一の根拠にしない」の本体。**
     * 日時で省略する実装にすると、ここが黙って unchanged になる。 */
    setup_pair(4096, 1, 0, 4096, 2, 0);
    rc = run1("bin");
    check(rc == 0, "終了コード 0");
    check(log_has("reason=content_changed"),
          "両側 mtime=0 でも内容を比較して content_changed");
    check(log_has("copied=1"), "copied=1");
    check(fk_read_calls > 0, "内容を読んでいる (省略していない)");
    check(node_equal("/host/bin/a.bin", "/bin/a.bin"), "宛先が元と一致した");

    printf("== コピー元だけ mtime=0 -> 内容を比較する ==\n");
    setup_pair(4096, 1, 0, 4096, 2, 777);
    rc = run1("bin");
    check(rc == 0 && log_has("reason=content_changed"),
          "src=0 / dst=777 でも内容比較でコピー");

    printf("== 宛先だけ mtime=0 -> 内容を比較する ==\n");
    setup_pair(4096, 1, 777, 4096, 2, 0);
    rc = run1("bin");
    check(rc == 0 && log_has("reason=content_changed"),
          "src=777 / dst=0 でも内容比較でコピー");

    printf("== 日時が不明で内容も同じ -> unchanged。省略を表示する ==\n");
    setup_pair(4096, 3, 0, 4096, 3, 0);
    rc = run2("-v", "bin");
    check(rc == 0 && log_has("unchanged=1") && log_has("copied=0"),
          "unchanged=1 / copied=0");
    check(fk_read_calls > 0, "内容を読んで確かめている");
    check(fk_set_mtime_calls == 0,
          "mtime=0 を書きに行かない (不明を書くと証拠を捏造する)");
    check(log_has("reason=content_same"), "-v が content_same を出す");
    check(log_has("metadata_updated=0"), "metadata_updated=0");
}

/* ========================================================================= */
/*  サイズも日時も同じ -> 1 バイトも読まない                                  */
/* ========================================================================= */

static void case_skip_by_mtime(void)
{
    int rc;

    printf("== サイズも日時も同じ -> 読まずにスキップ ==\n");
    setup_pair(4096, 3, 1234, 4096, 3, 1234);
    rc = run2("-v", "bin");
    check(rc == 0 && log_has("unchanged=1"), "unchanged=1");
    check(fk_read_calls == 0, "**sys_read を 1 度も呼んでいない** (速さの源)");
    check(fk_write_calls == 0, "1 度も write していない");
    check(fk_set_mtime_calls == 0, "mtime も書かない (既に同じ)");
    check(log_has("reason=size_mtime_same"), "-v が size_mtime_same を出す");

    printf("== 失うもの: サイズも日時も同じで中身が違う場合は見逃す ==\n");
    /* 票 §8 の「失うもの」を**そのまま試験にする**。黙って変わったら
     * それは仕様の変更なので、ここで気付けるようにしておく。 */
    setup_pair(4096, 1, 1234, 4096, 2, 1234);
    rc = run1("bin");
    check(rc == 0 && log_has("unchanged=1") && log_has("copied=0"),
          "既定では見逃す (票 §8 が明記した唯一の取りこぼし)");
    check(!node_equal("/host/bin/a.bin", "/bin/a.bin"), "中身は違うまま");

    printf("== --verify なら日時が同じでも内容を比較して捕まえる ==\n");
    setup_pair(4096, 1, 1234, 4096, 2, 1234);
    rc = run2("--verify", "bin");
    check(rc == 0 && log_has("reason=content_changed") && log_has("copied=1"),
          "--verify -> content_changed でコピー");
    check(fk_read_calls > 0, "--verify は内容を読む");
    check(node_equal("/host/bin/a.bin", "/bin/a.bin"), "宛先が元と一致した");
    check(log_has("verify mode"), "verify mode と表示する");

    printf("== --verify で日時も内容も同じ -> unchanged (ただし読む) ==\n");
    setup_pair(4096, 3, 1234, 4096, 3, 1234);
    rc = run2("--verify", "bin");
    check(rc == 0 && log_has("unchanged=1"), "unchanged=1");
    check(fk_read_calls > 0, "--verify は日時が同じでも読む");
}

/* ========================================================================= */
/*  A03 — 内容同一で mtime だけ相違 -> 本体コピーなし、metadata_updated       */
/* ========================================================================= */

static void case_a03(void)
{
    int rc;

    printf("== A03: 内容同一・mtime だけ相違 -> mtime だけ更新 ==\n");
    setup_pair(4096, 3, 111111, 4096, 3, 222222);
    rc = run1("bin");
    check(rc == 0, "終了コード 0");
    check(log_has("MTIME /bin/a.bin reason=mtime_only"),
          "MTIME ... reason=mtime_only を出す");
    check(log_has("metadata_updated=1"), "metadata_updated=1");
    check(log_has("copied=0"), "copied=0 (本体は書き直していない)");
    check(fk_write_calls == 0, "**1 度も write していない**");
    check(fk_set_mtime_calls == 1, "sys_set_mtime を 1 度だけ呼んだ");
    check(fk_last_set_mtime == 111111UL, "コピー元の mtime を渡した");
    check(node_mtime("/bin/a.bin") == 111111UL, "宛先の mtime が元と揃った");

    printf("== A03 の続き: 2 回目は読まずにスキップされる ==\n");
    rc = run1("bin");
    check(rc == 0 && log_has("unchanged=1"), "2 回目は unchanged");
    check(fk_read_calls == 0, "2 回目は 1 バイトも読まない");

    printf("== コピー元の mtime が 0 なら mtime だけの更新は起きない ==\n");
    setup_pair(4096, 3, 0, 4096, 3, 222222);
    rc = run2("-v", "bin");
    check(rc == 0 && log_has("unchanged=1") && log_has("metadata_updated=0"),
          "src の mtime が不明なら unchanged のまま");
    check(fk_set_mtime_calls == 0, "0 を書きに行かない");
    check(log_has("元の mtime が不明"), "省略したことを表示する");
    check(log_has("reason=mtime_unknown"), "固定の理由コードを出す");
}

/* ========================================================================= */
/*  A04 — 元の mtime が古いが内容が違う -> 同期する                           */
/* ========================================================================= */

static void case_a04(void)
{
    int rc;

    printf("== A04: 元の mtime が**古い**が内容が違う -> 同期する ==\n");
    /* 「新しい方だけコピー」にすると、ここが落ちる。日時は前置フィルタで
     * あって新旧の判定材料ではない (設計書 §3.1)。 */
    setup_pair(4096, 1, 100, 4096, 2, 999999);
    rc = run1("bin");
    check(rc == 0 && log_has("reason=content_changed") && log_has("copied=1"),
          "元が古くてもコピーする");
    check(node_equal("/host/bin/a.bin", "/bin/a.bin"), "宛先が元と一致した");
    check(fk_set_mtime_calls == 1, "コピー後に mtime を設定した");
    check(fk_last_set_mtime == 100UL, "**古い方の** mtime をそのまま保存した");
    check(node_mtime("/bin/a.bin") == 100UL,
          "書き込みが入れた現在時刻 (555555) を**上書きして**元の値になった");

    printf("== サイズが違う -> 読み比べずにコピーし、mtime も保存する ==\n");
    setup_pair(100, 1, 4242, 200, 1, 4242);
    rc = run1("bin");
    check(rc == 0 && log_has("reason=size_changed") && log_has("copied=1"),
          "size_changed でコピー");
    check(node_mtime("/bin/a.bin") == 4242UL, "mtime も揃った");

    printf("== 宛先が無い (new_file) -> コピーして mtime を保存する ==\n");
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    {
        u8 *a = make_blob(64, 7);
        int n = fs_add_file("/host/bin/new.bin", a, 64);
        fs_nodes[n].mtime = 31337;
        free(a);
    }
    rc = run1("bin");
    check(rc == 0 && log_has("reason=new_file") && log_has("copied=1"),
          "new_file でコピー");
    check(node_mtime("/bin/new.bin") == 31337UL, "新規でも mtime を保存する");

    printf("== -f (force) でもコピー後に mtime を保存する ==\n");
    setup_pair(4096, 3, 8888, 4096, 3, 8888);
    rc = run2("-f", "bin");
    check(rc == 0 && log_has("reason=forced") && log_has("copied=1"),
          "-f -> forced でコピー");
    check(fk_set_mtime_calls == 1 && fk_last_set_mtime == 8888UL,
          "force でも mtime を保存する");
}

/* ========================================================================= */
/*  非対応 FS (NOSYS) / 設定失敗 (metadata_failed)                            */
/* ========================================================================= */

static void case_nosys_and_failure(void)
{
    int rc;

    printf("== 非対応 FS -> NOSYS。内容同期は継続し、省略を表示する ==\n");
    setup_pair(4096, 1, 111, 4096, 2, 222);
    fk_set_mtime_all_rc = OS32_ERR_NOSYS;
    rc = run1("bin");
    check(rc == 0, "**エラーにしない** (終了コード 0)");
    check(log_has("copied=1"), "内容の同期は行った");
    check(node_equal("/host/bin/a.bin", "/bin/a.bin"), "宛先が元と一致した");
    check(log_has("errors=0"), "errors=0");
    check(log_has("reason=mtime_unsupported"), "固定の理由コードを出す");
    check(log_has("対応していない"), "省略したことを表示する");
    fk_set_mtime_all_rc = 0;

    printf("== 非対応 FS で内容だけ同じ -> unchanged。省略を表示する ==\n");
    setup_pair(4096, 3, 111, 4096, 3, 222);
    fk_set_mtime_all_rc = OS32_ERR_NOSYS;
    rc = run1("bin");
    check(rc == 0 && log_has("unchanged=1") && log_has("metadata_updated=0"),
          "NOSYS なら metadata_updated に数えない");
    check(log_has("reason=mtime_unsupported"), "省略を表示する");
    fk_set_mtime_all_rc = 0;

    printf("== 設定失敗 -> metadata_failed + 非ゼロ終了 (コピー経路) ==\n");
    setup_pair(4096, 1, 111, 4096, 2, 222);
    fk_set_mtime_all_rc = OS32_ERR_IO;
    rc = run1("bin");
    check(rc != 0, "**非ゼロ終了** (コピー成功だけで全成功と言わない)");
    check(log_has("reason=metadata_failed"), "metadata_failed を出す");
    /* **票 H2 §2-3 手順 7 でここが変わった** (Codex 往復 1 所見 4)。
     * mtime は**一時ファイル**に設定するので、その失敗は「公開の前の失敗」に
     * なる。ext2 はメタデータの I/O 失敗でマウントを書き込み禁止にするため、
     * 続く rename は必ず ROFS になり宛先は旧内容のまま — だから copied に
     * 数えない。H3 の時点では本名へ書いた後に mtime を付けていたので
     * copied=1 が正しかった。見ている規則 (設定失敗を握り潰さない・
     * 非ゼロ終了する) は変わっていない。 */
    check(log_has("copied=0"),
          "**copied に数えない** (公開の前の失敗、票 H2 §2-3 手順 7)");
    check(!log_has("UPDATE "), "UPDATE とは言わない");
    check(log_has("errors=1"), "errors=1");
    check(log_has("FAILED"), "頭が FAILED になる");
    fk_set_mtime_all_rc = 0;

    printf("== 設定失敗 -> metadata_failed + 非ゼロ終了 (mtime だけ経路) ==\n");
    setup_pair(4096, 3, 111, 4096, 3, 222);
    fk_set_mtime_all_rc = OS32_ERR_IO;
    rc = run1("bin");
    check(rc != 0, "非ゼロ終了");
    check(log_has("reason=metadata_failed"), "metadata_failed を出す");
    check(log_has("metadata_updated=0"), "metadata_updated には数えない");
    check(log_has("errors=1"), "errors=1");
    fk_set_mtime_all_rc = 0;
}

/* ========================================================================= */
/*  dry-run は mtime も書かない                                               */
/* ========================================================================= */

static void case_dry_run(void)
{
    int rc;
    u32 before;

    printf("== dry-run: mtime だけの更新も**書かない** ==\n");
    setup_pair(4096, 3, 111111, 4096, 3, 222222);
    before = node_mtime("/bin/a.bin");
    rc = run2("-n", "bin");
    check(rc == 0, "終了コード 0");
    check(log_has("PLAN /bin/a.bin reason=mtime_only"), "PLAN ... mtime_only");
    check(log_has("metadata_updated=1"), "予定件数として数える");
    check(fk_set_mtime_calls == 0, "**sys_set_mtime を 1 度も呼んでいない**");
    check(fk_write_calls == 0, "1 度も write していない");
    check(node_mtime("/bin/a.bin") == before, "宛先の mtime が変わっていない");
    check(log_has("mtime は 1 件も書いていない"), "その旨を表示する");

    printf("== dry-run: コピー予定のものも mtime を書かない ==\n");
    setup_pair(4096, 1, 111111, 4096, 2, 222222);
    before = node_mtime("/bin/a.bin");
    rc = run2("-n", "bin");
    check(rc == 0 && log_has("PLAN /bin/a.bin reason=content_changed"),
          "PLAN ... content_changed");
    check(fk_set_mtime_calls == 0, "sys_set_mtime を呼んでいない");
    check(node_mtime("/bin/a.bin") == before, "宛先の mtime が変わっていない");
}

/* ========================================================================= */
/*  集計は設計書 §7.2 の 6 区分                                               */
/* ========================================================================= */

static void case_tally(void)
{
    int rc;

    printf("== 集計が copied/unchanged/excluded/protected/metadata_updated/errors ==\n");
    setup_pair(4096, 3, 111111, 4096, 3, 222222);
    rc = run1("bin");
    check(rc == 0, "終了コード 0");
    check(log_has("copied=") && log_has("unchanged=") &&
          log_has("excluded=") && log_has("protected=") &&
          log_has("metadata_updated=") && log_has("errors="),
          "6 区分が全部出ている");
    /* 票 H2 §2-4 で `cleaned` が metadata_updated と errors の間に入った */
    check(log_has("protected=0 metadata_updated=1 cleaned=0 errors=0"),
          "並び順が protected -> metadata_updated -> cleaned -> errors");
}

/* ========================================================================= */

int main(void)
{
    printf("=== hsync H3 ホスト TDD (mtime の取得・保存・前置判定) ===\n");
    fake_api_init();

    case_a16();
    case_unknown_mtime();
    case_skip_by_mtime();
    case_a03();
    case_a04();
    case_nosys_and_failure();
    case_dry_run();
    case_tally();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
