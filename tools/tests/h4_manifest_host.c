/* =========================================================================
 *  H4_MANIFEST_HOST.C — 票 H4 (配備マニフェストと世代の確認) の
 *                       **読む側** を実物のソースで確かめる
 *
 *  対象票: docs/archive/shell/TASK_H4.md §2-1 / §2-3 / §2-3-1 / §4-1
 *          (M5 M6 M6b M6c M7 M8 M9 M9b M10 M11 M12)
 *  実行:   python3 -B tools/tests/test_h4_manifest.py [--target] [--mutate]
 *  記録:   tools/tests/h4_manifest_tdd.md
 *
 *  H1 / H2 / H3 と同じ作法で **userland/system/hsync.c を 1 行も写さず
 *  そのまま #include する**。差し替えるのは KernelAPI だけ — オンメモリの
 *  贋ファイルシステムに向け、`/host/.deploy/manifest.txt` に任意の本文を
 *  置けるようにしてある。
 *
 *  **この票の一番大事な規則**:
 *    (1) 名札が無い配備元でも**今までどおり動く** (後方互換)。
 *        → 票 TASK_KAPI_DATA_FIELDS (KAPI v63) で**変わった**: 名札の kapi= で
 *          配備物の KAPI 配置を確かめられないなら既定で断る (`--force-kapi`
 *          で越える)。H4 の規則を見る既存の段は `--force-kapi` を付けて回し
 *          (g_inject_force_kapi)、KAPI の門そのものは case_kapi が見る。
 *    (2) 壊れた名札は**捨てる**。ただし断るのは `--expect-build` かつ
 *        **全体同期**のときだけ。絞り込みでは表示して続ける。
 *    (3) **名札が読めないことを「一致」と扱わない** (往復 1 所見 2)。
 *    (4) **名札を信じて内容比較を省かない** — CRC が一致していても読み比べる。
 *    (5) 集計は `manifest_extra` だけ。`manifest_missing` は別票 (§2-4)。
 *
 *  **偽の緑を踏まないための観測窓**: 「1 件も書かない」は文言ではなく
 *  `fk_write_calls` / `fk_rename_calls` が 0 であることで見る。「内容比較が
 *  走った」は `g_content_compares` で、「表を引いた」は `g_man_lookups` で
 *  数える。表示は `fk_log` の中身で見る。
 *
 *  エミュレータ・実配備・make には一切触れない。
 *  [C1] C89 / GNU89。宣言はブロック先頭、`//` コメント無し。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "os32api.h"      /* KernelAPI / OS32_Stat / OS32_ERR_* / u8,u32 */

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

#define FS_MAX_NODES 1024
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
    u32  mtime;
    u16  nlink;
} FNode;

typedef struct {
    int used;
    int node;
    u32 pos;
} FFd;

static FNode fs_nodes[FS_MAX_NODES];
static FFd   fs_fds[FS_MAX_FDS];
static u32   fs_next_ino;

/* ---- 観測窓 (偽の緑を踏まないための数) ---- */
static int fk_write_calls;
static int fk_read_calls;
static int fk_rename_calls;
static int fk_mkdir_calls;

static char  fk_log[262144];
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
    fk_rename_calls = 0;
    fk_mkdir_calls = 0;
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
        fs_nodes[i].nlink = 1;
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

static int fs_add_dir(const char *path) { return fs_new(path, 1); }

static void fs_drop(int n)
{
    if (n < 0) return;
    if (fs_nodes[n].data) free(fs_nodes[n].data);
    memset(&fs_nodes[n], 0, sizeof(FNode));
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
    buf->st_dev = fs_nodes[n].dev;
    buf->st_ino = fs_nodes[n].ino;
    buf->st_nlink = fs_nodes[n].nlink;
    buf->st_mode = (u16)(fs_nodes[n].is_dir ? (OS_S_IFDIR | 0755)
                                            : (OS_S_IFREG | 0644));
    buf->st_size = fs_nodes[n].size;
    buf->st_mtime = fs_nodes[n].mtime;
    return 0;
}

static int fk_sys_open(const char *path, int mode)
{
    int n = fs_find(path);
    int f;

    if (mode & KAPI_O_EXCL) {
        if (!(mode & KAPI_O_CREAT)) return OS32_ERR_INVAL;
        if (n >= 0) return OS32_ERR_EXIST;
        n = fs_add_file(path, 0, 0);
    } else if (n < 0) {
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
    nd->mtime = 555555;
    return (int)size;
}

static int fk_sys_set_mtime(const char *path, u32 mtime)
{
    int n;
    if (mtime == 0) return OS32_ERR_INVAL;
    n = fs_find(path);
    if (n < 0) return OS32_ERR_NOTFOUND;
    fs_nodes[n].mtime = mtime;
    return 0;
}

static int fk_sys_unlink(const char *path)
{
    int n = fs_find(path);
    if (n < 0) return OS32_ERR_NOTFOUND;
    if (fs_nodes[n].is_dir) return OS32_ERR_ISDIR;
    fs_drop(n);
    return 0;
}

static int fk_sys_rename(const char *oldpath, const char *newpath)
{
    int src_n = fs_find(oldpath);
    int dst_n;

    fk_rename_calls++;
    if (src_n < 0) return OS32_ERR_NOTFOUND;
    dst_n = fs_find(newpath);
    if (dst_n >= 0) fs_drop(dst_n);
    strncpy(fs_nodes[src_n].path, newpath, FS_PATH_CAP - 1);
    fs_nodes[src_n].path[FS_PATH_CAP - 1] = '\0';
    return 0;
}

static KernelAPI g_fake;

static void fake_api_init(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.version = 53;
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
    g_fake.sys_set_mtime = fk_sys_set_mtime;
    g_fake.sys_unlink = fk_sys_unlink;
    g_fake.sys_rename = fk_sys_rename;
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

/* 1 = 引数の末尾に `--force-kapi` を足す (H4 の規則を見る既存の段)。
 * KAPI の門そのものを見る case_kapi だけ 0 にする。 */
static int g_inject_force_kapi = 1;

static int run_hsync(int argc, char **argv)
{
    char *av[16];
    int i;

    fk_log_len = 0;
    fk_log[0] = '\0';
    fk_write_calls = 0;
    fk_read_calls = 0;
    fk_rename_calls = 0;
    fk_mkdir_calls = 0;
    for (i = 0; i < argc && i < 14; i++) av[i] = argv[i];
    if (g_inject_force_kapi) av[i++] = (char *)"--force-kapi";
    av[i] = 0;
    return hsync_main(i, av, &g_fake);
}

static int run0(void)
{
    char *av[2];
    av[0] = (char *)"hsync";
    av[1] = 0;
    return run_hsync(1, av);
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

static int run3(const char *a1, const char *a2, const char *a3)
{
    char *av[5];
    av[0] = (char *)"hsync";
    av[1] = (char *)a1;
    av[2] = (char *)a2;
    av[3] = (char *)a3;
    av[4] = 0;
    return run_hsync(4, av);
}

static int log_has(const char *needle) { return strstr(fk_log, needle) != 0; }

/* 票 §2-3 / 受入 4-2 の 1: 世代は**1 行目**に出る。log_has だけだと、
 * どこか後ろに紛れていても緑になってしまう。 */
static int log_starts_with(const char *needle)
{
    return strncmp(fk_log, needle, strlen(needle)) == 0;
}

static u8 *make_blob(u32 size, u32 seed)
{
    u8 *p = (u8 *)malloc(size ? size : 1);
    u32 i;
    for (i = 0; i < size; i++)
        p[i] = (u8)((i * 31u + seed * 7u + (i >> 8)) & 0xFF);
    return p;
}

static u32 blob_crc(const u8 *p, u32 n)
{
    return crc32_core_final(crc32_core_update(CRC32_INIT, p, n));
}

/* **1 件も書いていない**ことを文言ではなく呼び出し回数で見る (観測窓) */
static int wrote_nothing(void)
{
    return fk_write_calls == 0 && fk_rename_calls == 0 && fk_mkdir_calls == 0;
}

/* ---- 足場: /host に 2 ファイル、/ 側は空 (= 全部 new_file) ---- */

/* 名札の KAPI 行 (票 TASK_KAPI_DATA_FIELDS)。値は v63 の配置 0x4B8 = 1208 と
 * 版 63 — main() の先頭で KAPI_DATA_FIELDS_OFF / KAPI_VERSION と突き合わせる。 */
#define KL "kapi=1208\nkapi_version=63\n"

#define MAN_DIR  "/host/.deploy"
#define MAN_PATH "/host/.deploy/manifest.txt"

static u32 g_a_size, g_b_size;
static u32 g_a_crc, g_b_crc;

static void setup_tree(void)
{
    u8 *a;
    int n;

    fs_reset();
    fs_add_dir("/host");
    fs_add_dir("/host/bin");
    fs_add_dir("/bin");

    g_a_size = 5000;
    a = make_blob(g_a_size, 1);
    g_a_crc = blob_crc(a, g_a_size);
    n = fs_add_file("/host/bin/a.bin", a, g_a_size);
    fs_nodes[n].mtime = 111;
    free(a);

    g_b_size = 3000;
    a = make_blob(g_b_size, 2);
    g_b_crc = blob_crc(a, g_b_size);
    n = fs_add_file("/host/bin/b.bin", a, g_b_size);
    fs_nodes[n].mtime = 222;
    free(a);
}

/* 宛先 /bin/a.bin を**コピー元と同じ内容・同じサイズ・違う日時**で置く。
 * 「名札の CRC が宛先と一致していても内容比較を省かない」(M10) を見るため。 */
static void put_same_dst_a(void)
{
    u8 *a = make_blob(g_a_size, 1);
    int n = fs_add_file("/bin/a.bin", a, g_a_size);
    fs_nodes[n].mtime = 999;          /* 日時が違う = 内容比較の候補になる */
    free(a);
}

static void put_manifest(const char *text)
{
    if (fs_find(MAN_DIR) < 0) fs_add_dir(MAN_DIR);
    fs_add_file(MAN_PATH, (const u8 *)text, (u32)strlen(text));
}

/* 正しい名札を組み立てる。build を差し替えられる。 */
static void put_good_manifest(const char *build)
{
    char buf[4096];
    sprintf(buf,
            "format=2\n"
            "build=%s\n"
            "generated=2026-09-16T21:45:19Z\n"
            KL
            "count=2\n"
            "---\n"
            "bin/a.bin %lu %08lx 111\n"
            "bin/b.bin %lu %08lx 222\n",
            build,
            (unsigned long)g_a_size, (unsigned long)g_a_crc,
            (unsigned long)g_b_size, (unsigned long)g_b_crc);
    put_manifest(buf);
}

/* ========================================================================= */
/*  M5 — 名札が無い配備元は今までどおり (後方互換)                            */
/* ========================================================================= */

static void case_m5(void)
{
    printf("== M5: 名札が無い配備元 -> 今までどおり動く ==\n");
    setup_tree();
    check(run0() == 0, "同期は成功する");
    check(!log_has("DEPLOY"), "**DEPLOY 行を出さない** (名札が無いのだから)");
    check(log_has("copied=2"), "2 件コピーした (今までどおり)");
    check(!log_has("manifest_extra"),
          "名札が無いので manifest_extra は出さない");
    check(fs_find("/bin/a.bin") >= 0 && fs_find("/bin/b.bin") >= 0,
          "宛先に両方そろう");
    check(g_man_present == 0 && g_man_valid == 0, "名札は無いと記録される");
}

/* ========================================================================= */
/*  M6 / M6b / M6c / M9b / M12 — 壊れた名札                                   */
/* ========================================================================= */

/* 壊し方 1 件分。`--expect-build` 無し (M6) と 有り (M6b) を両方見る。 */
static void broken_case(const char *label, const char *text,
                        const char *want_reason)
{
    char name[256];

    /* --- M6: --expect-build 無し -> 捨てて表示、同期は続ける --- */
    setup_tree();
    put_manifest(text);
    sprintf(name, "M6 [%s] 同期は続く (終了コード 0)", label);
    check(run0() == 0, name);
    sprintf(name, "M6 [%s] manifest invalid を 1 行目に表示する", label);
    check(log_starts_with("DEPLOY manifest invalid:"), name);
    if (want_reason) {
        sprintf(name, "M6 [%s] 理由が %s", label, want_reason);
        check(log_has(want_reason), name);
    }
    sprintf(name, "M6 [%s] 名札を捨てる (表は空)", label);
    check(g_man_valid == 0 && g_man_count == 0, name);
    /* 名札そのものも同期対象なので 3 件 (a, b, .deploy/manifest.txt)。
     * 除外の規則は足していない — H1 / H2 / H3 の規則を変えない。 */
    sprintf(name, "M6 [%s] 3 件コピーした (作業は止めない)", label);
    check(log_has("copied=3"), name);

    /* --- M6b: --expect-build 有り + 全体同期 -> 1 件も書かずに断る --- */
    setup_tree();
    put_manifest(text);
    sprintf(name, "M6b [%s] 非ゼロ終了", label);
    check(run2("--expect-build", "deadbee") != 0, name);
    sprintf(name, "M6b [%s] reason=manifest_invalid", label);
    check(log_has("reason=manifest_invalid"), name);
    sprintf(name, "M6b [%s] **1 件も書かない** (write/rename/mkdir が 0 回)",
            label);
    check(wrote_nothing(), name);
    sprintf(name, "M6b [%s] 宛先に何も現れない", label);
    check(fs_find("/bin/a.bin") < 0 && fs_find("/bin/b.bin") < 0, name);

    /* --- M12: --expect-build 有り + 範囲を絞った同期 -> 表示して続ける --- */
    setup_tree();
    put_manifest(text);
    sprintf(name, "M12 [%s] 絞り込みなら成功する", label);
    check(run3("--expect-build", "deadbee", "bin") == 0, name);
    sprintf(name, "M12 [%s] manifest invalid は表示する", label);
    check(log_has("DEPLOY manifest invalid:"), name);
    sprintf(name, "M12 [%s] reason=manifest_invalid では断らない", label);
    check(!log_has("reason=manifest_invalid"), name);
    sprintf(name, "M12 [%s] 確かめられないことは黙らせない", label);
    check(log_has("NOTE: 配備元の世代を確かめられない"), name);
    sprintf(name, "M12 [%s] 絞った範囲は同期される", label);
    check(fs_find("/bin/a.bin") >= 0, name);
}

static void case_m6(void)
{
    char buf[8192];
    char big[65536];
    int i;
    int at;

    printf("== M6 / M6b / M12: 壊れた名札 ==\n");

    broken_case("形式版が違う",
                "format=3\nbuild=x\ngenerated=g\n" KL "count=0\n---\n", "format");
    broken_case("--- が無い",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=0\n", "separator");
    broken_case("count が行数と合わない",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=2\n---\n"
                "bin/a.bin 1 00000000 1\n", "count");
    broken_case("count より行が多い",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "bin/a.bin 1 00000000 1\nbin/b.bin 1 00000000 1\n", "count");
    broken_case("重複するパス",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=2\n---\n"
                "bin/a.bin 1 00000000 1\nbin/a.bin 2 00000000 2\n",
                "duplicate");
    broken_case("絶対パス",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "/bin/a.bin 1 00000000 1\n", "bad path");
    broken_case("'..' を含むパス",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "../etc/passwd 1 00000000 1\n", "bad path");
    broken_case("'.' を含むパス",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "bin/./a.bin 1 00000000 1\n", "bad path");
    broken_case("'\\' を含むパス",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "bin\\a.bin 1 00000000 1\n", "bad path");
    broken_case("CRC の桁が足りない",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "bin/a.bin 1 abc 1\n", "bad crc");
    broken_case("CRC が大文字",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "bin/a.bin 1 ABCDEF01 1\n", "bad crc");
    broken_case("サイズが数字でない",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "bin/a.bin xx 00000000 1\n", "bad size");
    broken_case("mtime が数字でない",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "bin/a.bin 1 00000000 zz\n", "bad mtime");
    broken_case("区切りが空白 2 つ",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "bin/a.bin  1 00000000 1\n", "field");
    broken_case("欄が 5 つ",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n"
                "bin/a.bin 1 00000000 1 extra\n", "field");
    broken_case("build が無い",
                "format=2\ngenerated=g\n" KL "count=0\n---\n", "missing");
    broken_case("知らない鍵",
                "format=2\nbuild=x\ngenerated=g\n" KL "count=0\nweird=1\n---\n",
                "unknown key");
    /* 票 TASK_KAPI_DATA_FIELDS: format=2 は kapi= / kapi_version= が必須 */
    broken_case("kapi が無い",
                "format=2\nbuild=x\ngenerated=g\nkapi_version=63\ncount=0\n---\n",
                "missing");
    broken_case("kapi_version が無い",
                "format=2\nbuild=x\ngenerated=g\nkapi=1208\ncount=0\n---\n",
                "missing");
    broken_case("kapi が数字でない",
                "format=2\nbuild=x\ngenerated=g\nkapi=0x4b8\nkapi_version=63\n"
                "count=0\n---\n", "bad kapi");
    broken_case("kapi が 0",
                "format=2\nbuild=x\ngenerated=g\nkapi=0\nkapi_version=63\n"
                "count=0\n---\n", "bad kapi");
    broken_case("kapi が重複",
                "format=2\nbuild=x\ngenerated=g\n" KL "kapi=1208\ncount=0\n---\n",
                "duplicate");
    broken_case("旧形式 format=1 (kapi を持たない)",
                "format=1\nbuild=x\ngenerated=g\ncount=0\n---\n", "format");
    broken_case("build に空白",
                "format=2\nbuild=a b\ngenerated=g\n" KL "count=0\n---\n", "build");

    /* 長すぎるパス (HS_MAN_PATH_CAP 超) */
    at = sprintf(buf, "format=2\nbuild=x\ngenerated=g\n" KL "count=1\n---\n");
    for (i = 0; i < HS_MAN_PATH_CAP + 4; i++) buf[at++] = 'p';
    at += sprintf(buf + at, " 1 00000000 1\n");
    buf[at] = '\0';
    broken_case("長すぎるパス", buf, "path too long");

    /* M9b: 名札が大きすぎる (表に収まらない) */
    printf("== M9b: 名札が大きすぎる -> 名札ごと捨てる ==\n");
    at = sprintf(big, "format=2\nbuild=x\ngenerated=g\n" KL "count=%d\n---\n",
                 HS_MAN_MAX + 1);
    for (i = 0; i < HS_MAN_MAX + 1; i++)
        at += sprintf(big + at, "bin/f%d.bin 1 00000000 1\n", i);
    big[at] = '\0';
    broken_case("件数が上限超え", big, "too many entries");

    /* M6c: 名札が**無い**のに --expect-build。
     *
     * 扱いは「壊れている」場合と**同じ** (PM 決裁 2026-09-16)。どちらも
     * 「確かめられない」であって、名札はルートの世代を表すものだから、
     * 絞った範囲の正しさは保証しない。全体同期では断り、絞り込みでは
     * 表示だけして続ける。 */
    printf("== M6c: 名札が無い配備元に --expect-build ==\n");
    setup_tree();
    check(run2("--expect-build", "deadbee") != 0, "全体同期では非ゼロ終了");
    check(log_has("reason=manifest_absent"),
          "reason=manifest_absent (壊れている場合と語を分ける)");
    check(!log_has("reason=manifest_invalid"),
          "無いものを「壊れている」とは言わない");
    check(wrote_nothing(), "**1 件も書かない**");
    check(fs_find("/bin/a.bin") < 0, "宛先に何も現れない");

    printf("== M6c': 名札が無い + 絞り込み -> 表示のみ・続行 ==\n");
    setup_tree();
    check(run3("--expect-build", "deadbee", "bin") == 0,
          "絞り込みでは成功する (名札はルートの世代であって範囲を保証しない)");
    check(!log_has("reason=manifest_absent"), "断らない");
    check(!log_has("reason=manifest_invalid"), "断らない");
    check(log_has("NOTE: 配備元の世代を確かめられない"),
          "確かめられないことは**黙らせない**");
    check(fs_find("/bin/a.bin") >= 0 && fs_find("/bin/b.bin") >= 0,
          "**同期が走る** (書き込み 0 件ではない)");
    check(fk_write_calls > 0, "実際に書いた (観測窓)");

    /* 名札が読めて**不一致**なら、絞り込みでも断る。
     * **「確かめた結果おかしい」と「確かめられない」は別**。 */
    printf("== M8''': 不一致は絞り込みでも断る ==\n");
    setup_tree();
    put_good_manifest("OLDBUILD");
    check(run3("--expect-build", "NEWBUILD", "bin") != 0,
          "絞り込みでも不一致は断る (protection の本体)");
    check(log_has("reason=build_mismatch"), "reason=build_mismatch");
    check(wrote_nothing(), "**1 件も書かない**");
}

/* ========================================================================= */
/*  M7 / M8 / M11 — --expect-build の照合                                     */
/* ========================================================================= */

static void case_m7_m8(void)
{
    printf("== M7 / M8 / M11: --expect-build の照合 ==\n");

    /* M7: 一致 -> 同期する */
    setup_tree();
    put_good_manifest("9742a6b+dirty");
    check(run2("--expect-build", "9742a6b+dirty") == 0, "M7 一致なら同期する");
    check(log_starts_with("DEPLOY build=9742a6b+dirty"),
          "M7 世代を**1 行目**に表示する (受入 4-2 の 1)");
    check(log_has("count=2"), "M7 count も表示する");
    check(log_has("generated=2026-09-16T21:45:19Z"), "M7 generated も表示する");
    check(fs_find("/bin/a.bin") >= 0, "M7 宛先に現れる");

    /* M8: 不一致 -> 1 件も書かずに非ゼロ終了 */
    setup_tree();
    put_good_manifest("9742a6b+dirty");
    check(run2("--expect-build", "3ce9b70") != 0, "M8 非ゼロ終了");
    check(log_has("reason=build_mismatch"), "M8 reason=build_mismatch");
    check(wrote_nothing(), "M8 **1 件も書かない**");
    check(fs_find("/bin/a.bin") < 0, "M8 宛先に何も現れない");

    /* M8': 大小を比べない (文字列の完全一致) */
    setup_tree();
    put_good_manifest("9742A6B");
    check(run2("--expect-build", "9742a6b") != 0,
          "M8' 大文字小文字違いは**一致ではない** (完全一致で見る)");
    check(log_has("reason=build_mismatch"), "M8' reason=build_mismatch");

    /* M8'': 前方一致でも一致にしない */
    setup_tree();
    put_good_manifest("9742a6b+dirty");
    check(run2("--expect-build", "9742a6b") != 0,
          "M8'' 前方一致は一致ではない (+dirty は別の名札)");

    /* M11: 全件コピー成功のあと名札を書く前に中断 = 名札が古いまま。
     *      新しい ID で照合すると build_mismatch。**安全側の壊れ方** (§2-3-1)。 */
    setup_tree();
    put_good_manifest("OLDBUILD");
    check(run2("--expect-build", "NEWBUILD") != 0, "M11 非ゼロ終了");
    check(log_has("reason=build_mismatch"),
          "M11 reason=build_mismatch (§2-3-1 の想定どおり)");
    check(wrote_nothing(), "M11 **1 件も書かない** (ファイルが新しくても)");

    /* 既定 (--expect-build 無し) は表示だけして続ける。
     * **勝手に拒否しない** — 意図した巻き戻しもある。 */
    setup_tree();
    put_good_manifest("OLDBUILD");
    check(run0() == 0, "既定は世代が何であれ同期する (意図した巻き戻しを許す)");
    check(log_has("DEPLOY build=OLDBUILD"), "既定でも世代は表示する");
    check(!log_has("reason=build_mismatch"), "既定では断らない");

    /* --expect-build に値が無い */
    setup_tree();
    check(run1("--expect-build") != 0, "値の無い --expect-build は断る");
    check(wrote_nothing(), "値の無い --expect-build は 1 件も書かない");
}

/* ========================================================================= */
/*  M9 — manifest_extra                                                       */
/* ========================================================================= */

static void case_m9(void)
{
    u8 *c;
    int n;

    printf("== M9: 名札に無いものを写したら manifest_extra に数える ==\n");

    /* 名札に載っている 2 件だけを写す -> extra は 0 */
    setup_tree();
    put_good_manifest("B1");
    check(run0() == 0, "同期は成功する");
    check(log_has("manifest_extra=0"),
          "名札どおりなら manifest_extra=0");
    check(g_man_lookups == 3,
          "**表を引いた回数** = 写した数 (a, b, 名札そのもの) = 3");

    /* 名札に無い 1 件を足す -> extra=1 */
    setup_tree();
    put_good_manifest("B1");
    c = make_blob(700, 3);
    n = fs_add_file("/host/bin/c.bin", c, 700);
    fs_nodes[n].mtime = 333;
    free(c);
    check(run0() == 0, "同期は成功する");
    check(log_has("manifest_extra=1"), "名札に無い 1 件を数える");
    check(log_has("copied=4"),
          "コピー自体は 4 件 (a, b, c, 名札そのもの。数えるだけで止めない)");
    check(fs_find("/bin/c.bin") >= 0, "名札に無くても写す (拒否はしない)");

    /* 名札そのものの写しは extra に数えない (鶏と卵) */
    setup_tree();
    put_good_manifest("B1");
    check(run0() == 0, "同期は成功する");
    check(fs_find("/.deploy/manifest.txt") >= 0,
          "名札そのものも同期される (除外の規則は足していない)");
    check(log_has("manifest_extra=0"),
          "**名札自身の写しは extra に数えない** (名札は自分を載せられない)");

    /* 名札が無い / 壊れているときは数えない (数えようがない) */
    setup_tree();
    check(run0() == 0, "名札なしでも成功する");
    check(g_man_extra == 0 && g_man_lookups == 0,
          "名札が無ければ表を引かない");
}

/* ========================================================================= */
/*  M10 — 名札を信じて内容比較を省かない                                      */
/* ========================================================================= */

static void case_m10(void)
{
    printf("== M10: 名札の CRC が宛先と一致していても内容比較を省かない ==\n");

    /* 宛先は**コピー元と同じ内容**で、名札の CRC とも一致する。日時だけ違う
     * ので H3 の前置フィルタは内容比較へ回す。ここで名札を信じて省くと
     * g_content_compares が増えない。 */
    setup_tree();
    put_same_dst_a();
    put_good_manifest("B1");
    check(run0() == 0, "同期は成功する");
    check(g_content_compares >= 1,
          "**内容比較が走った** (名札の CRC で省いていない)");
    check(log_has("MTIME /bin/a.bin"),
          "内容は同じなので mtime だけ直す (H3 の判定順は変えていない)");

    /* 宛先の中身だけ違う (サイズは同じ) -> 名札の CRC はコピー元と一致する
     * のに、宛先は違う。内容比較が無ければ見逃す。 */
    {
        u8 *bad = make_blob(g_a_size, 77);
        int n;
        setup_tree();
        put_good_manifest("B1");
        n = fs_add_file("/bin/a.bin", bad, g_a_size);
        fs_nodes[n].mtime = 111;        /* サイズも日時も同じ */
        free(bad);
        check(run1("--verify") == 0, "--verify なら全件の内容を比較する");
        check(g_content_compares >= 1, "内容比較が走った");
        check(log_has("reason=content_changed"),
              "**名札の CRC が合っていても中身の違いを見つける**");
    }

    /* 名札が無くても内容比較の回数は変わらない (名札は判定に効かない) */
    {
        int with_man, without_man;
        setup_tree();
        put_same_dst_a();
        put_good_manifest("B1");
        run0();
        with_man = g_content_compares;
        setup_tree();
        put_same_dst_a();
        run0();
        without_man = g_content_compares;
        check(with_man == without_man,
              "名札の有無で内容比較の回数が変わらない (判定に混ぜていない)");
    }
}

/* ========================================================================= */
/*  H1 / H2 / H3 の規則を変えていないこと                                     */
/* ========================================================================= */

static void case_regression(void)
{
    printf("== R: H1 / H2 / H3 の規則を変えていない ==\n");

    /* 予約名 `.hs~` の掃除は名札に当たらない (`.deploy/` の下なので) */
    setup_tree();
    put_good_manifest("B1");
    check(run0() == 0, "同期は成功する");
    check(fs_find(MAN_PATH) >= 0, "配備元の名札を消さない");
    check(!log_has("cleaned=1"), "名札は予約名の掃除に当たらない");

    /* 判定順: サイズ -> mtime 前置 -> 内容比較 */
    setup_tree();
    put_good_manifest("B1");
    put_same_dst_a();
    {
        int n = fs_find("/bin/a.bin");
        fs_nodes[n].mtime = 111;      /* サイズも日時も同じ */
        check(run0() == 0, "同期は成功する");
        check(log_has("unchanged=1"),
              "サイズも日時も同じなら読まずに省く (H3 の前置フィルタ)");
    }

    /* 既定の /sys 除外は残っている */
    setup_tree();
    fs_add_dir("/host/sys");
    put_good_manifest("B1");
    check(run0() == 0, "同期は成功する");
    check(log_has("excluded=1"), "ルート直下の sys は既定で除外 (H1)");

    /* 古いカーネルの門 (H2) より名札の照合が先に立たないこと:
     * v52 では名札を読む前に kernel_too_old で断る。 */
    setup_tree();
    put_good_manifest("B1");
    g_fake.version = 52;
    check(run2("--expect-build", "B1") != 0, "KAPI v52 は既定で断る");
    check(log_has("reason=kernel_too_old"),
          "名札が一致していても kernel_too_old が先 (H2 の門を弱めない)");
    check(wrote_nothing(), "1 件も書かない");
    g_fake.version = 53;

    /* dry-run は 1 バイトも書かない (名札を読んでも) */
    setup_tree();
    put_good_manifest("B1");
    check(run2("-n", "--expect-build") != 0 || 1, "(足場)");
    setup_tree();
    put_good_manifest("B1");
    check(run1("-n") == 0, "dry-run は成功する");
    check(log_starts_with("DEPLOY build=B1"),
          "dry-run でも世代を 1 行目に表示する");
    check(wrote_nothing(), "dry-run は 1 バイトも書かない");
}

/* ========================================================================= */
/*  上限そのもの                                                              */
/* ========================================================================= */

static void case_limits(void)
{
    char *big;
    int at;
    int i;

    printf("== 上限: ちょうど HS_MAN_MAX 件は通る ==\n");
    big = (char *)malloc(HS_MAN_MAX * 64 + 4096);
    at = sprintf(big, "format=2\nbuild=B1\ngenerated=g\n" KL "count=%d\n---\n",
                 HS_MAN_MAX);
    for (i = 0; i < HS_MAN_MAX; i++)
        at += sprintf(big + at, "bin/f%d.bin 1 00000000 1\n", i);
    big[at] = '\0';

    setup_tree();
    put_manifest(big);
    check(run0() == 0, "同期は成功する");
    check(g_man_valid == 1, "ちょうど上限なら名札は使える");
    check(g_man_count == HS_MAN_MAX, "全件が表に入る");
    free(big);

    printf("== 上限: 実配備 (約 200 件) に余裕がある ==\n");
    check(HS_MAN_MAX >= 256,
          "HS_MAN_MAX は実測の配備件数 (約 200) に余裕をもつ");
    check(HS_MAN_PATH_CAP == NAME_CAP,
          "パスの上限は NAME_CAP に揃える (票 §2-3)");
}

/* ========================================================================= */
/*  K — KAPI の門 (票 TASK_KAPI_DATA_FIELDS、KAPI v63)                        */
/*                                                                           */
/*  名札の kapi= (配備物のデータ欄の配置) がカーネルと違う / kapi_version が  */
/*  カーネルより新しい / 名札が無い・壊れている → **1 件も書かずに断る**。    */
/*  絞り込み (`hsync sys`) でも断る。ただし `/boot` だけの同期は「版が新しい」*/
/*  だけを外す (カーネルを先)。`--force-kapi` だけが越える (`-f` は            */
/*  別の意味の旗なので越えない)。                                             */
/* ========================================================================= */

static void put_kapi_manifest(unsigned long off, unsigned long ver)
{
    char buf[4096];
    sprintf(buf,
            "format=2\n"
            "build=K\n"
            "generated=g\n"
            "kapi=%lu\n"
            "kapi_version=%lu\n"
            "count=2\n"
            "---\n"
            "bin/a.bin %lu %08lx 111\n"
            "bin/b.bin %lu %08lx 222\n",
            off, ver,
            (unsigned long)g_a_size, (unsigned long)g_a_crc,
            (unsigned long)g_b_size, (unsigned long)g_b_crc);
    put_manifest(buf);
}

/* /host/boot/vmkernel.lz4 を足した木 (K3b〜K3d: `hsync boot`) */
static u32 g_k_size;
static u32 g_k_crc;

static void setup_boot_tree(void)
{
    u8 *k;
    int n;

    setup_tree();
    fs_add_dir("/host/boot");
    fs_add_dir("/boot");
    g_k_size = 4000;
    k = make_blob(g_k_size, 3);
    g_k_crc = blob_crc(k, g_k_size);
    n = fs_add_file("/host/boot/vmkernel.lz4", k, g_k_size);
    fs_nodes[n].mtime = 333;
    free(k);
}

static void put_kapi_manifest_boot(unsigned long off, unsigned long ver)
{
    char buf[4096];
    sprintf(buf,
            "format=2\n"
            "build=K\n"
            "generated=g\n"
            "kapi=%lu\n"
            "kapi_version=%lu\n"
            "count=3\n"
            "---\n"
            "bin/a.bin %lu %08lx 111\n"
            "bin/b.bin %lu %08lx 222\n"
            "boot/vmkernel.lz4 %lu %08lx 333\n",
            off, ver,
            (unsigned long)g_a_size, (unsigned long)g_a_crc,
            (unsigned long)g_b_size, (unsigned long)g_b_crc,
            (unsigned long)g_k_size, (unsigned long)g_k_crc);
    put_manifest(buf);
}

static void kapi_refused(const char *label, const char *reason)
{
    char name[256];
    sprintf(name, "%s: 理由を表示する", label);
    check(log_has(reason), name);
    sprintf(name, "%s: **1 件も書かない**", label);
    check(wrote_nothing(), name);
    sprintf(name, "%s: 宛先に何も現れない", label);
    check(fs_find("/bin/a.bin") < 0 && fs_find("/bin/b.bin") < 0, name);
}

static void case_kapi(void)
{
    const char *why = 0;
    u32 saved_ver = g_fake.version;
    unsigned long off = (unsigned long)KAPI_DATA_FIELDS_OFF;
    unsigned long ver = (unsigned long)KAPI_VERSION;

    printf("== K: KAPI の門 (票 TASK_KAPI_DATA_FIELDS) ==\n");
    g_inject_force_kapi = 0;
    g_fake.version = KAPI_VERSION;

    /* K0: 試験の KL が v63 の配置と一致している (ずれたら全段が嘘になる) */
    check(KAPI_DATA_FIELDS_OFF == 1208, "K0 KL の kapi=1208 は KAPI_DATA_FIELDS_OFF");
    check(KAPI_VERSION >= 63, "K0 KL の kapi_version=63 はこの SDK 以下");

    /* K1: 一致 → 同期する */
    setup_tree();
    put_kapi_manifest(off, ver);
    check(run0() == 0, "K1 配置・版が一致すれば同期する");
    check(!log_has("reason=kapi") && !log_has("KAPI reason"), "K1 KAPI の門で断らない");
    check(log_has("kapi=1208/v"), "K1 DEPLOY 行に kapi を出す");
    check(fs_find("/bin/a.bin") >= 0, "K1 宛先に届く");

    /* K1b: 配備物の版がカーネルより古い → 通す (v63 以降は配置が固定) */
    setup_tree();
    put_kapi_manifest(off, 63);
    g_fake.version = 70;
    check(run0() == 0, "K1b 配備物の版 < カーネルの版は通す (カーネルを先に配備した形)");
    g_fake.version = KAPI_VERSION;

    /* K2: 配置違い → 全体でも絞り込みでも断る */
    setup_tree();
    put_kapi_manifest(off - 4, ver);
    check(run0() != 0, "K2 配置違いは非ゼロ終了");
    kapi_refused("K2 reason=kapi_layout_mismatch", "reason=kapi_layout_mismatch");
    setup_tree();
    put_kapi_manifest(off - 4, ver);
    check(run1("bin") != 0, "K2b 絞り込み (bin) でも断る");
    kapi_refused("K2b reason=kapi_layout_mismatch", "reason=kapi_layout_mismatch");

    /* K3: 配備物の版 > カーネルの版 → 断る (v64 以降はカーネルを先) */
    setup_tree();
    put_kapi_manifest(off, ver + 1);
    check(run0() != 0, "K3 版がカーネルより新しいなら非ゼロ終了");
    kapi_refused("K3 reason=kapi_newer_than_kernel", "reason=kapi_newer_than_kernel");

    /* K3b: `/boot` だけの同期は「版が新しい」から外す (v64 以降のカーネルを
     * 先に運ぶ HostDrv 経路 — Codex 実装レビュー R1 blocker 2) */
    setup_boot_tree();
    put_kapi_manifest_boot(off, ver + 1);
    check(run1("boot") == 0, "K3b hsync boot は版が新しくても通す (カーネルを先)");
    check(!log_has("reason=kapi"), "K3b KAPI の門で断らない");
    check(log_has("NOTE: /boot"), "K3b 版が新しいまま進むことを表示する");
    check(fs_find("/boot/vmkernel.lz4") >= 0, "K3b カーネルが届く");
    check(fs_find("/bin/a.bin") < 0, "K3b ユーザーランドには触れない");
    setup_boot_tree();
    put_kapi_manifest_boot(off, ver + 1);
    check(run1("/boot/") == 0, "K3b 正規化後に /boot なら同じ (\"/boot/\")");
    /* K3c: /boot でも配置違い・名札の欠落は断る */
    setup_boot_tree();
    put_kapi_manifest_boot(off - 4, ver + 1);
    check(run1("boot") != 0, "K3c hsync boot でも配置違いは断る");
    kapi_refused("K3c reason=kapi_layout_mismatch", "reason=kapi_layout_mismatch");
    check(fs_find("/boot/vmkernel.lz4") < 0, "K3c カーネルも書かない");
    setup_boot_tree();
    check(run1("boot") != 0, "K3c hsync boot でも名札が無ければ断る");
    check(log_has("reason=manifest_absent"), "K3c reason=manifest_absent");
    check(fs_find("/boot/vmkernel.lz4") < 0, "K3c 名札なしでカーネルを書かない");
    /* K3d: /boot 以外の絞り込み (bin、/bootx) は版が新しければ断る */
    setup_boot_tree();
    put_kapi_manifest_boot(off, ver + 1);
    check(run1("bin") != 0, "K3d hsync bin は版が新しければ断る");
    kapi_refused("K3d reason=kapi_newer_than_kernel", "reason=kapi_newer_than_kernel");
    setup_boot_tree();
    put_kapi_manifest_boot(off, ver + 1);
    check(run1("bootx") != 0 && log_has("reason=kapi_newer_than_kernel"),
          "K3d /bootx は /boot ではない (接頭辞は要素単位)");
    setup_boot_tree();
    put_kapi_manifest_boot(off, ver + 1);
    check(run0() != 0 && log_has("reason=kapi_newer_than_kernel"),
          "K3d 全体同期は版が新しければ断る");
    check(fs_find("/boot/vmkernel.lz4") < 0, "K3d 全体同期でカーネルも書かない");

    /* K4: 名札が無い → 確かめられない = 一致と扱わない */
    setup_tree();
    check(run0() != 0, "K4 名札が無ければ断る");
    kapi_refused("K4 reason=manifest_absent", "reason=manifest_absent");

    /* K5: 旧形式 (format=1、kapi が無い) → 壊れた名札と同じ扱いで断る */
    setup_tree();
    put_manifest("format=1\nbuild=x\ngenerated=g\ncount=0\n---\n");
    check(run1("bin") != 0, "K5 旧形式の名札は絞り込みでも断る");
    kapi_refused("K5 reason=manifest_invalid", "reason=manifest_invalid");

    /* K6: -f / --force は KAPI の門を開けない */
    setup_tree();
    put_kapi_manifest(off - 4, ver);
    check(run1("-f") != 0, "K6 -f では越えない");
    kapi_refused("K6 reason=kapi_layout_mismatch", "reason=kapi_layout_mismatch");

    /* K7: --force-kapi だけが越える。理由は黙らせない */
    setup_tree();
    put_kapi_manifest(off - 4, ver);
    check(run1("--force-kapi") == 0, "K7 --force-kapi なら続ける");
    check(log_has("NOTE: KAPI (kapi_layout_mismatch)"), "K7 越えた理由を表示する");
    check(fs_find("/bin/a.bin") >= 0, "K7 宛先に届く");
    setup_tree();
    check(run1("--force-kapi") == 0, "K7b 名札が無くても --force-kapi なら続ける");
    check(log_has("NOTE: KAPI (manifest_absent)"), "K7b 理由を表示する");

    /* K8: 判定関数そのもの */
    g_man_present = 1; g_man_valid = 1;
    g_man_kapi = 1208; g_man_kapi_ver = 63;
    check(man_kapi_check(1208, 63, 0, &why) == 0 && why == 0, "K8 一致 → 通す");
    check(man_kapi_check(1204, 63, 0, &why) == 1 && why && strcmp(why, HR_KAPI_LAYOUT) == 0, "K8 配置違い → 断る");
    check(man_kapi_check(1208, 62, 0, &why) == 1 && why && strcmp(why, HR_KAPI_NEWER) == 0, "K8 版が新しい → 断る");
    check(man_kapi_check(1208, 64, 0, &why) == 0, "K8 版が古い → 通す");
    check(man_kapi_check(1208, 62, 1, &why) == 0 && why == 0, "K8 /boot だけなら版が新しくても通す");
    check(man_kapi_check(1204, 62, 1, &why) == 1 && why && strcmp(why, HR_KAPI_LAYOUT) == 0, "K8 /boot でも配置違い → 断る");
    g_man_valid = 0;
    check(man_kapi_check(1208, 63, 0, &why) == 1 && why && strcmp(why, HR_MANIFEST_INVALID) == 0, "K8 壊れた名札 → 断る");
    check(man_kapi_check(1208, 63, 1, &why) == 1 && why && strcmp(why, HR_MANIFEST_INVALID) == 0, "K8 /boot でも壊れた名札 → 断る");
    g_man_present = 0;
    check(man_kapi_check(1208, 63, 0, &why) == 1 && why && strcmp(why, HR_MANIFEST_ABSENT) == 0, "K8 名札なし → 断る");
    check(man_kapi_check(1208, 63, 1, &why) == 1 && why && strcmp(why, HR_MANIFEST_ABSENT) == 0, "K8 /boot でも名札なし → 断る");

    g_fake.version = saved_ver;
    g_inject_force_kapi = 1;
}

int main(void)
{
    printf("=== 票 H4: 配備マニフェストと世代の確認 (読む側) ===\n");
    fake_api_init();

    case_m5();
    case_m6();
    case_m7_m8();
    case_m9();
    case_m10();
    case_regression();
    case_limits();
    case_kapi();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

/* Caller tests mock enumeration; real packet iteration is check-ls-client-host. */
int os32_ls(const char *path, DirCallback cb, void *ctx)
{
    return (&g_fake)->sys_ls(path, (void *)cb, ctx);
}
