/* ======================================================================== */
/*  WGET.C — ホスト経由で URL を取得する (票 N3 §2)                          */
/*                                                                          */
/*  Usage: wget <url> [file]                                                */
/*    file 指定 → sys_open(O_CREAT|O_TRUNC) に書く。無ければ stdout。         */
/*    http_status を確定してからファイルを作る (404 で空ファイルを残さない)。 */
/*    非 200 の本文は捨て、`wget: <status>` + 終了 1。                        */
/*                                                                          */
/*  終了コード: 0 成功 / 1 業務 (非 200) / 2 リンク / 3 usage・url 長 /       */
/*              4 ローカル I/O。                                             */
/*  本文は host_get の sink でストリーミング (>64KB でメモリ一定)。           */
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
#define WGET_OK      0
#define WGET_BIZ     1
#define WGET_LINK    2
#define WGET_USAGE   3
#define WGET_LOCAL   4

/* --- 前方宣言 (main を先頭関数に保つ。変数定義は関数ではない) ---------- */
#ifndef HOST_TEST
static KernelAPI *g_api;
#endif
static int io_open_write(const char *path);
static int io_write(int fd, const void *buf, unsigned n);
static void io_close(int fd);
static int wget_sink(void *ud, const u8 *buf, u32 n);
static void usage(void);

typedef struct {
    int http_status;   /* host_get が read ループ前に書く */
    int have_file;
    const char *path;
    int fd;
    int opened;
    int ioerr;
} WgetSink;

int main(int argc, char **argv
#ifndef HOST_TEST
         , KernelAPI *api
#endif
         )
{
    const char *url;
    const char *file;
    WgetSink w;
    u32 nb = 0;
    int rc;

#ifndef HOST_TEST
    g_api = api;
#endif

    if (argc < 2 || argc > 3) {
        usage();
        return WGET_USAGE;
    }
    url = argv[1];
    file = (argc == 3) ? argv[2] : (const char *)0;

    w.http_status = 0;
    w.have_file = (file != (const char *)0);
    w.path = file;
    w.fd = -1;
    w.opened = 0;
    w.ioerr = 0;

    rc = host_get(url, wget_sink, &w, &w.http_status, &nb);

    if (w.ioerr || rc == HOST_EABORT) {
        if (w.opened) io_close(w.fd);
        printf("wget: write error\n");
        return WGET_LOCAL;
    }
    if (rc == HOST_EINVAL) {
        printf("wget: url too long\n");
        return WGET_USAGE;
    }
    if (rc == HOST_ELINK) { printf("wget: host link down\n"); return WGET_LINK; }
    if (rc == HOST_ENODEV) { printf("wget: no host link (no NIC)\n"); return WGET_LINK; }
    if (rc == HOST_ETIMEOUT) { printf("wget: host timeout\n"); return WGET_LINK; }
    if (rc != 0) { printf("wget: host error\n"); return WGET_LINK; }

    if (w.http_status != 200) {
        if (w.opened) io_close(w.fd);
        printf("wget: %d\n", w.http_status);
        return WGET_BIZ;
    }

    /* 200 だが本文 0 バイト = sink 未呼び出し。空ファイルは作る (404 とは別)。 */
    if (w.have_file && !w.opened) {
        w.fd = io_open_write(w.path);
        if (w.fd < 0) {
            printf("wget: cannot create %s\n", w.path);
            return WGET_LOCAL;
        }
        w.opened = 1;
    }
    if (w.opened) io_close(w.fd);
    printf("%u bytes\n", (unsigned)nb);
    return WGET_OK;
}

/* ======================================================================== */
/*  sink — http_status を見て 200 のときだけ書く                             */
/* ======================================================================== */
static int wget_sink(void *ud, const u8 *buf, u32 n)
{
    WgetSink *w = (WgetSink *)ud;
    u32 off;

    if (w->http_status != 200) return 0;   /* 非 200 は捨てる (中断はしない) */

    if (w->have_file && !w->opened) {
        w->fd = io_open_write(w->path);
        if (w->fd < 0) { w->ioerr = 1; return 1; }
        w->opened = 1;
    }
    off = 0;
    while (off < n) {
        int wr = io_write(w->have_file ? w->fd : 1, buf + off, (unsigned)(n - off));
        if (wr <= 0) { w->ioerr = 1; return 1; }
        off += (u32)wr;
    }
    return 0;
}

static void usage(void)
{
    printf("Usage: wget <url> [file]\n");
}

/* ======================================================================== */
/*  低レベル I/O — ゲストは KAPI、ホスト試験は POSIX                        */
/* ======================================================================== */
static int io_open_write(const char *path)
{
#ifdef HOST_TEST
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
#else
    return g_api->sys_open(path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
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
