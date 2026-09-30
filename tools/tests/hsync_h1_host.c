/* =========================================================================
 *  HSYNC_H1_HOST.C — 票 H1 (同サイズ更新の検出) を **実物のソースで** 確かめる
 *
 *  対象票: docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md §9 の A01〜A13
 *  実行:   python3 -B tools/tests/test_hsync_h1.py [--target]
 *  記録:   tools/tests/h1_tdd.md
 *
 *  userland/system/hsync.c を 1 行も写さずそのまま #include する (模型では
 *  ない)。差し替えるのは KernelAPI だけ — オンメモリの贋ファイルシステムに
 *  向け、short read の分割・read エラー・早期 EOF・0 進捗 write・短い write・
 *  書き込み時の破損・vfs_sync 失敗・stat 失敗を注入できるようにしてある。
 *
 *  fs/hostdrv_stat_rules.inc (HostDrv の stat 失敗の是正) も同じ翻訳単位で
 *  直接呼ぶ (A11)。CRC は lib/crc32.c (既存の一括版) と同じ実行ファイルに
 *  リンクし、ストリーム核の値が一致することを見る (A07)。
 *
 *  -DHSYNC_CRC_STUB でビルドすると CRC 核を「常に同じ値を返す贋物」に
 *  差し替える。既定の同一判定がバイト比較であることの否定側 (A08)。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "os32api.h"      /* KernelAPI / OS32_Stat / OS32_ERR_* / u8,u32 */
#include "crc32.h"        /* 既存の一括版 crc32_calc (A07 の照合相手) */

/* HostDrv の stat 判定 (票 H1 の是正点)。純関数なのでそのまま呼べる。
 * CRC 贋物ビルド (A08) では使わないので取り込まない。 */
#ifndef HSYNC_CRC_STUB
#include "../../fs/hostdrv_stat_rules.inc"
#endif

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* ========================================================================= */
/*  贋ファイルシステム                                                        */
/* ========================================================================= */

#define FS_MAX_NODES 64
#define FS_MAX_FDS   16
#define FS_PATH_CAP  256

typedef struct {
    int  used;
    char path[FS_PATH_CAP];     /* 絶対パス。ディレクトリに末尾 '/' は付けない */
    int  is_dir;
    u8  *data;
    u32  size;
    u32  cap;
    u32  dev;
    u32  ino;
    u32  mtime;          /* 票 H3。既定 0 = 不明 (この贋 FS は時刻を持たない) */

    /* --- 注入 --- */
    int  stat_err;       /* != 0 … sys_stat がこの値を返す */
    int  mode_zero;      /* 1 … st_mode を 0 (種別不明) で返す */
    u32  stat_size;      /* != 0 … st_size をこの値で偽装する */
    int  read_chunk;     /* > 0 … 1 回の sys_read で返す最大バイト数 */
    int  read_err_after; /* > 0 … このバイト数を返したあとの read が -1 */
    u32  eof_at;         /* > 0 … このオフセットで EOF にする (早期 EOF) */
    int  write_chunk;    /* > 0 … 1 回の sys_write で受ける最大バイト数 */
    int  write_zero;     /* 1 … sys_write が 0 を返す (0 進捗) */
    int  corrupt_write;  /* 1 … 書き込んだ最初の 1 バイトを反転して格納する */
} FNode;

typedef struct {
    int used;
    int node;
    u32 pos;
    u32 served;          /* この fd が返した総バイト数 (read エラー注入用) */
} FFd;

static FNode fs_nodes[FS_MAX_NODES];
static FFd   fs_fds[FS_MAX_FDS];
static u32   fs_next_ino;
static int   fk_sync_fail;
static int   fk_write_calls;
static int   fk_mkdir_calls;
static int   fk_sync_calls;
static int   fk_create_calls;   /* O_CREAT で新規に作った回数 */
/* > 0 … sys_ls がこの件数だけ流してから I/O エラーを返す。HostDrv の
 * hdrv_list_dir が「途中で切れた列挙」を返す状況の再現 (票 H1 / 往復 3)。 */
static int   fk_ls_fail_after;
/* 票 H2: hsync は**一時ファイル** (.hs~<名前>) へ書くようになったので、
 * 「宛先ノードに仕掛ける」注入 (write_zero / corrupt_write) は当たらない。
 * 書き先を問わない大域の旗を足す。見ている規則 (0 進捗を失敗にする /
 * 読戻しで破損を捕まえる) は何も変えていない。 */
static int   fk_write_zero_all;
static int   fk_corrupt_write_all;

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
    fk_sync_fail = 0;
    fk_write_calls = 0;
    fk_mkdir_calls = 0;
    fk_sync_calls = 0;
    fk_create_calls = 0;
    fk_write_zero_all = 0;
    fk_corrupt_write_all = 0;
    fk_ls_fail_after = 0;
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

/* path の親ディレクトリを out に (末尾 '/' 無し、root は "/") */
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

static int fk_vfs_sync(void)
{
    fk_sync_calls++;
    return fk_sync_fail ? OS32_ERR_IO : 0;
}

static int fk_sys_mkdir(const char *path)
{
    fk_mkdir_calls++;
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

    {
        int sent = 0;
        for (i = 0; i < FS_MAX_NODES; i++) {
            if (!fs_nodes[i].used) continue;
            fs_parent(fs_nodes[i].path, parent);
            if (strcmp(parent, dir) != 0) continue;
            if (fk_ls_fail_after > 0 && sent >= fk_ls_fail_after) {
                /* 途中で切れた列挙。**成功として返さない** (票 H1) */
                return OS32_ERR_IO;
            }
            memset(&e, 0, sizeof(e));
            strncpy(e.name, fs_base(fs_nodes[i].path), OS32_MAX_PATH - 1);
            e.size = fs_nodes[i].size;
            e.type = fs_nodes[i].is_dir ? OS32_FILE_TYPE_DIR
                                        : OS32_FILE_TYPE_FILE;
            fn(&e, ctx);
            sent++;
        }
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
    if (fs_nodes[n].mode_zero) {
        buf->st_mode = 0;
    } else {
        buf->st_mode = (u16)(fs_nodes[n].is_dir ? (OS_S_IFDIR | 0755)
                                                : (OS_S_IFREG | 0644));
    }
    buf->st_size = fs_nodes[n].stat_size ? fs_nodes[n].stat_size
                                         : fs_nodes[n].size;
    /* 票 H3: この贋 FS は時刻を持たないので **0 = 不明** を返す
     * (以前は両側 1000 の決め打ちだった)。hsync は「証拠が無い」ことを
     * 同一の根拠にしてはいけないので、0 同士でも必ず内容を比較する —
     * A01 / A02 の期待はそのまま通る。時刻を使う試験は H3 側
     * (tools/tests/hsync_h3_host.c) が node ごとに値を入れて回す。 */
    buf->st_mtime = fs_nodes[n].mtime;
    buf->st_atime = 0;
    buf->st_ctime = 0;
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
        fk_create_calls++;
    } else if (fs_nodes[n].is_dir) {
        return OS32_ERR_ISDIR;
    }
    if (mode & KAPI_O_TRUNC) fs_nodes[n].size = 0;

    for (f = 0; f < FS_MAX_FDS; f++) {
        if (fs_fds[f].used) continue;
        fs_fds[f].used = 1;
        fs_fds[f].node = n;
        fs_fds[f].pos = 0;
        fs_fds[f].served = 0;
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
    u32 avail, limit;

    if (fd < 3 || fd - 3 >= FS_MAX_FDS || !fs_fds[fd - 3].used)
        return OS32_ERR_IO;
    h = &fs_fds[fd - 3];
    nd = &fs_nodes[h->node];

    if (nd->read_err_after > 0 && h->served >= (u32)nd->read_err_after)
        return OS32_ERR_IO;

    limit = nd->size;
    if (nd->eof_at > 0 && nd->eof_at < limit) limit = nd->eof_at;
    if (h->pos >= limit) return 0;
    avail = limit - h->pos;
    if (avail > size) avail = size;
    if (nd->read_chunk > 0 && avail > (u32)nd->read_chunk)
        avail = (u32)nd->read_chunk;

    memcpy(buf, nd->data + h->pos, avail);
    h->pos += avail;
    h->served += avail;
    return (int)avail;
}

static int fk_sys_write(int fd, const void *buf, u32 size)
{
    FFd *h;
    FNode *nd;
    u32 take;

    fk_write_calls++;
    if (fd < 3 || fd - 3 >= FS_MAX_FDS || !fs_fds[fd - 3].used)
        return OS32_ERR_IO;
    h = &fs_fds[fd - 3];
    nd = &fs_nodes[h->node];
    /* 大域の旗は**書き先を問わない** — 票 H2 で hsync が一時ファイルへ
     * 書くようになり、宛先ノードに仕掛ける注入が当たらなくなったため。 */
    if (nd->write_zero || fk_write_zero_all) return 0;

    take = size;
    if (nd->write_chunk > 0 && take > (u32)nd->write_chunk)
        take = (u32)nd->write_chunk;

    if (h->pos + take > nd->cap) {
        u32 newcap = (h->pos + take) * 2 + 16;
        u8 *p = (u8 *)realloc(nd->data, newcap);
        if (!p) return OS32_ERR_NOSPC;
        nd->data = p;
        nd->cap = newcap;
    }
    memcpy(nd->data + h->pos, buf, take);
    if ((nd->corrupt_write || fk_corrupt_write_all) && take > 0)
        nd->data[h->pos] ^= 0xFF;
    h->pos += take;
    if (h->pos > nd->size) nd->size = h->pos;
    return (int)take;
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
}

/* ========================================================================= */
/*  実物の hsync.c                                                            */
/* ========================================================================= */

#ifdef HSYNC_CRC_STUB
/* A08: CRC 核を「常に同じ値を返す贋物」に差し替える。既定の同一判定が
 * バイト比較である限り、内容の違いはこれでも検出できなければならない。 */
#define LIB_CRC32_CORE_INC
#define CRC32_INIT 0UL
static u32 crc32_core_update(u32 state, const void *data, u32 size)
{
    (void)data; (void)size;
    return state;
}
static u32 crc32_core_final(u32 state) { return state; }
#endif

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
    return run_hsync(a1 ? 2 : 1, av);
}

#ifndef HSYNC_CRC_STUB
static int run2(const char *a1, const char *a2)
{
    char *av[4];
    av[0] = (char *)"hsync";
    av[1] = (char *)a1;
    av[2] = (char *)a2;
    av[3] = 0;
    return run_hsync(3, av);
}
#endif /* !HSYNC_CRC_STUB */

static int log_has(const char *needle)
{
    return strstr(fk_log, needle) != 0;
}

static int node_equal(const char *pa, const char *pb)
{
    int a = fs_find(pa);
    int b = fs_find(pb);
    if (a < 0 || b < 0) return 0;
    if (fs_nodes[a].size != fs_nodes[b].size) return 0;
    if (fs_nodes[a].size == 0) return 1;
    return memcmp(fs_nodes[a].data, fs_nodes[b].data,
                  fs_nodes[a].size) == 0;
}

/* 決定的な内容。seed を変えると 1 バイトも同じにならない */
static u8 *make_blob(u32 size, u32 seed)
{
    u8 *p = (u8 *)malloc(size ? size : 1);
    u32 i;
    for (i = 0; i < size; i++)
        p[i] = (u8)((i * 31u + seed * 7u + (i >> 8)) & 0xFF);
    return p;
}

/* 単純な src/dst の 2 ファイル構成を作る */
static void setup_pair(u32 src_size, u32 src_seed,
                       int dst_present, u32 dst_size, u32 dst_seed)
{
    u8 *a;
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    a = make_blob(src_size, src_seed);
    fs_add_file("/host/bin/a.bin", a, src_size);
    free(a);
    if (dst_present) {
        a = make_blob(dst_size, dst_seed);
        fs_add_file("/bin/a.bin", a, dst_size);
        free(a);
    }
}

/* ========================================================================= */
/*  A07 — CRC のストリーム核                                                  */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB
static u32 ref_bitwise(const u8 *b, u32 n)
{
    u32 crc = 0xFFFFFFFFUL;
    u32 i;
    int j;
    for (i = 0; i < n; i++) {
        crc ^= b[i];
        for (j = 0; j < 8; j++)
            crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFUL;
}

static void case_a07(void)
{
    u8 *blob;
    u32 n = 70000;
    u32 s, i, expect;
    u32 tbl[16];

    printf("== A07: CRC-32 ストリーム核 ==\n");

    /* 表が反転多項式から導いた値と一致する */
    for (i = 0; i < 16; i++) {
        u32 c = i;
        int j;
        for (j = 0; j < 4; j++)
            c = (c & 1) ? ((c >> 1) ^ 0xEDB88320UL) : (c >> 1);
        tbl[i] = c;
    }
    check(memcmp(tbl, crc32_core_nib, sizeof(tbl)) == 0,
          "nibble 表が 0xEDB88320 から導いた値と一致");

    /* 既知ベクトル */
    check(crc32_core_final(crc32_core_update(CRC32_INIT, "", 0)) == 0x00000000UL,
          "空列 -> 00000000");
    check(crc32_core_final(crc32_core_update(CRC32_INIT, "123456789", 9))
              == 0xCBF43926UL,
          "\"123456789\" -> CBF43926");
    check(crc32_calc("123456789", 9) == 0xCBF43926UL,
          "既存 crc32_calc も CBF43926 (戻り値を変えていない)");
    check(crc32_calc("", 0) == 0x00000000UL, "既存 crc32_calc 空列 -> 00000000");

    blob = make_blob(n, 5);
    expect = ref_bitwise(blob, n);

    check(crc32_calc(blob, n) == expect, "crc32_calc == ビット版の基準値");

    /* 全量 1 回 */
    check(crc32_core_final(crc32_core_update(CRC32_INIT, blob, n)) == expect,
          "ストリーム核: 全量 1 チャンク");

    /* 1 バイト刻み */
    s = CRC32_INIT;
    for (i = 0; i < n; i++) s = crc32_core_update(s, blob + i, 1);
    check(crc32_core_final(s) == expect, "ストリーム核: 1 バイト刻み");

    /* 不規則チャンク */
    s = CRC32_INIT;
    i = 0;
    while (i < n) {
        u32 step = 1 + ((i * 37u + 11u) % 4099u);
        if (i + step > n) step = n - i;
        s = crc32_core_update(s, blob + i, step);
        i += step;
    }
    check(crc32_core_final(s) == expect, "ストリーム核: 不規則チャンク");

    /* 空チャンクを混ぜても変わらない */
    s = CRC32_INIT;
    s = crc32_core_update(s, blob, 0);
    s = crc32_core_update(s, blob, n);
    s = crc32_core_update(s, blob + n, 0);
    check(crc32_core_final(s) == expect, "空チャンクは無害");

    /* チャンクごとに確定して XOR で畳むのは**別物** (やってはいけない) */
    {
        u32 half = n / 2;
        u32 bad = crc32_core_final(crc32_core_update(CRC32_INIT, blob, half)) ^
                  crc32_core_final(crc32_core_update(CRC32_INIT, blob + half,
                                                     n - half));
        check(bad != expect, "チャンクごとの完成 CRC の XOR は全体と一致しない");
    }

    free(blob);
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */
/*  A11 — HostDrv の stat 失敗 / 64bit サイズ                                 */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB
static void case_a11_rules(void)
{
    OS32_Stat st;

    printf("== A11: hdrv_stat の成否判定 (fs/hostdrv_stat_rules.inc) ==\n");

    memset(&st, 0, sizeof(st));
    check(hdrv_stat_fill(-1, 0, 0, 0, &st) == OS32_ERR_IO,
          "Basic 取得失敗 -> 成功にしない (種別不明をゼロで返さない)");

    memset(&st, 0, sizeof(st));
    check(hdrv_stat_fill(0, 0, -1, 0, &st) == OS32_ERR_IO,
          "Standard 取得失敗 -> 成功にしない (ゼロサイズを空ファイルにしない)");

    memset(&st, 0, sizeof(st));
    check(hdrv_stat_fill(0, 0, 0, 0x100000000ULL, &st) == OS32_ERR_IO,
          "4GiB 超の EndOfFile -> u32 へ切り詰めず拒否");

    memset(&st, 0, sizeof(st));
    check(hdrv_stat_fill(0, 0, 0, 0xFFFFFFFFULL, &st) == 0 &&
              st.st_size == 0xFFFFFFFFUL &&
              (st.st_mode & OS_S_IFMT) == OS_S_IFREG,
          "u32 上限ちょうどは通る (通常ファイル)");

    memset(&st, 0, sizeof(st));
    check(hdrv_stat_fill(0, HDRV_STAT_ATTR_DIRECTORY, 0, 4096ULL, &st) == 0 &&
              (st.st_mode & OS_S_IFMT) == OS_S_IFDIR,
          "DIRECTORY 属性 -> ディレクトリ");

    memset(&st, 0, sizeof(st));
    check(hdrv_stat_fill(0, 0, 0, 12345ULL, &st) == 0 &&
              st.st_size == 12345UL &&
              (st.st_mode & OS_S_IFMT) == OS_S_IFREG,
          "通常ファイルはサイズを転記");
}

static void case_a11_hsync(void)
{
    int rc;
    int n;

    printf("== A11: 上位 (hsync) が stat 失敗を成功にしない ==\n");

    /* コピー元の stat が I/O エラー: 「空ファイル」として同期しない */
    setup_pair(100, 1, 1, 100, 2);
    n = fs_find("/host/bin/a.bin");
    fs_nodes[n].stat_err = OS32_ERR_IO;
    rc = run1("bin");
    check(rc != 0, "src の stat 失敗 -> 非ゼロ終了");
    check(log_has("reason=io_error"), "reason=io_error を出す");
    check(log_has("errors=1"), "errors に数える");
    check(!node_equal("/host/bin/a.bin", "/bin/a.bin"),
          "宛先を書き換えていない");

    /* 種別が取れない (st_mode == 0): 通常ファイルとして読まない */
    setup_pair(100, 1, 1, 100, 2);
    n = fs_find("/host/bin/a.bin");
    fs_nodes[n].mode_zero = 1;
    rc = run1("bin");
    check(rc != 0 && log_has("reason=type_unknown"),
          "種別不明 -> type_unknown で非ゼロ終了");

    /* int で表せない長さ: 黙って切り詰めない */
    setup_pair(100, 1, 1, 100, 2);
    n = fs_find("/host/bin/a.bin");
    fs_nodes[n].stat_size = 0x80000000UL;
    rc = run1("bin");
    check(rc != 0 && log_has("reason=size_unsupported"),
          "2GiB 以上 -> size_unsupported で非ゼロ終了");
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */
/*  A01 / A02 — 同サイズの更新を検出する                                      */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB
static void case_a01_a02(void)
{
    int rc;
    int n;

    printf("== A01: 同サイズで 1 バイト変更 ==\n");
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    {
        u8 *a = make_blob(100, 1);
        u8 *b = make_blob(100, 1);
        b[50] ^= 0x01;                       /* 1 バイトだけ違う */
        fs_add_file("/host/bin/a.bin", a, 100);
        fs_add_file("/bin/a.bin", b, 100);
        free(a);
        free(b);
    }
    rc = run1("bin");
    check(rc == 0, "終了コード 0");
    check(log_has("UPDATE /bin/a.bin reason=content_changed size=100"),
          "UPDATE ... reason=content_changed size=100 を出す");
    check(log_has("copied=1"), "copied=1");
    check(node_equal("/host/bin/a.bin", "/bin/a.bin"), "宛先が元と一致した");

    printf("== A02: 同サイズ・同 mtime で内容だけ変更 ==\n");
    /* 贋 stat は両側とも st_mtime=1000 を返す。mtime は同一の根拠に
     * なってはいけない。 */
    setup_pair(4096, 1, 1, 4096, 2);
    rc = run1("bin");
    check(rc == 0 && log_has("reason=content_changed"),
          "同 mtime でも content_changed でコピー");
    check(node_equal("/host/bin/a.bin", "/bin/a.bin"), "宛先が元と一致した");

    printf("== 内容が同じなら unchanged ==\n");
    setup_pair(4096, 3, 1, 4096, 3);
    rc = run2("-v", "bin");
    check(rc == 0 && log_has("unchanged=1") && log_has("copied=0"),
          "同一内容 -> unchanged=1 / copied=0");
    check(log_has("SAME /bin/a.bin size=4096"), "-v が SAME 行を出す");
    check(fk_write_calls == 0, "1 度も write していない");

    printf("== -f は比較を省くが保護・検証は省かない ==\n");
    setup_pair(4096, 3, 1, 4096, 3);
    rc = run2("-f", "bin");
    check(rc == 0 && log_has("reason=forced") && log_has("copied=1"),
          "-f -> reason=forced でコピー");
    n = fs_find("/bin/a.bin");
    check(n >= 0 && fs_nodes[n].size == 4096, "宛先の長さが保たれている");
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */
/*  A05 — 空 / 新規 / 大小変化 / バッファ境界 / 複数チャンク                   */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB
static void case_a05(void)
{
    static const u32 sizes[] = {
        0, 1, 2,
        CMP_BUF_SIZE - 1, CMP_BUF_SIZE, CMP_BUF_SIZE + 1,
        FILE_BUF_SIZE - 1, FILE_BUF_SIZE, FILE_BUF_SIZE + 1,
        2 * FILE_BUF_SIZE, 2 * FILE_BUF_SIZE + 13
    };
    char name[128];
    u32 k;

    printf("== A05: 空 / 新規 / 大小変化 / バッファ境界 / 複数チャンク ==\n");

    for (k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        u32 sz = sizes[k];

        /* (a) 同一内容 -> unchanged */
        setup_pair(sz, 9, 1, sz, 9);
        sprintf(name, "size=%lu 同一 -> unchanged", (unsigned long)sz);
        check(run1("bin") == 0 && log_has("unchanged=1") && fk_write_calls == 0,
              name);

        /* (b) 最終バイトだけ違う -> content_changed でコピー */
        if (sz > 0) {
            int n;
            setup_pair(sz, 9, 1, sz, 9);
            n = fs_find("/bin/a.bin");
            fs_nodes[n].data[sz - 1] ^= 0xFF;
            sprintf(name, "size=%lu 末尾 1 バイト差 -> copied",
                    (unsigned long)sz);
            check(run1("bin") == 0 && log_has("reason=content_changed") &&
                      node_equal("/host/bin/a.bin", "/bin/a.bin"), name);

            /* (b2) 先頭バイトだけ違う */
            setup_pair(sz, 9, 1, sz, 9);
            n = fs_find("/bin/a.bin");
            fs_nodes[n].data[0] ^= 0xFF;
            sprintf(name, "size=%lu 先頭 1 バイト差 -> copied",
                    (unsigned long)sz);
            check(run1("bin") == 0 && log_has("reason=content_changed") &&
                      node_equal("/host/bin/a.bin", "/bin/a.bin"), name);
        }

        /* (c) 宛先が無い -> new_file */
        setup_pair(sz, 9, 0, 0, 0);
        sprintf(name, "size=%lu 新規 -> new_file", (unsigned long)sz);
        check(run1("bin") == 0 && log_has("reason=new_file") &&
                  node_equal("/host/bin/a.bin", "/bin/a.bin"), name);

        /* (d) サイズが違う -> size_changed */
        setup_pair(sz, 9, 1, sz + 7, 9);
        sprintf(name, "size=%lu -> %lu -> size_changed",
                (unsigned long)sz, (unsigned long)(sz + 7));
        check(run1("bin") == 0 && log_has("reason=size_changed") &&
                  node_equal("/host/bin/a.bin", "/bin/a.bin"), name);
    }
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */
/*  A06 — 分割・エラー注入                                                    */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB
static void case_a06(void)
{
    int rc;
    int ns, nd;

    printf("== A06: short read の分割・read エラー・早期 EOF・write 異常 ==\n");

    /* 両側で **異なる** short read 分割。内容は同じなので unchanged */
    setup_pair(70000, 4, 1, 70000, 4);
    ns = fs_find("/host/bin/a.bin");
    nd = fs_find("/bin/a.bin");
    fs_nodes[ns].read_chunk = 777;
    fs_nodes[nd].read_chunk = 4096;
    rc = run1("bin");
    check(rc == 0 && log_has("unchanged=1"),
          "分割が違っても同一内容は unchanged (誤コピーしない)");

    /* 両側で異なる分割 + 内容差 -> コピー */
    setup_pair(70000, 4, 1, 70000, 4);
    ns = fs_find("/host/bin/a.bin");
    nd = fs_find("/bin/a.bin");
    fs_nodes[ns].read_chunk = 333;
    fs_nodes[nd].read_chunk = 60000;
    fs_nodes[nd].data[69999] ^= 0xFF;
    rc = run1("bin");
    check(rc == 0 && log_has("reason=content_changed"),
          "分割が違っても末尾の差を検出");

    /* 比較中の read エラー -> エラー。unchanged にも copied にもしない */
    setup_pair(70000, 4, 1, 70000, 4);
    ns = fs_find("/host/bin/a.bin");
    fs_nodes[ns].read_err_after = 1000;
    rc = run1("bin");
    check(rc != 0, "比較中の read エラー -> 非ゼロ終了");
    check(log_has("errors=1") && log_has("unchanged=0") && log_has("copied=0"),
          "unchanged/copied に入れない");

    /* 早期 EOF (stat は 70000 と言うのに 1000 で EOF) -> エラー */
    setup_pair(70000, 4, 1, 70000, 4);
    nd = fs_find("/bin/a.bin");
    fs_nodes[nd].eof_at = 1000;
    rc = run1("bin");
    check(rc != 0 && log_has("copied=0") && log_has("unchanged=0"),
          "予定サイズに達しない EOF -> エラー (同一扱いにしない)");

    /* 0 進捗 write -> 失敗。copied に入れない */
    setup_pair(5000, 4, 0, 0, 0);
    rc = run1("bin");           /* いったん作らせる */
    check(rc == 0 && log_has("copied=1"), "前提: 新規コピーは成功する");
    setup_pair(5000, 4, 1, 5000, 5);
    fk_write_zero_all = 1;
    rc = run1("bin");
    fk_write_zero_all = 0;
    check(rc != 0 && log_has("copied=0") && log_has("errors=1"),
          "0 進捗 write -> 非ゼロ終了、copied に入れない");

    /* 短い write は詰めて書き切る -> 成功 */
    setup_pair(70000, 4, 1, 70000, 5);
    nd = fs_find("/bin/a.bin");
    fs_nodes[nd].write_chunk = 101;
    rc = run1("bin");
    check(rc == 0 && log_has("copied=1") &&
              node_equal("/host/bin/a.bin", "/bin/a.bin"),
          "短い write は繰り返して書き切る");

    /* コピー中の src read エラー -> 失敗 */
    setup_pair(70000, 4, 1, 60000, 5);   /* サイズ差でコピー経路へ */
    ns = fs_find("/host/bin/a.bin");
    fs_nodes[ns].read_err_after = 5000;
    rc = run1("bin");
    check(rc != 0 && log_has("copied=0") && log_has("errors=1"),
          "コピー中の read エラー -> 非ゼロ終了");
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */
/*  A08 — CRC を差し替えてもバイト比較が不一致を検出する                       */
/* ========================================================================= */

static void case_a08(void)
{
    int rc;

    printf("== A08: CRC 核を贋物にしても同サイズの内容差を検出する ==\n");
    setup_pair(70000, 1, 1, 70000, 2);
    rc = run1("bin");
    check(rc == 0 && log_has("reason=content_changed") &&
              node_equal("/host/bin/a.bin", "/bin/a.bin"),
          "同サイズ・全内容違い -> content_changed でコピー");

    setup_pair(70000, 1, 1, 70000, 1);
    {
        int n = fs_find("/bin/a.bin");
        fs_nodes[n].data[69999] ^= 0x01;
    }
    rc = run1("bin");
    check(rc == 0 && log_has("reason=content_changed"),
          "同サイズ・末尾 1 バイト違い -> content_changed でコピー");

    setup_pair(70000, 1, 1, 70000, 1);
    rc = run1("bin");
    check(rc == 0 && log_has("unchanged=1"),
          "同一内容は unchanged のまま (贋 CRC で誤検出しない)");
}

/* ========================================================================= */
/*  A09 — コピー後の破損 / vfs_sync 失敗                                      */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB
static void case_a09(void)
{
    int rc;

    printf("== A09: 読戻し検証 ==\n");

    /* 書き込みが化ける -> 読戻しの CRC が合わない */
    setup_pair(70000, 1, 1, 70000, 2);
    fk_corrupt_write_all = 1;
    rc = run1("bin");
    fk_corrupt_write_all = 0;
    check(rc != 0, "書き込み破損 -> 非ゼロ終了");
    check(log_has("reason=verify_failed"), "reason=verify_failed を出す");
    check(log_has("copied=0") && log_has("errors=1"),
          "成功件数に入れない");
    /* **票 H2 でここが変わった**。H1 は「直接上書きなので旧内容は残らない」と
     * 告げるのが正しかったが、H2 は一時ファイルへ書いて検証してから置換する
     * ので、読戻しの失敗は**公開の前**の失敗になり旧宛先はそのまま残る。
     * 見ている規則 (読戻しの失敗を成功にしない・限界を失敗行で告げる) は
     * 変わっていない。 */
    check(log_has("公開の前なので旧宛先はそのまま"),
          "H2: 公開の前の失敗であることを失敗行で告げる");
    check(fs_nodes[fs_find("/bin/a.bin")].size == 70000,
          "H2: 旧宛先が残っている (切り詰められていない)");

    /* vfs_sync が落ちる -> verify_failed */
    setup_pair(5000, 1, 1, 5000, 2);
    fk_sync_fail = 1;
    rc = run1("bin");
    check(rc != 0 && log_has("reason=verify_failed") && log_has("copied=0"),
          "vfs_sync 失敗 -> verify_failed / 非ゼロ終了");

    /* 正常時は vfs_sync が呼ばれ、読戻しも一致する */
    setup_pair(5000, 1, 1, 5000, 2);
    rc = run1("bin");
    check(rc == 0 && fk_sync_calls >= 2 && log_has("copied=1"),
          "正常時: コピーごとの sync + 最後の sync が成功");
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */
/*  A10 — 強制モードでも保護は効く                                            */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB
static void case_a10(void)
{
    int rc;
    int n;
    u8 keep[8];

    printf("== A10: -f でも settings.db の保護は解除しない ==\n");

    /* 大文字小文字違いの実在名 */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/etc");
    fs_add_dir("/etc");
    {
        u8 *a = make_blob(64, 1);
        fs_add_file("/host/etc/SETTINGS.DB", a, 64);
        free(a);
        a = make_blob(64, 2);
        fs_add_file("/etc/SETTINGS.DB", a, 64);
        free(a);
    }
    n = fs_find("/etc/SETTINGS.DB");
    memcpy(keep, fs_nodes[n].data, 8);
    rc = run2("-f", "etc");
    check(rc == 0, "保護は失敗ではない (終了コード 0)");
    check(log_has("PROTECTED /etc/SETTINGS.DB reason=settings_db"),
          "PROTECTED 行を出す");
    n = fs_find("/etc/SETTINGS.DB");
    check(memcmp(fs_nodes[n].data, keep, 8) == 0 && fs_nodes[n].size == 64,
          "中身を書き換えていない");

    /* 祖先保護: /etc/settings.db/ の下 */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/etc");
    fs_add_dir("/host/etc/settings.db");
    fs_add_dir("/etc");
    {
        u8 *a = make_blob(8, 1);
        fs_add_file("/host/etc/settings.db/x.bin", a, 8);
        free(a);
    }
    rc = run2("-f", "etc");
    check(rc == 0 && log_has("PROTECTED /etc/settings.db reason=settings_db"),
          "祖先 (/etc/settings.db) で止まる");
    check(fs_find("/etc/settings.db") < 0, "残骸ディレクトリを作らない");

    /* 実体規則: 別名だが同じ dev/ino */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");
    fs_add_dir("/etc");
    {
        u8 *a = make_blob(64, 1);
        int real = fs_add_file("/etc/settings.db", a, 64);
        int alias;
        free(a);
        a = make_blob(64, 3);
        alias = fs_add_file("/bin/link.bin", a, 64);
        free(a);
        fs_nodes[alias].dev = fs_nodes[real].dev;
        fs_nodes[alias].ino = fs_nodes[real].ino;   /* hardlink 相当 */
        a = make_blob(64, 9);
        fs_add_file("/host/bin/link.bin", a, 64);
        free(a);
    }
    n = fs_find("/bin/link.bin");
    memcpy(keep, fs_nodes[n].data, 8);
    rc = run2("-f", "bin");
    check(rc == 0 && log_has("PROTECTED /bin/link.bin reason=settings_db"),
          "別名でも同じ実体なら保護 (実体規則)");
    n = fs_find("/bin/link.bin");
    check(memcmp(fs_nodes[n].data, keep, 8) == 0, "実体を書き換えていない");
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */
/*  A12 — 範囲・正規化・自己コピー・引数                                       */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB
static void setup_tree(void)
{
    u8 *a;
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/host/sys");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/sys");
    a = make_blob(32, 1);
    fs_add_file("/host/bin/b.bin", a, 32);
    fs_add_file("/host/sys/s.bin", a, 32);
    fs_add_file("/host/usr/sys/u.bin", a, 32);
    free(a);
}

static void case_a12(void)
{
    int rc;

    printf("== A12: 同期範囲・正規化・自己コピー・引数 ==\n");

    /* 全体同期: ルート直下の sys は除外、それ以外は同期 */
    setup_tree();
    rc = run1(0);
    check(rc == 0, "全体同期は成功");
    check(log_has("ルート直下の sys は既定で除外"),
          "既定の除外を先頭で明示する");
    check(log_has("excluded=1"), "excluded=1");
    check(fs_find("/sys/s.bin") < 0, "/sys は同期しない");
    check(fs_find("/bin/b.bin") >= 0, "/bin は同期する");
    check(fs_find("/usr/sys/u.bin") >= 0,
          "全体同期でも usr/sys は対象 (ルート直下だけを外す)");

    /* -f でも除外は解除しない */
    setup_tree();
    rc = run1("-f");
    check(rc == 0 && log_has("excluded=1") && fs_find("/sys/s.bin") < 0,
          "-f でも /sys の除外は解除しない");

    /* 明示 sys */
    setup_tree();
    rc = run1("sys");
    check(rc == 0 && fs_find("/sys/s.bin") >= 0 && log_has("excluded=0"),
          "hsync sys は /sys を同期する");
    check(log_has("NOTE: /sys を更新した"),
          "/sys 更新時にシェル再起動が要ると告げる");

    /* hsync usr の usr/sys を誤除外しない */
    setup_tree();
    rc = run1("usr");
    check(rc == 0 && fs_find("/usr/sys/u.bin") >= 0 && log_has("excluded=0"),
          "hsync usr は usr/sys を除外しない");

    /* 正規化した dir */
    setup_tree();
    rc = run1("./bin/");
    check(rc == 0 && fs_find("/bin/b.bin") >= 0,
          "'./bin/' は /bin に正規化される");
    setup_tree();
    rc = run1("usr/../bin");
    check(rc == 0 && fs_find("/bin/b.bin") >= 0 && fs_find("/usr") < 0,
          "'usr/../bin' は /bin に畳まれる");

    /* root の外へ出る dir は拒否 */
    setup_tree();
    rc = run1("../etc");
    check(rc != 0, "'../etc' は拒否 (非ゼロ終了)");
    check(fk_write_calls == 0 && fk_mkdir_calls == 0, "何も書かない");

    /* 自己コピー */
    setup_tree();
    rc = run1("host");
    check(rc != 0 && log_has("同期元"), "'host' (同期元自身) は拒否");
    setup_tree();
    rc = run1("/host/bin");
    check(rc != 0, "'/host/bin' も拒否");

    /* 引数 */
    setup_tree();
    rc = run2("bin", "sys");
    check(rc != 0 && log_has("dir は 1 つだけ"),
          "dir を 2 つ渡すとエラー (最後勝ちにしない)");
    check(fk_write_calls == 0, "何も書かない");
    setup_tree();
    rc = run1("--bogus");
    check(rc != 0 && log_has("unknown option"), "未知オプションはエラー");
    setup_tree();
    rc = run1("--help");
    check(rc == 0 && log_has("Usage: hsync"), "--help は 0 で戻る");

    /* 宛先がディレクトリ = 型衝突。勝手に消さない */
    setup_tree();
    fs_add_dir("/bin");
    fs_add_dir("/bin/b.bin");
    rc = run1("bin");
    check(rc != 0 && log_has("reason=type_conflict"),
          "宛先がディレクトリ -> type_conflict");
    check(fs_find("/bin/b.bin") >= 0, "宛先ディレクトリを消していない");
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */
/*  A13 — dry-run                                                             */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB
static void case_a13(void)
{
    int rc;
    u8 before[16];
    int n;

    printf("== A13: dry-run は 1 バイトも書かない ==\n");

    setup_tree();
    fs_add_dir("/bin");
    {
        u8 *a = make_blob(32, 7);
        fs_add_file("/bin/b.bin", a, 32);      /* 同サイズ・内容違い */
        free(a);
    }
    n = fs_find("/bin/b.bin");
    memcpy(before, fs_nodes[n].data, 16);

    rc = run2("-n", "bin");
    check(rc == 0, "dry-run は 0 で戻る");
    check(log_has("dry-run: 読み取りと比較だけ"), "dry-run の断りを出す");
    check(log_has("PLAN /bin/b.bin reason=content_changed size=32"),
          "PLAN 行で予定を出す (UPDATE とは別の語)");
    check(!log_has("UPDATE "), "UPDATE は出さない");
    check(log_has("copied=1") && log_has("(dry-run: copied は予定件数)"),
          "集計に dry-run の断りを添える");
    check(fk_write_calls == 0, "sys_write を 1 度も呼ばない");
    check(fk_mkdir_calls == 0, "sys_mkdir を 1 度も呼ばない");
    check(fk_sync_calls == 0, "vfs_sync を呼ばない");
    check(fk_create_calls == 0, "O_CREAT で作らない");
    n = fs_find("/bin/b.bin");
    check(memcmp(fs_nodes[n].data, before, 16) == 0 && fs_nodes[n].size == 32,
          "宛先の内容が変わっていない");

    /* 宛先ディレクトリが無い全体 dry-run でも mkdir しない */
    setup_tree();
    rc = run1("-n");
    check(rc == 0, "全体 dry-run も 0 で戻る");
    check(fk_mkdir_calls == 0 && fk_write_calls == 0 && fk_sync_calls == 0,
          "全体 dry-run でも書き込み系を呼ばない");
    check(fs_find("/bin") < 0 && fs_find("/usr") < 0,
          "名前空間を作らない");
    check(log_has("PLAN /bin/b.bin"), "新規予定も PLAN で出す");
    check(log_has("excluded=1"), "除外は dry-run でも数える");
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */
/*  B1 / B2 / B3 — Codex 実装レビューの blocker 3 件                          */
/* ========================================================================= */

#ifndef HSYNC_CRC_STUB

/* ---- B1: '\' を含む要素は '..' 脱出検査を素通りする ---------------------
 * OS32 の区切りは '/' だけなので `..\os32-other` は seg_len 14 の**通常の
 * 1 要素**として受理される。HostDrv の session_set_path は '/' を '\' に
 * 直したうえで他の文字をそのまま通すので、ホスト側では区切りに化けて
 * `C:\os32\..\os32-other` になり、NP21/W の境界無し前方一致を通過する。
 * hsync 側で '\' を含むパスを断つ。argv の dir と**列挙で得た名前の両方**。 */
static void case_b1(void)
{
    int rc;

    printf("== B1: '\\' を含むパスを断つ ==\n");

    /* argv の dir */
    setup_tree();
    rc = run1("..\\os32-other");
    check(rc != 0, "'..\\os32-other' は非ゼロ終了で断る");
    check(log_has("reason=bad_name"), "reason=bad_name を出す");
    check(fk_write_calls == 0 && fk_mkdir_calls == 0, "何も書かない");

    setup_tree();
    rc = run1("bin\\..\\..\\other");
    check(rc != 0 && log_has("reason=bad_name"),
          "要素の途中の '\\' も断る");

    setup_tree();
    rc = run1("/host/..\\os32-other");
    check(rc != 0, "'/host/..\\os32-other' も断る (自己コピー検査より前)");

    /* 列挙で得た名前 — ホスト側にこんな名前は無いはずだが信用しない */
    setup_tree();
    {
        u8 *a = make_blob(16, 3);
        fs_add_file("/host/bin/..\\os32-other", a, 16);
        free(a);
    }
    rc = run1("bin");
    check(rc != 0, "列挙名に '\\' があれば非ゼロ終了");
    check(log_has("reason=bad_name"), "列挙側も reason=bad_name");
    check(fs_find("/bin/..\\os32-other") < 0, "その名前を作らない");
    check(fs_find("/bin/b.bin") >= 0, "他の正常な名前は同期する");

    /* 正常系が巻き込まれていないこと */
    setup_tree();
    rc = run1("bin");
    check(rc == 0 && fs_find("/bin/b.bin") >= 0,
          "'\\' が無ければ従来どおり");
}

/* ---- B2: '/host' の 1 要素分を上限に数えていない ------------------------
 * hsp_normalize の上限 (32) は入力側にしか掛からない。32 要素の dir は
 * 通るが `/host` を足すと 33 要素になり、fs/vfs.c が**黙って末尾を捨てて**
 * 指定の親ディレクトリを列挙する。宛先側も化けるので、保護用の正規化失敗が
 * PROTECTED に畳まれて「何も同期していないのに errors=0 / 終了コード 0」に
 * なっていた。 */
static void build_deep(char *out, int segs, const char *leaf)
{
    int i;
    out[0] = '\0';
    for (i = 0; i < segs; i++) strcat(out, leaf);
}

static void case_b2(void)
{
    char dir[512];
    int rc;

    printf("== B2: '/host' を足したあとの要素数で上限を見る ==\n");

    /* 上限ちょうど: dir 30 要素 -> src ルートは /host 込み 31 要素、
     * その中のファイルが 32 要素 (= HS_MAX_PATH_DEPTH)。同期できること。 */
    fs_reset();
    fs_add_dir("/host");
    {
        char cur[512];
        int i;
        strcpy(cur, "/host");
        for (i = 0; i < 30; i++) { strcat(cur, "/a"); fs_add_dir(cur); }
        {
            u8 *a = make_blob(16, 2);
            strcat(cur, "/leaf.bin");        /* /host + 32 要素 */
            fs_add_file(cur, a, 16);
            free(a);
        }
    }
    build_deep(dir, 30, "/a");
    rc = run1(dir);
    check(!log_has("reason=path_too_deep"),
          "32 要素ちょうどは深さで断らない");
    check(rc == 0 && log_has("copied=1"), "上限ちょうどは同期できる");

    /* その 1 つ先: dir 31 要素 -> 中のファイルが 33 要素になる */
    fs_reset();
    fs_add_dir("/host");
    {
        char cur[512];
        int i;
        strcpy(cur, "/host");
        for (i = 0; i < 31; i++) { strcat(cur, "/a"); fs_add_dir(cur); }
        {
            u8 *a = make_blob(16, 2);
            strcat(cur, "/leaf.bin");        /* /host + 33 要素 */
            fs_add_file(cur, a, 16);
            free(a);
        }
    }
    build_deep(dir, 31, "/a");
    rc = run1(dir);
    check(rc != 0 && log_has("reason=path_too_deep"),
          "1 要素越えた瞬間に path_too_deep (off-by-one の境界)");
    check(fk_write_calls == 0, "越えた先へは書かない");

    /* 32 要素 -> /host を足して 33。断る */
    setup_tree();
    build_deep(dir, 32, "/a");
    rc = run1(dir);
    check(rc != 0, "32 要素 (+/host = 33) は非ゼロ終了");
    check(log_has("reason=path_too_deep"), "reason=path_too_deep を出す");
    check(!log_has("PROTECTED"), "PROTECTED とは呼ばない");
    check(fk_write_calls == 0 && fk_mkdir_calls == 0, "何も書かない");

    /* 40 要素 */
    setup_tree();
    build_deep(dir, 40, "/a");
    rc = run1(dir);
    check(rc != 0, "40 要素も非ゼロ終了");

    /* 再帰の途中で上限を越える場合も errors に数える。
     * 再帰そのものは MAX_DEPTH (8) で先に止まるので、上限へ届くのは
     * 「深い dir を明示してから 1〜2 段もぐる」経路だけ。30 要素の dir を
     * 起点にすると、その中の 1 段目 (32 要素) は通り、2 段目 (33 要素) で
     * 越える。 */
    fs_reset();
    fs_add_dir("/host");
    {
        char cur[512];
        int i;
        strcpy(cur, "/host");
        for (i = 0; i < 30; i++) { strcat(cur, "/a"); fs_add_dir(cur); }
        strcat(cur, "/b");                   /* /host + 31 要素 */
        fs_add_dir(cur);
        strcat(cur, "/c");                   /* /host + 32 要素 */
        fs_add_dir(cur);
        {
            u8 *a = make_blob(16, 2);
            strcat(cur, "/d.bin");           /* /host + 33 要素 -> 越える */
            fs_add_file(cur, a, 16);
            free(a);
        }
    }
    build_deep(dir, 30, "/a");
    rc = run1(dir);
    check(log_has("reason=path_too_deep"),
          "再帰の途中で上限を越えても path_too_deep で数える");
    check(rc != 0, "その実行は非ゼロ終了");
    check(fk_write_calls == 0, "越えた先へは書かない");

    /* 判定できないものを PROTECTED と呼ばない (語の分離)。
     * 経路としては深さ検査 / '\' 検査が先に効くので、分類そのものも直に
     * 呼んで 3 値を固定する — ここが 1 に畳まれると PROTECTED が
     * 「守った」と「読めなかった」の両方を指すことになる。 */
    setup_tree();
    rc = run1("bin\\x");
    check(rc != 0 && !log_has("PROTECTED"),
          "判定できないものを PROTECTED と呼ばない");
    {
        char deep[256];
        build_deep(deep, 33, "/a");
        check(hsp_path_classify(deep) == -1,
              "33 要素は -1 (判定できない) であって 1 ではない");
        check(hsp_path_classify("/bin/a\\b") == -1,
              "'\\' 混じりも -1");
        check(hsp_path_classify("/etc/settings.db") == 1, "保護は 1");
        check(hsp_path_classify("/bin/sh.bin") == 0, "対象外は 0");
    }

    /* PROTECTED は保護に当たったときだけ */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/etc");
    fs_add_dir("/etc");
    {
        u8 *a = make_blob(16, 1);
        fs_add_file("/host/etc/settings.db", a, 16);
        free(a);
        a = make_blob(16, 2);
        fs_add_file("/etc/settings.db", a, 16);
        free(a);
    }
    rc = run1("etc");
    check(rc == 0 && log_has("PROTECTED /etc/settings.db reason=settings_db"),
          "本当の保護は PROTECTED のまま / 終了コード 0");
}

/* ---- B3: ディレクトリ経路の型衝突 --------------------------------------
 * ext2_find_entry は種別を問わず EXIST を返すので、mkdir の EXIST を無条件に
 * 受理して再帰すると、コピー元がディレクトリ・宛先が通常ファイルのまま
 * errors=0 / 終了コード 0 で終わる。 */
static void case_b3(void)
{
    int rc;
    int n;
    u8 keep[8];

    printf("== B3: 宛先が通常ファイルなのにコピー元がディレクトリ ==\n");

    /* 空のディレクトリ (コピー元に中身が無い = 以前は何も起きずに完了) */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/empty");
    fs_add_dir("/usr");
    {
        u8 *a = make_blob(16, 5);
        fs_add_file("/usr/empty", a, 16);          /* 通常ファイル */
        free(a);
    }
    n = fs_find("/usr/empty");
    memcpy(keep, fs_nodes[n].data, 8);
    rc = run1("usr");
    check(rc != 0, "非ゼロ終了");
    check(log_has("FAIL /usr/empty reason=type_conflict"),
          "reason=type_conflict を出す");
    check(log_has("errors=1"), "errors に数える");
    n = fs_find("/usr/empty");
    check(n >= 0 && fs_nodes[n].size == 16 &&
              memcmp(fs_nodes[n].data, keep, 8) == 0,
          "宛先の通常ファイルを消しも切り詰めもしない");

    /* 中身のあるディレクトリでも同じ (中へ入らない) */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/d");
    fs_add_dir("/usr");
    {
        u8 *a = make_blob(16, 5);
        fs_add_file("/host/usr/d/inner.bin", a, 16);
        free(a);
        a = make_blob(16, 6);
        fs_add_file("/usr/d", a, 16);
        free(a);
    }
    rc = run1("usr");
    check(rc != 0 && log_has("reason=type_conflict"), "中身があっても衝突");
    check(fs_find("/usr/d/inner.bin") < 0, "衝突した先へ書き込まない");

    /* dry-run でも同じ衝突を報告する (mkdir は呼ばない) */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/empty");
    fs_add_dir("/usr");
    {
        u8 *a = make_blob(16, 5);
        fs_add_file("/usr/empty", a, 16);
        free(a);
    }
    rc = run2("-n", "usr");
    check(rc != 0, "dry-run でも非ゼロ終了");
    check(log_has("reason=type_conflict"), "dry-run でも type_conflict");
    check(fk_mkdir_calls == 0 && fk_write_calls == 0,
          "dry-run は書き込み系を呼ばない");

    /* 宛先がディレクトリなら従来どおり通る */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/d");
    fs_add_dir("/usr");
    fs_add_dir("/usr/d");
    {
        u8 *a = make_blob(16, 5);
        fs_add_file("/host/usr/d/inner.bin", a, 16);
        free(a);
    }
    rc = run1("usr");
    check(rc == 0 && fs_find("/usr/d/inner.bin") >= 0,
          "宛先がディレクトリなら同期する (EXIST を通す)");

    /* 宛先が無ければ mkdir して進む */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/d");
    {
        u8 *a = make_blob(16, 5);
        fs_add_file("/host/usr/d/inner.bin", a, 16);
        free(a);
    }
    rc = run1("usr");
    check(rc == 0 && fs_find("/usr/d/inner.bin") >= 0,
          "宛先が無ければ作って進む");
}

/* ---- B3 残件 (往復 2): 明示 dir **自身**の起点で型を見ていなかった -------
 * 列挙ループの型検査は子項目にしか掛からない。コピー元が空ディレクトリだと
 * 子が 1 つも無いので一度も呼ばれず、`hsync usr/empty` は型が食い違ったまま
 * errors=0 / 終了コード 0 で終わっていた (-n でも -f でも同じ)。 */

/* /host/usr/empty = 空ディレクトリ、/usr/empty = 通常ファイル */
static void setup_start_conflict(void)
{
    u8 *a;
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/empty");
    fs_add_dir("/usr");
    a = make_blob(16, 5);
    fs_add_file("/usr/empty", a, 16);
    free(a);
}

static void case_b3_start(void)
{
    int rc;
    int n;
    u8 keep[8];

    printf("== B3 残件: 明示 dir の起点で宛先の型を見る ==\n");

    /* 通常モード */
    setup_start_conflict();
    n = fs_find("/usr/empty");
    memcpy(keep, fs_nodes[n].data, 8);
    rc = run1("usr/empty");
    check(rc != 0, "通常モード: 非ゼロ終了");
    check(log_has("FAIL /usr/empty reason=type_conflict"),
          "通常モード: 子項目と同じ reason=type_conflict");
    check(log_has("errors=1"), "通常モード: errors=1 (同じ集計)");
    check(!log_has("Done:"), "通常モード: Done: へ進まない");
    n = fs_find("/usr/empty");
    check(n >= 0 && fs_nodes[n].size == 16 &&
              memcmp(fs_nodes[n].data, keep, 8) == 0,
          "通常モード: 宛先の通常ファイルを消しも切り詰めもしない");

    /* dry-run */
    setup_start_conflict();
    rc = run2("-n", "usr/empty");
    check(rc != 0, "dry-run: 非ゼロ終了");
    check(log_has("FAIL /usr/empty reason=type_conflict"),
          "dry-run: reason=type_conflict");
    check(log_has("errors=1"), "dry-run: errors=1");
    check(fk_write_calls == 0 && fk_mkdir_calls == 0 && fk_sync_calls == 0,
          "dry-run: 書き込み系を呼ばない");

    /* force */
    setup_start_conflict();
    rc = run2("-f", "usr/empty");
    check(rc != 0, "force: 非ゼロ終了");
    check(log_has("FAIL /usr/empty reason=type_conflict"),
          "force: reason=type_conflict (-f でも型検査は省かない)");
    check(log_has("errors=1"), "force: errors=1");
    check(fk_write_calls == 0, "force: 何も書かない");

    /* 中身のあるディレクトリでも同じ (子項目の検査に頼らない) */
    setup_start_conflict();
    {
        u8 *a = make_blob(16, 7);
        fs_add_file("/host/usr/empty/inner.bin", a, 16);
        free(a);
    }
    rc = run1("usr/empty");
    check(rc != 0 && log_has("FAIL /usr/empty reason=type_conflict"),
          "子項目があっても起点で止まる");
    check(fs_find("/usr/empty/inner.bin") < 0, "衝突した先へ入らない");

    /* 宛先がディレクトリなら従来どおり通る */
    setup_start_conflict();
    {
        int f = fs_find("/usr/empty");
        fs_nodes[f].is_dir = 1;                 /* 宛先をディレクトリに */
        fs_nodes[f].size = 0;
    }
    {
        u8 *a = make_blob(16, 7);
        fs_add_file("/host/usr/empty/inner.bin", a, 16);
        free(a);
    }
    rc = run1("usr/empty");
    check(rc == 0 && fs_find("/usr/empty/inner.bin") >= 0,
          "宛先がディレクトリなら同期する");

    /* 宛先が無ければそのまま進む */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/empty");
    {
        u8 *a = make_blob(16, 7);
        fs_add_file("/host/usr/empty/inner.bin", a, 16);
        free(a);
    }
    rc = run1("usr/empty");
    check(rc == 0 && fs_find("/usr/empty/inner.bin") >= 0,
          "宛先が無ければ作って進む");

    /* 空ディレクトリ同士 = 何もすることが無い。成功のまま */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/empty");
    fs_add_dir("/usr");
    fs_add_dir("/usr/empty");
    rc = run1("usr/empty");
    check(rc == 0 && log_has("errors=0"),
          "空ディレクトリ同士は成功 (誤検出しない)");

    printf("== B3 残件: 明示 dir の起点でコピー元の型も見る ==\n");

    /* コピー元が通常ファイル。sys_ls 任せにしない */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    {
        u8 *a = make_blob(16, 5);
        fs_add_file("/host/usr/file.txt", a, 16);
        free(a);
    }
    rc = run1("usr/file.txt");
    check(rc != 0, "コピー元が通常ファイル: 非ゼロ終了");
    check(log_has("FAIL /host/usr/file.txt reason=type_conflict"),
          "コピー元が通常ファイル: reason=type_conflict (ls の err 任せにしない)");
    check(log_has("errors=1"), "コピー元が通常ファイル: errors=1");

    /* コピー元が無い */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    rc = run1("usr/nope");
    check(rc != 0 && log_has("reason=io_error"),
          "コピー元が無い: reason=io_error で非ゼロ終了");

    /* コピー元の種別が取れない */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/usr");
    fs_add_dir("/host/usr/d");
    {
        int f = fs_find("/host/usr/d");
        fs_nodes[f].mode_zero = 1;
    }
    rc = run1("usr/d");
    check(rc != 0 && log_has("reason=type_unknown"),
          "コピー元の種別不明: reason=type_unknown");
}

/* ---- B4 (往復 3): 途中で切れた列挙を hsync が成功にしない ----------------
 * fs/hostdrvfs.c の hdrv_list_dir は、`hostdrv_query_dir` が途中で失敗しても
 * 末尾で無条件に VFS_OK を返していた。200 件のうち 50 件目で失敗すると
 * 「50 件だけの成功した列挙」になり、hsync は rc == 0 を見て 50 件を同期し
 * errors=0 / 終了コード 0 で終わる。件数上限 (1000) での打ち切りも同じ。
 *
 * ループ側の修正は tools/tests/hostdrv_list_host.c が持つ。ここでは
 * **呼び手の意味** — 列挙が非ゼロを返したら hsync が部分同期を成功と
 * 呼ばないこと — を固定する。 */
static void case_b4(void)
{
    int rc;

    printf("== B4: 途中で切れた列挙を成功にしない (呼び手側) ==\n");

    /* 5 件のうち 2 件流したところで列挙が失敗する */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    {
        u8 *a = make_blob(16, 1);
        fs_add_file("/host/bin/a1.bin", a, 16);
        fs_add_file("/host/bin/a2.bin", a, 16);
        fs_add_file("/host/bin/a3.bin", a, 16);
        fs_add_file("/host/bin/a4.bin", a, 16);
        fs_add_file("/host/bin/a5.bin", a, 16);
        free(a);
    }
    fk_ls_fail_after = 2;
    rc = run1("bin");
    check(rc != 0, "非ゼロ終了 (部分的な列挙を成功にしない)");
    check(log_has("FAIL: ls /host/bin"), "ls の失敗を報告する");
    check(log_has("errors=1"), "errors に数える");
    check(!log_has("Done:"), "Done: へ進まない");
    check(log_has("copied=0"),
          "1 件も同期しない (途中まで写して「完了」と言わない)");

    /* 列挙が成功する側 — 誤検出していないこと */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    {
        u8 *a = make_blob(16, 1);
        fs_add_file("/host/bin/a1.bin", a, 16);
        fs_add_file("/host/bin/a2.bin", a, 16);
        free(a);
    }
    rc = run1("bin");
    check(rc == 0 && log_has("copied=2") && log_has("errors=0"),
          "列挙が最後まで届けば従来どおり同期する");

    /* dry-run でも同じ — 「変更なし」と報告しない */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    {
        u8 *a = make_blob(16, 1);
        fs_add_file("/host/bin/a1.bin", a, 16);
        fs_add_file("/host/bin/a2.bin", a, 16);
        fs_add_file("/host/bin/a3.bin", a, 16);
        free(a);
    }
    fk_ls_fail_after = 1;
    rc = run2("-n", "bin");
    check(rc != 0 && log_has("errors=1"),
          "dry-run でも列挙の失敗を errors に数える");

    /* 全体同期の途中でサブディレクトリの列挙が切れた場合 */
    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    {
        u8 *a = make_blob(16, 1);
        fs_add_file("/host/bin/a1.bin", a, 16);
        fs_add_file("/host/bin/a2.bin", a, 16);
        free(a);
    }
    fk_ls_fail_after = 1;             /* ルートの列挙自体が 1 件で切れる */
    rc = run1(0);
    check(rc != 0 && log_has("errors=1"),
          "全体同期でも列挙の失敗で非ゼロ終了");
}
#endif /* !HSYNC_CRC_STUB */

/* ========================================================================= */

int main(void)
{
    fake_api_init();

#ifdef HSYNC_CRC_STUB
    printf("=== hsync H1 ホスト TDD (CRC 核を贋物に差し替え / A08) ===\n");
    case_a08();
#else
    printf("=== hsync H1 ホスト TDD ===\n");
    case_a07();
    case_a11_rules();
    case_a11_hsync();
    case_a01_a02();
    case_a05();
    case_a06();
    case_a08();
    case_a09();
    case_a10();
    case_a12();
    case_a13();
    case_b1();
    case_b2();
    case_b3();
    case_b3_start();
    case_b4();
#endif

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
