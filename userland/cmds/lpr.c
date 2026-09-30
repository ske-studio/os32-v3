/* ======================================================================== */
/*  LPR.C — ファイル / 標準入力をホストのプリンタへ送る (票 N3 §2)           */
/*                                                                          */
/*  Usage: lpr <file>   ファイルを印刷                                       */
/*         lpr -        標準入力を印刷                                        */
/*                                                                          */
/*  本文は host_print_stream の src でストリーミング (大きいファイルでも      */
/*  メモリ一定)。ジョブ名は basename の空白・制御文字を '_' に置換 (Agent は  */
/*  line.split() で種別を読むため)、空 basename は "file"、`-` は "stdin"。   */
/*  終了コード: 0 成功 / 1 業務失敗 (ESERVICE) / 2 リンク / 3 usage /         */
/*              4 ローカル I/O (読み取り失敗)。                              */
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
#define LPR_OK      0
#define LPR_BIZ     1
#define LPR_LINK    2
#define LPR_USAGE   3
#define LPR_LOCAL   4

#define LPR_NAME_MAX  200   /* ジョブ名の上限 (要求行 1400B に収まる範囲) */

/* --- 前方宣言 (main を先頭関数に保つ) --------------------------------- */
#ifndef HOST_TEST
static KernelAPI *g_api;
#endif
static int io_open_read(const char *path);
static int io_read(int fd, void *buf, unsigned cap);
static void io_close(int fd);
static i32 lpr_src(void *ud, u8 *buf, u32 cap);
static void make_name(const char *path, char *out, u32 cap);
static void usage(void);

typedef struct { int fd; int ioerr; } LprSrc;

int main(int argc, char **argv
#ifndef HOST_TEST
         , KernelAPI *api
#endif
         )
{
    char name[LPR_NAME_MAX + 8];
    LprSrc s;
    u32 pages = 0, svc = 0;
    int rc, fd;

#ifndef HOST_TEST
    g_api = api;
#endif

    if (argc != 2) {
        usage();
        return LPR_USAGE;
    }

    if (strcmp(argv[1], "-") == 0) {
        fd = 0;                       /* 標準入力 */
        name[0] = 's'; name[1] = 't'; name[2] = 'd';
        name[3] = 'i'; name[4] = 'n'; name[5] = '\0';
    } else {
        fd = io_open_read(argv[1]);
        if (fd < 0) {
            printf("lpr: cannot open %s\n", argv[1]);
            return LPR_LOCAL;
        }
        make_name(argv[1], name, (u32)sizeof(name));
    }

    s.fd = fd;
    s.ioerr = 0;
    rc = host_print_stream(name, lpr_src, &s, &pages, &svc);

    if (fd > 2) io_close(fd);         /* 標準入力 (0) は閉じない */

    if (s.ioerr || rc == HOST_EABORT) {
        printf("lpr: read error\n");
        return LPR_LOCAL;
    }
    if (rc == HOST_ESERVICE) {
        printf("lpr: print failed (%u)\n", (unsigned)svc);
        return LPR_BIZ;
    }
    if (rc == HOST_EINVAL) { usage(); return LPR_USAGE; }
    if (rc == HOST_ELINK) { printf("lpr: host link down\n"); return LPR_LINK; }
    if (rc == HOST_ENODEV) { printf("lpr: no host link (no NIC)\n"); return LPR_LINK; }
    if (rc == HOST_ETIMEOUT) { printf("lpr: host timeout\n"); return LPR_LINK; }
    if (rc != 0) { printf("lpr: host error\n"); return LPR_LINK; }

    printf("printed, %u pages\n", (unsigned)pages);
    return LPR_OK;
}

/* ======================================================================== */
/*  src — ファイル / stdin を読む                                            */
/* ======================================================================== */
static i32 lpr_src(void *ud, u8 *buf, u32 cap)
{
    LprSrc *s = (LprSrc *)ud;
    int n = io_read(s->fd, buf, (unsigned)cap);
    if (n < 0) { s->ioerr = 1; return -1; }
    return (i32)n;                    /* 0 = EOF */
}

/* basename を取り、空白・制御文字を '_' に。空なら "file"。 */
static void make_name(const char *path, char *out, u32 cap)
{
    const char *base = path;
    const char *p;
    u32 i = 0;

    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;

    while (base[i] && i < cap - 1 && i < LPR_NAME_MAX) {
        unsigned char c = (unsigned char)base[i];
        out[i] = (c == ' ' || c < 0x20 || c == 0x7f) ? '_' : (char)c;
        i++;
    }
    out[i] = '\0';
    if (i == 0) {
        out[0] = 'f'; out[1] = 'i'; out[2] = 'l'; out[3] = 'e'; out[4] = '\0';
    }
}

static void usage(void)
{
    printf("Usage: lpr <file> | lpr -\n");
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

static void io_close(int fd)
{
#ifdef HOST_TEST
    close(fd);
#else
    g_api->sys_close(fd);
#endif
}
