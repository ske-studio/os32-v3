/* ========================================================================
 *  cd_pkg_host.c — cdinst の無圧縮の展開 (userland/lib/rt/pkg.c の
 *                  pkg_extract) がどう区切って読むかの試験
 *
 *  実行:  python3 -B tools/tests/test_cd_read.py   (cd_read_host.c と組で回す)
 *  記録:  tools/tests/cd_read_tdd.md
 *
 *  実物の pkg.c を #include し、KAPI だけを贋物にする。PKG は試験がメモリに
 *  組む (データ部の先頭はセクタに揃っていない)。sys_read の (位置, 長さ) を
 *  記録し、書き出されたファイルの中身を突き合わせる。記録は CD_PKG_TRACE の
 *  ファイルへ "offset length" で書き、test_cd_read.py が cd_read_host の
 *  replay へ渡す (実物の iso9660 + atapi で READ(10) を数える)。
 *
 *  PkgHeader の u32 は unsigned long なので 64 ビットのホストでは形が違うが、
 *  pkg_extract はヘッダをバイト列から読まない (PkgInfo を試験が組む) ので
 *  ここでは効かない。
 *
 *  [C1] C89 / GNU89。
 * ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "userland/lib/rt/pkg.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)

#define PKG_FD     3
#define OUT_FD     7
#define DATA_OFF   1234u      /* 表の後ろ。セクタ (2048) に揃っていない */
#define MAX_READS  100000

static const u32 g_sizes[] = { 0, 1, 5000, 100000, 70000, 2048, 33000,
                               777, 262144, 4095, 4097, 65536, 12345 };
#define N_FILES (sizeof(g_sizes) / sizeof(g_sizes[0]))

static PkgInfo g_info;
static u8 *g_pkg;          /* PKG ファイル全体 */
static u32 g_pkg_len;
static u32 g_pos;          /* PKG の読み位置 */
static int g_pkg_open;

static int g_nomem;
static void *g_alloc_ptr;
static int g_alloc_n, g_free_n;

static u32 g_read_off[MAX_READS], g_read_len[MAX_READS];
static u32 g_nreads;

static int g_cur_file = -1;   /* OUT_FD が指す項目 */
static u32 g_cur_written;
static u32 g_file_start[N_FILES + 1];  /* 各ファイルのデータ部の中の位置 */
static int g_closed_ok[N_FILES + 1];

static u8 data_byte(u32 pos) { return (u8)((pos * 131u) ^ (pos >> 7) ^ 0x5Au); }

static int __cdecl f_open(const char *path, int mode)
{
    int i;
    if (strcmp(path, "/cd0/X.PKG") == 0) {
        CHECK(!g_pkg_open);
        g_pkg_open = 1;
        g_pos = 0;
        return PKG_FD;
    }
    CHECK(mode & KAPI_O_WRONLY);
    CHECK(g_cur_file < 0);
    for (i = 0; i < g_info.entry_count; i++) {
        if (strcmp(path, g_info.entries[i].path) == 0) {
            g_cur_file = i;
            g_cur_written = 0;
            return OUT_FD;
        }
    }
    CHECK(0);
    return -1;
}

static void __cdecl f_close(int fd)
{
    if (fd == PKG_FD) { g_pkg_open = 0; return; }
    CHECK(fd == OUT_FD && g_cur_file >= 0);
    if (g_cur_written == g_info.entries[g_cur_file].size) g_closed_ok[g_cur_file] = 1;
    g_cur_file = -1;
}

static int __cdecl f_read(int fd, void *buf, u32 size)
{
    u32 n;
    CHECK(fd == PKG_FD);
    CHECK(g_nreads < MAX_READS);
    n = (g_pos < g_pkg_len) ? g_pkg_len - g_pos : 0;
    if (n > size) n = size;
    g_read_off[g_nreads] = g_pos;
    g_read_len[g_nreads] = size;
    g_nreads++;
    memcpy(buf, g_pkg + g_pos, n);
    g_pos += n;
    return (int)n;
}

static int __cdecl f_write(int fd, const void *buf, u32 size)
{
    const u8 *b = (const u8 *)buf;
    u32 i, base;
    CHECK(fd == OUT_FD && g_cur_file >= 0);
    base = g_info.data_offset + g_file_start[g_cur_file] + g_cur_written;
    CHECK(g_cur_written + size <= g_info.entries[g_cur_file].size);
    for (i = 0; i < size; i++) CHECK(b[i] == data_byte(base + i));
    g_cur_written += size;
    return (int)size;
}

static int __cdecl f_lseek(int fd, int off, int whence)
{
    CHECK(fd == PKG_FD && whence == SEEK_SET);
    g_pos = (u32)off;
    return off;
}

static int __cdecl f_mkdir(const char *p) { (void)p; return 0; }
static void *__cdecl f_mem_alloc(u32 n)
{
    if (g_nomem) return (void *)0;
    g_alloc_n++;
    g_alloc_ptr = malloc((size_t)n);
    return g_alloc_ptr;
}
static void __cdecl f_mem_free(void *p)
{
    CHECK(p == g_alloc_ptr);
    g_free_n++;
    free(p);
}
static void __cdecl f_kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

static KernelAPI g_api;

static void build(void)
{
    u32 i, sum = 0;
    memset(&g_info, 0, sizeof(g_info));
    /* 項目 0 はディレクトリ、1.. がファイル */
    g_info.entries[0].type = PKG_TYPE_DIR;
    strcpy(g_info.entries[0].path, "/hd0/d");
    for (i = 0; i < N_FILES; i++) {
        PkgEntry *e = &g_info.entries[i + 1];
        e->type = PKG_TYPE_FILE;
        e->size = g_sizes[i];
        sprintf(e->path, "/hd0/d/f%02u", (unsigned)i);
        g_file_start[i + 1] = sum;
        sum += g_sizes[i];
    }
    g_info.entry_count = (int)N_FILES + 1;
    g_info.header.orig_size = sum;
    g_info.header.comp_size = sum;
    g_info.header.flags = 0;
    g_info.data_offset = DATA_OFF;
    g_pkg_len = DATA_OFF + sum;
    g_pkg = (u8 *)malloc(g_pkg_len);
    for (i = 0; i < g_pkg_len; i++) g_pkg[i] = data_byte(i);

    memset(&g_api, 0, sizeof(g_api));
    g_api.sys_open = f_open;
    g_api.sys_close = f_close;
    g_api.sys_read = f_read;
    g_api.sys_write = f_write;
    g_api.sys_lseek = f_lseek;
    g_api.sys_mkdir = f_mkdir;
    g_api.mem_alloc = f_mem_alloc;
    g_api.mem_free = f_mem_free;
    g_api.kprintf = f_kprintf;
}

static void run_extract(void)
{
    int i;
    CHECK(pkg_extract(&g_api, "/cd0/X.PKG", &g_info) == PKG_OK);
    CHECK(!g_pkg_open);
    for (i = 1; i < g_info.entry_count; i++) CHECK(g_closed_ok[i]);
}

/* 読みの区切りは 32KB 以下、終わりはセクタ境界かファイルの終わり */
static void t_aligned_chunks(void)
{
    u32 i, sum = 0, ends_mid = 0;
    build();
    run_extract();
    CHECK(g_alloc_n == 1 && g_free_n == 1);
    for (i = 0; i < g_nreads; i++) {
        u32 end = g_read_off[i] + g_read_len[i];
        int file_end = 0;
        u32 k;
        CHECK(g_read_len[i] <= PKG_STREAM_CHUNK);
        for (k = 1; k <= N_FILES; k++)
            if (end == DATA_OFF + g_file_start[k] + g_info.entries[k].size) file_end = 1;
        if (end % PKG_STREAM_ALIGN != 0) {
            CHECK(file_end);
            ends_mid++;
        }
        sum += g_read_len[i];
    }
    CHECK(sum == g_info.header.comp_size);
    printf("aligned_chunks: reads=%lu bytes=%lu ends_at_file_end=%lu pkg_len=%lu\n",
           (unsigned long)g_nreads, (unsigned long)sum, (unsigned long)ends_mid,
           (unsigned long)g_pkg_len);
    /* 32KB に揃えた区切り: 全体 / 32KB + ファイルの数 程度 */
    CHECK(g_nreads <= sum / PKG_STREAM_CHUNK + 2u * N_FILES);
    {
        const char *tp = getenv("CD_PKG_TRACE");
        if (tp) {
            FILE *f = fopen(tp, "w");
            CHECK(f != NULL);
            /* replay の相手は PKG と同じ大きさの BIG.PKG (pkg_len を見よ) */
            for (i = 0; i < g_nreads; i++)
                fprintf(f, "%lu %lu\n", (unsigned long)g_read_off[i],
                        (unsigned long)g_read_len[i]);
            fclose(f);
        }
    }
}

/* 32KB が取れなければ 4KB のスタックのバッファで同じ結果 */
static void t_nomem(void)
{
    u32 i;
    build();
    g_nomem = 1;
    run_extract();
    CHECK(g_alloc_n == 0 && g_free_n == 0);
    for (i = 0; i < g_nreads; i++) CHECK(g_read_len[i] <= PKG_STREAM_SMALL);
}

/* 書き込みに失敗したら PKG_ERR_IO、確保したバッファは返す */
static int __cdecl f_open_fail(const char *path, int mode)
{
    if (strcmp(path, "/cd0/X.PKG") == 0) return f_open(path, mode);
    return -1;
}

static void t_open_fail_frees(void)
{
    build();
    g_api.sys_open = f_open_fail;
    CHECK(pkg_extract(&g_api, "/cd0/X.PKG", &g_info) == PKG_ERR_IO);
    CHECK(!g_pkg_open);
    CHECK(g_alloc_n == 1 && g_free_n == 1);
}

int main(int argc, char **argv)
{
    const char *cs = (argc > 1) ? argv[1] : "";
    if (!strcmp(cs, "aligned_chunks"))        t_aligned_chunks();
    else if (!strcmp(cs, "nomem"))            t_nomem();
    else if (!strcmp(cs, "open_fail_frees"))  t_open_fail_frees();
    else { fprintf(stderr, "unknown case '%s'\n", cs); return 2; }
    return 0;
}
