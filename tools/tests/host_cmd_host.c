/* ========================================================================= */
/*  HOST_CMD_HOST.C — wget / lpr / hclip / hdate の引数・終了コードのホスト   */
/*  TDD (票 N3 §2/§4)。stat/tar と同じ作法で、実物の各コマンドソースを        */
/*  -DHOST_TEST で取り込み (main を改名)、libos32host の関数だけを台本式の贋  */
/*  物に差し替える。ローカル I/O は POSIX (実ファイル)。printf は捕まえる。   */
/*                                                                           */
/*  4 コマンドは同名 static (usage / io_close 等) を持つので 1 TU に同居でき  */
/*  ない。このファイルを -DCMD_WGET / -DCMD_LPR / -DCMD_HCLIP / -DCMD_HDATE の */
/*  いずれか 1 つで 4 回ビルドする (test_host_lib.py が回す)。                */
/*                                                                           */
/*  見るもの: 終了コード表 (0/1/2/3/4)、wget の http_status 確定後のファイル  */
/*  作成 (404 で空ファイルを残さない)、lpr の basename 正規化、hclip の空 /   */
/*  4096B 超、hdate。                                                         */
/* ========================================================================= */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "libos32host.h"     /* HOST_TEST 分岐: stdint 型 + HOST_E* + プロトタイプ */

/* ---- 出力の捕捉 -------------------------------------------------------- */
static char cap_out[8192];
static int cap_len;
static int cap_printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(cap_out + cap_len, sizeof(cap_out) - (size_t)cap_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        cap_len += n;
        if (cap_len > (int)sizeof(cap_out) - 1) cap_len = (int)sizeof(cap_out) - 1;
    }
    return n;
}

/* ---- 贋 libos32host (台本) — 使うコマンドのぶんだけ参照される ----------- */
static int fg_status;
static const char *fg_body;
static u32 fg_body_len;
static int fg_rc;
int host_get(const char *url, host_sink_fn sink, void *ud, int *http_status, u32 *nbytes)
{
    (void)url;
    if (http_status) *http_status = fg_status;
    if (fg_rc == 0 && sink && fg_body_len) {
        u32 off = 0;
        while (off < fg_body_len) {
            u32 n = fg_body_len - off;
            if (n > 1400) n = 1400;
            if (sink(ud, (const u8 *)fg_body + off, n)) return HOST_EABORT;
            off += n;
        }
    }
    if (nbytes) *nbytes = fg_body_len;
    return fg_rc;
}

static char fp_name[256];
static u32 fp_pages;
static int fp_rc;
static u32 fp_svc;
int host_print_stream(const char *name, host_source_fn src, void *ud, u32 *pages, u32 *svc_status)
{
    u8 buf[512];
    i32 n;
    strncpy(fp_name, name, sizeof(fp_name) - 1);
    fp_name[sizeof(fp_name) - 1] = '\0';
    do {
        n = src(ud, buf, (u32)sizeof(buf));
        if (n < 0) return HOST_EABORT;
    } while (n > 0);
    if (pages) *pages = fp_pages;
    if (svc_status) *svc_status = fp_svc;
    return fp_rc;
}

static int fcg_rc;
static const char *fcg_body;
static u32 fcg_body_len;
static u32 fcg_svc;
int host_clip_get(host_sink_fn sink, void *ud, u32 *nbytes, u32 *svc_status)
{
    if (fcg_rc == 0 && sink && fcg_body_len) {
        if (sink(ud, (const u8 *)fcg_body, fcg_body_len)) return HOST_EABORT;
    }
    if (nbytes) *nbytes = fcg_body_len;
    if (svc_status) *svc_status = fcg_svc;
    return fcg_rc;
}

static int fcp_rc;
static u32 fcp_len;
static u32 fcp_svc;
int host_clip_put(const char *buf, u32 len, u32 *svc_status)
{
    (void)buf;
    if (len < 1 || len > HOST_CLIP_MAX) return HOST_EINVAL;   /* 実装の契約を贋物でも守る */
    fcp_len = len;
    if (svc_status) *svc_status = fcp_svc;
    return fcp_rc;
}

static int ft_rc;
static const char *ft_str;
int host_time(char out[HOST_TIME_BUF])
{
    if (ft_rc == 0) {
        strncpy(out, ft_str, HOST_TIME_BUF - 1);
        out[HOST_TIME_BUF - 1] = '\0';
    }
    return ft_rc;
}

/* fake の未使用を黙らせる (コマンドごとに参照するものが違うため) */
static void touch_fakes(void)
{
    (void)fg_status; (void)fg_body; (void)fg_body_len; (void)fg_rc;
    (void)fp_name; (void)fp_pages; (void)fp_rc; (void)fp_svc;
    (void)fcg_rc; (void)fcg_body; (void)fcg_body_len; (void)fcg_svc;
    (void)fcp_rc; (void)fcp_len; (void)fcp_svc;
    (void)ft_rc; (void)ft_str;
}

/* ---- 試験土台 ---------------------------------------------------------- */
static int failures;
static int checks;
static char g_tmpdir[256];

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; fprintf(stderr, "  FAIL: %s\n  out: %s\n", what, cap_out); }
}
static void reset_out(void) { cap_len = 0; cap_out[0] = '\0'; }
static int has(const char *n) { return strstr(cap_out, n) != NULL; }
static void tpath(char *dst, const char *name)   /* dst は 512B */
{
    size_t a = strlen(g_tmpdir), b = strlen(name);
    memcpy(dst, g_tmpdir, a);
    dst[a] = '/';
    memcpy(dst + a + 1, name, b);
    dst[a + 1 + b] = '\0';
}
static int file_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static long file_size(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }
static void write_file(const char *p, const char *data, size_t n)
{
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { if (write(fd, data, n) < 0) { /* ignore */ } close(fd); }
}
static void rm(const char *p) { unlink(p); }
/* コマンドごとに使う補助が違うので、未使用 static 関数の警告を黙らせる。 */
static void touch_helpers(void)
{
    (void)has; (void)tpath; (void)file_exists; (void)file_size;
    (void)write_file; (void)rm;
}

/* ========================================================================= */
static void run(void);   /* コマンドごとに定義 */

/* ========================================================================= */
/*  wget                                                                      */
/* ========================================================================= */
#if defined(CMD_WGET)
#define printf cap_printf
#define main wget_main
#include "../../userland/cmds/wget.c"
#undef main
#undef printf
static void run(void)
{
    char *av[3];
    char out[512];
    char body[64];
    int i;

    av[0] = (char *)"wget";

    reset_out();
    check(wget_main(1, av) == 3, "wget usage: exit 3");
    check(has("Usage: wget"), "wget usage: message");

    tpath(out, "w200"); rm(out);
    for (i = 0; i < 20; i++) body[i] = (char)('A' + (i % 26));
    fg_status = 200; fg_body = body; fg_body_len = 20; fg_rc = 0;
    av[1] = (char *)"http://x/f"; av[2] = out;
    reset_out();
    check(wget_main(3, av) == 0, "wget 200: exit 0");
    check(has("20 bytes"), "wget 200: prints byte count");
    check(file_size(out) == 20, "wget 200: file has 20 bytes");

    tpath(out, "w404"); rm(out);
    fg_status = 404; fg_body = body; fg_body_len = 20; fg_rc = 0;
    av[1] = (char *)"http://x/nope"; av[2] = out;
    reset_out();
    check(wget_main(3, av) == 1, "wget 404: exit 1");
    check(has("wget: 404"), "wget 404: status message");
    check(!file_exists(out), "wget 404: no empty file left");

    tpath(out, "w200e"); rm(out);
    fg_status = 200; fg_body = body; fg_body_len = 0; fg_rc = 0;
    av[1] = (char *)"http://x/empty"; av[2] = out;
    reset_out();
    check(wget_main(3, av) == 0, "wget 200 empty: exit 0");
    check(file_exists(out) && file_size(out) == 0, "wget 200 empty: empty file created");

    fg_rc = HOST_ELINK; av[1] = (char *)"http://x/"; av[2] = (char *)0;
    reset_out();
    check(wget_main(2, av) == 2, "wget ELINK: exit 2");
    check(has("host link down"), "wget ELINK: message");

    fg_rc = HOST_ETIMEOUT;
    reset_out();
    check(wget_main(2, av) == 2, "wget ETIMEOUT: exit 2");
    check(has("timeout"), "wget ETIMEOUT: message");

    fg_rc = HOST_ENODEV;
    reset_out();
    check(wget_main(2, av) == 2, "wget ENODEV: exit 2");

    fg_rc = HOST_EINVAL;
    reset_out();
    check(wget_main(2, av) == 3, "wget EINVAL: exit 3");
}
#endif

/* ========================================================================= */
/*  lpr                                                                       */
/* ========================================================================= */
#if defined(CMD_LPR)
#define printf cap_printf
#define main lpr_main
#include "../../userland/cmds/lpr.c"
#undef main
#undef printf
static void run(void)
{
    char *av[2];
    char p[512];

    av[0] = (char *)"lpr";

    reset_out();
    check(lpr_main(1, av) == 3, "lpr usage: exit 3");

    tpath(p, "a b\tc.txt");
    write_file(p, "hello\n", 6);
    fp_rc = 0; fp_pages = 2; fp_name[0] = '\0';
    av[1] = p;
    reset_out();
    check(lpr_main(2, av) == 0, "lpr file: exit 0");
    check(has("printed, 2 pages"), "lpr file: pages message");
    check(strcmp(fp_name, "a_b_c.txt") == 0, "lpr file: basename sanitized to a_b_c.txt");

    fp_rc = 0; fp_pages = 1; fp_name[0] = '\0';
    av[1] = (char *)"-";
    reset_out();
    check(lpr_main(2, av) == 0, "lpr -: exit 0");
    check(strcmp(fp_name, "stdin") == 0, "lpr -: name is stdin");

    write_file(p, "x", 1);
    fp_rc = HOST_ESERVICE; fp_svc = 500;
    av[1] = p;
    reset_out();
    check(lpr_main(2, av) == 1, "lpr ESERVICE: exit 1");
    check(has("print failed (500)"), "lpr ESERVICE: message with svc");

    fp_rc = HOST_ELINK;
    reset_out();
    check(lpr_main(2, av) == 2, "lpr ELINK: exit 2");

    av[1] = (char *)"/no/such/path/xyz";
    reset_out();
    check(lpr_main(2, av) == 4, "lpr open fail: exit 4");
    rm(p);
}
#endif

/* ========================================================================= */
/*  hclip                                                                     */
/* ========================================================================= */
#if defined(CMD_HCLIP)
#define printf cap_printf
#define main hclip_main
#include "../../userland/cmds/hclip.c"
#undef main
#undef printf
static void run(void)
{
    char *av[3];
    char p[512];

    av[0] = (char *)"hclip";

    reset_out();
    check(hclip_main(1, av) == 3, "hclip usage: exit 3");

    tpath(p, "clip.txt");
    write_file(p, "hello", 5);
    fcp_rc = 0;
    av[1] = (char *)"put"; av[2] = p;
    reset_out();
    check(hclip_main(3, av) == 0, "hclip put: exit 0");

    write_file(p, "", 0);
    fcp_rc = 0;
    reset_out();
    check(hclip_main(3, av) == 3, "hclip put empty: exit 3");
    check(has("empty"), "hclip put empty: message");

    {
        static char big[HOST_CLIP_MAX + 8];
        memset(big, 'z', HOST_CLIP_MAX + 1);
        write_file(p, big, HOST_CLIP_MAX + 1);
    }
    fcp_rc = 0;
    reset_out();
    check(hclip_main(3, av) == 3, "hclip put too big: exit 3");
    check(has("too large"), "hclip put too big: message");

    fcg_rc = 0; fcg_body = "clip"; fcg_body_len = 4;
    av[1] = (char *)"get";
    reset_out();
    check(hclip_main(2, av) == 0, "hclip get: exit 0");

    fcg_rc = HOST_ESERVICE; fcg_svc = 503;
    reset_out();
    check(hclip_main(2, av) == 1, "hclip get 503: exit 1");

    fcg_rc = HOST_ELINK;
    reset_out();
    check(hclip_main(2, av) == 2, "hclip get link: exit 2");
    rm(p);
}
#endif

/* ========================================================================= */
/*  hdate                                                                     */
/* ========================================================================= */
#if defined(CMD_HDATE)
#define printf cap_printf
#define main hdate_main
#include "../../userland/cmds/hdate.c"
#undef main
#undef printf
static void run(void)
{
    char *av[1];
    av[0] = (char *)"hdate";

    ft_rc = 0; ft_str = "2026-09-14 12:34:56";
    reset_out();
    check(hdate_main(1, av) == 0, "hdate: exit 0");
    check(has("2026-09-14 12:34:56"), "hdate: prints host time");

    ft_rc = HOST_ELINK;
    reset_out();
    check(hdate_main(1, av) == 2, "hdate link: exit 2");
    check(has("host link down"), "hdate link: message");

    ft_rc = HOST_ENODEV;
    reset_out();
    check(hdate_main(1, av) == 2, "hdate nodev: exit 2");
}
#endif

/* ========================================================================= */
int main(void)
{
    char tmpl[] = "/tmp/os32-hostcmd-XXXXXX";
    char *d = mkdtemp(tmpl);
    if (!d) { fprintf(stderr, "mkdtemp failed\n"); return 2; }
    strncpy(g_tmpdir, d, sizeof(g_tmpdir) - 1);
    g_tmpdir[sizeof(g_tmpdir) - 1] = '\0';
    touch_fakes();
    touch_helpers();

    run();

    if (failures) {
        fprintf(stderr, "host_cmd_host: FAIL %d/%d\n", checks - failures, checks);
        return 1;
    }
    printf("host_cmd_host: PASS %d/%d\n", checks, checks);
    return 0;
}
