/* ======================================================================== */
/*  HCLIP.C — ホスト (WSL2) のクリップボードを読み書きする (票 N3 §2)        */
/*                                                                          */
/*  Usage: hclip get         クリップボードを標準出力へ                       */
/*         hclip put <file>  ファイル (1〜4096B) をクリップボードへ           */
/*                                                                          */
/*  終了コード: 0 成功 / 1 業務失敗 (ESERVICE = 503) / 2 リンク /            */
/*              3 usage・空・4096B 超 / 4 ローカル I/O。                     */
/*  ホスト TDD は -DHOST_TEST (tools/tests/host_cmd_host.c)。                 */
/* ======================================================================== */

#ifdef HOST_TEST
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include "os32api.h"
#include <stdio.h>
#include <string.h>
#endif
#include "libos32host.h"

/* 終了コード ([C4]) */
#define HCLIP_OK      0
#define HCLIP_BIZ     1
#define HCLIP_LINK    2
#define HCLIP_USAGE   3
#define HCLIP_LOCAL   4

/* --- 前方宣言 (main を先頭関数に保つ) --------------------------------- */
#ifndef HOST_TEST
static KernelAPI *g_api;
#endif
static int io_open_read(const char *path);
static int io_read(int fd, void *buf, unsigned cap);
static int io_write(int fd, const void *buf, unsigned n);
static void io_close(int fd);
static int hclip_sink(void *ud, const u8 *buf, u32 n);
static int do_get(void);
static int do_put(const char *path);
static void usage(void);

typedef struct { int ioerr; } HclipSink;

int main(int argc, char **argv
#ifndef HOST_TEST
         , KernelAPI *api
#endif
         )
{
#ifndef HOST_TEST
    g_api = api;
#endif

    if (argc == 2 && strcmp(argv[1], "get") == 0)
        return do_get();
    if (argc == 3 && strcmp(argv[1], "put") == 0)
        return do_put(argv[2]);

    usage();
    return HCLIP_USAGE;
}

static int do_get(void)
{
    HclipSink s;
    u32 nb = 0, svc = 0;
    int rc;

    s.ioerr = 0;
    rc = host_clip_get(hclip_sink, &s, &nb, &svc);

    if (s.ioerr || rc == HOST_EABORT) { printf("hclip: write error\n"); return HCLIP_LOCAL; }
    if (rc == HOST_ESERVICE) { printf("hclip: clipboard unavailable (%u)\n", (unsigned)svc); return HCLIP_BIZ; }
    if (rc == HOST_ELINK) { printf("hclip: host link down\n"); return HCLIP_LINK; }
    if (rc == HOST_ENODEV) { printf("hclip: no host link (no NIC)\n"); return HCLIP_LINK; }
    if (rc == HOST_ETIMEOUT) { printf("hclip: host timeout\n"); return HCLIP_LINK; }
    if (rc != 0) { printf("hclip: host error\n"); return HCLIP_LINK; }
    return HCLIP_OK;
}

static int do_put(const char *path)
{
    static char buf[HOST_CLIP_MAX];
    u32 total = 0, svc = 0;
    int fd, rc, too_large = 0;

    fd = io_open_read(path);
    if (fd < 0) { printf("hclip: cannot open %s\n", path); return HCLIP_LOCAL; }

    for (;;) {
        int n;
        if (total >= HOST_CLIP_MAX) {
            unsigned char extra;
            int m = io_read(fd, &extra, 1);
            if (m > 0) too_large = 1;
            break;
        }
        n = io_read(fd, buf + total, (unsigned)(HOST_CLIP_MAX - total));
        if (n < 0) { io_close(fd); printf("hclip: read error\n"); return HCLIP_LOCAL; }
        if (n == 0) break;
        total += (u32)n;
    }
    io_close(fd);

    if (too_large) { printf("hclip: file too large (max %d)\n", HOST_CLIP_MAX); return HCLIP_USAGE; }

    rc = host_clip_put(buf, total, &svc);
    if (rc == HOST_EINVAL) { printf("hclip: nothing to put (empty)\n"); return HCLIP_USAGE; }
    if (rc == HOST_ESERVICE) { printf("hclip: clipboard rejected (%u)\n", (unsigned)svc); return HCLIP_BIZ; }
    if (rc == HOST_ELINK) { printf("hclip: host link down\n"); return HCLIP_LINK; }
    if (rc == HOST_ENODEV) { printf("hclip: no host link (no NIC)\n"); return HCLIP_LINK; }
    if (rc == HOST_ETIMEOUT) { printf("hclip: host timeout\n"); return HCLIP_LINK; }
    if (rc != 0) { printf("hclip: host error\n"); return HCLIP_LINK; }

    printf("%u bytes\n", (unsigned)total);
    return HCLIP_OK;
}

/* sink — クリップボード本文を標準出力へ */
static int hclip_sink(void *ud, const u8 *buf, u32 n)
{
    HclipSink *s = (HclipSink *)ud;
    u32 off = 0;
    while (off < n) {
        int wr = io_write(1, buf + off, (unsigned)(n - off));
        if (wr <= 0) { s->ioerr = 1; return 1; }
        off += (u32)wr;
    }
    return 0;
}

static void usage(void)
{
    printf("Usage: hclip get | hclip put <file>\n");
}

/* ======================================================================== */
/*  低レベル I/O — ゲストは KAPI、ホスト試験は POSIX                        */
/* ======================================================================== */
static int io_open_read(const char *path)
{
#ifdef HOST_TEST
    return open(path, O_RDONLY);
#else
    return g_api->sys_open(path, KAPI_O_RDONLY);
#endif
}

static int io_read(int fd, void *buf, unsigned cap)
{
#ifdef HOST_TEST
    return (int)read(fd, buf, (size_t)cap);
#else
    return g_api->sys_read(fd, buf, (u32)cap);
#endif
}

static int io_write(int fd, const void *buf, unsigned n)
{
#ifdef HOST_TEST
    return (int)write(fd, buf, (size_t)n);
#else
    return g_api->sys_write(fd, buf, (u32)n);
#endif
}

static void io_close(int fd)
{
#ifdef HOST_TEST
    close(fd);
#else
    g_api->sys_close(fd);
#endif
}
