/* ========================================================================= */
/*  HOST_LIB_HOST.C — libos32host (票 N3 §1) のホスト TDD                     */
/*                                                                           */
/*  実物の userland/lib/host/libos32host.c をそのまま #include し、KAPI の    */
/*  host_open / host_status / host_read / host_write / host_close /          */
/*  get_tick / sys_yield だけを贋テーブルに差し替える。贋 Agent は「1 回に    */
/*  1 ハンドル」(ライブラリは多段でも同時に 1 本しか開かない) を前提に、      */
/*  open ごとに 1 つの HScript を消費する台本方式で駆動する。sys_yield は     */
/*  贋クロック fk_now を進める。                                              */
/*                                                                           */
/*  HOST_LIB_TEST_SEAMS を定義してビルドするので、無進捗期限を「絶対期限」に  */
/*  落とす変異点 host__np_absolute が使える (回帰の否定側)。                  */
/*                                                                           */
/*  実行: tools/tests/test_host_lib.py。記録は tools/tests/n3_tdd.md。        */
/* ========================================================================= */

#include <stdio.h>
#include <string.h>

#include "os32api.h"     /* KernelAPI, OS32_ERR_*, u8/u16/u32/i32 */

/* ------------------------------------------------------------------------- */
/*  台本 (open 1 回ぶんのふるまい)                                            */
/* ------------------------------------------------------------------------- */
typedef struct {
    int open_stale;      /* この回数だけ open が STALE (減算) */
    int open_again;      /* この回数だけ open が AGAIN (減算) */
    int open_full;       /* この回数だけ open が FULL (減算) */
    int open_nosys;      /* open が NOSYS を返す */
    int status_again;    /* 業務 RESPONSE 前に status が AGAIN を返す回数 */
    int status_forever;  /* status が常に AGAIN (無進捗 ETIMEOUT 用) */
    u32 status;          /* 業務ステータス */
    const u8 *body;      /* read 本文 (NULL 可) */
    u32 body_len;
    u32 read_gap;        /* read チャンク間に要する tick (0 = 即時) */
    u32 read_chunk;      /* read 1 回の最大 (0 = 1400) */
    int write_inval;     /* host_write が即 INVAL を返す */
    int write_inval_mode;/* INVAL 後の status: 0=業務, 1=AGAIN, 2=STALE */
    int write_again;     /* 最初のこの回数だけ host_write が AGAIN (減算) */
    u32 write_gap;       /* write チャンク間に要する tick (0 = 即時) */
    u32 write_chunk;     /* write 1 回の最大 (0 = 1400) */
} HScript;

#define MAXS 8
static HScript scripts[MAXS];
static int nscripts;
static int cur;

/* 記録 */
static char reqlog[MAXS][256];
static int  reqn;

/* クロック / カウンタ */
static u32 fk_now;
static u32 fk_yield_step;
static int fk_open_count;
static int fk_close_count;

/* 現ハンドルの実行時状態 */
static int fk_open_flag;
static u32 rt_body_off;
static u32 rt_read_last;
static u32 rt_write_last;
static u32 rt_write_got;
static int rt_status_again;
static int rt_write_inval_fired;
static u32 fk_total_write;   /* 全 host_write の合計 (open ごとに消えない) */

/* パターン本文 (>64KB ストリーミング / GET) */
static u8 g_pat[200000];
static const char *g_time = "2026-09-14 12:34:56";   /* 19B */

/* 外部 (libos32host.c の) 変異点 */
extern int host__np_absolute;

/* ------------------------------------------------------------------------- */
/*  贋 KAPI                                                                   */
/* ------------------------------------------------------------------------- */
static i32 fk_open_fn(const char *req, u32 len)
{
    HScript *s;
    fk_open_count++;
    s = (cur < nscripts) ? &scripts[cur] : &scripts[0];
    if (s->open_nosys) return OS32_ERR_NOSYS;
    if (s->open_again > 0) { s->open_again--; return OS32_ERR_AGAIN; }
    if (s->open_full > 0) { s->open_full--; return OS32_ERR_FULL; }
    if (s->open_stale > 0) { s->open_stale--; return OS32_ERR_STALE; }
    if (len > 200) len = 200;
    if (reqn < MAXS) { memcpy(reqlog[reqn], req, len); reqlog[reqn][len] = '\0'; reqn++; }
    fk_open_flag = 1;
    rt_body_off = 0;
    rt_read_last = fk_now;
    rt_write_last = fk_now;
    rt_write_got = 0;
    rt_status_again = s->status_again;
    rt_write_inval_fired = 0;
    return 0;
}

static i32 fk_status_fn(i32 h, u32 *status, u32 *length)
{
    HScript *s = &scripts[cur];
    (void)h;
    if (rt_write_inval_fired) {
        if (s->write_inval_mode == 1) return OS32_ERR_AGAIN;
        if (s->write_inval_mode == 2) return OS32_ERR_STALE;
        if (status) *status = s->status;
        if (length) *length = 0;
        return 0;
    }
    if (s->status_forever) return OS32_ERR_AGAIN;
    if (rt_status_again > 0) { rt_status_again--; return OS32_ERR_AGAIN; }
    if (status) *status = s->status;
    if (length) *length = s->body_len;
    return 0;
}

static i32 fk_read_fn(i32 h, void *buf, u32 cap)
{
    HScript *s = &scripts[cur];
    u32 remaining, n, lim;
    (void)h;
    remaining = s->body_len - rt_body_off;
    if (remaining == 0) return 0;
    if (s->read_gap > 0 && (u32)(fk_now - rt_read_last) < s->read_gap) return OS32_ERR_AGAIN;
    n = remaining;
    lim = s->read_chunk ? s->read_chunk : 1400u;
    if (n > lim) n = lim;
    if (n > cap) n = cap;
    memcpy(buf, s->body + rt_body_off, n);
    rt_body_off += n;
    rt_read_last = fk_now;
    return (i32)n;
}

static i32 fk_write_fn(i32 h, const void *buf, u32 len)
{
    HScript *s = &scripts[cur];
    u32 n;
    (void)h; (void)buf;
    if (s->write_inval) { rt_write_inval_fired = 1; return OS32_ERR_INVAL; }
    if (s->write_again > 0) { s->write_again--; return OS32_ERR_AGAIN; }
    if (s->write_gap > 0 && (u32)(fk_now - rt_write_last) < s->write_gap) return OS32_ERR_AGAIN;
    n = len; if (n > 1400) n = 1400;
    if (s->write_chunk && n > s->write_chunk) n = s->write_chunk;
    rt_write_got += n;
    fk_total_write += n;
    rt_write_last = fk_now;
    return (i32)n;
}

static i32 fk_close_fn(i32 h)
{
    (void)h;
    if (!fk_open_flag) return OS32_ERR_INVAL;
    fk_open_flag = 0;
    fk_close_count++;
    cur++;
    return 0;
}

static u32 fk_get_tick(void) { return fk_now; }
static i32 fk_sys_yield(void) { fk_now += fk_yield_step; return 0; }

/* ------------------------------------------------------------------------- */
/*  贋テーブルと大域 kapi (libos32host.c の extern を満たす)                   */
/* ------------------------------------------------------------------------- */
static KernelAPI g_fake;
KernelAPI *kapi = &g_fake;

#define HOST_LIB_TEST_SEAMS
#include "../../userland/lib/host/libos32host.c"

/* ------------------------------------------------------------------------- */
/*  試験土台                                                                  */
/* ------------------------------------------------------------------------- */
static int failures;
static int checks;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; fprintf(stderr, "  FAIL: %s\n", what); }
}

static void fk_reset(void)
{
    int i;
    memset(scripts, 0, sizeof(scripts));
    nscripts = 0; cur = 0; reqn = 0;
    fk_now = 0; fk_yield_step = 100;
    fk_open_count = 0; fk_close_count = 0;
    fk_open_flag = 0; fk_total_write = 0;
    host__np_absolute = 0;
    g_link_up = 0;                 /* 最初の open の STALE 予算を復活 */
    for (i = 0; i < MAXS; i++) reqlog[i][0] = '\0';
}

static int reqlog_has_prefix(const char *pfx)
{
    int i;
    u32 n = (u32)strlen(pfx);
    for (i = 0; i < reqn; i++)
        if (strncmp(reqlog[i], pfx, n) == 0) return 1;
    return 0;
}

static int reqlog_count_prefix(const char *pfx)
{
    int i, c = 0;
    u32 n = (u32)strlen(pfx);
    for (i = 0; i < reqn; i++)
        if (strncmp(reqlog[i], pfx, n) == 0) c++;
    return c;
}

/* ---- sink / src ---------------------------------------------------------- */
typedef struct { u32 total; u32 bad; int http_seen; int http_val; int abort_at; } CapSink;
static int cap_sink(void *ud, const u8 *b, u32 n)
{
    CapSink *c = (CapSink *)ud;
    u32 k;
    for (k = 0; k < n; k++)
        if (b[k] != (u8)(c->total + k)) c->bad++;
    c->total += n;
    if (c->abort_at > 0 && c->total >= (u32)c->abort_at) return 1;
    return 0;
}
/* http_status を初回 sink で観測する */
typedef struct { u32 total; int http_val; int seen; int *statp; } StatSink;
static int stat_sink(void *ud, const u8 *b, u32 n)
{
    StatSink *c = (StatSink *)ud;
    (void)b;
    if (!c->seen) { c->http_val = *c->statp; c->seen = 1; }
    c->total += n;
    return 0;
}

/* 1 バイトずつ返す src (途中 EOF) */
typedef struct { u32 remaining; } OneSrc;
static i32 one_src(void *ud, u8 *buf, u32 cap)
{
    OneSrc *s = (OneSrc *)ud;
    (void)cap;
    if (s->remaining == 0) return 0;
    buf[0] = 0xAB;
    s->remaining--;
    return 1;
}
/* 固定バッファを流す src */
typedef struct { const u8 *p; u32 len; u32 off; } BufSrc;
static i32 buf_src(void *ud, u8 *buf, u32 cap)
{
    BufSrc *s = (BufSrc *)ud;
    u32 n = s->len - s->off;
    if (n == 0) return 0;
    if (n > cap) n = cap;
    memcpy(buf, s->p + s->off, n);
    s->off += n;
    return (i32)n;
}
/* 負を返す src */
static i32 abort_src(void *ud, u8 *buf, u32 cap)
{ (void)ud; (void)buf; (void)cap; return -1; }

/* ========================================================================= */
/*  ケース                                                                    */
/* ========================================================================= */

static void case_time(void)
{
    char out[HOST_TIME_BUF];
    int rc;
    fk_reset();
    nscripts = 1;
    scripts[0].status = 200;
    scripts[0].body = (const u8 *)g_time; scripts[0].body_len = 19;
    rc = host_time(out);
    check(rc == 0, "time: rc 0");
    check(strcmp(out, g_time) == 0, "time: 19B body NUL-terminated");
    check(fk_close_count == fk_open_count && fk_open_count == 1, "time: 1 open == 1 close");
}

static void case_get_stream(void)
{
    CapSink c;
    int rc, http = -1;
    u32 nb = 0;
    fk_reset();
    nscripts = 1;
    scripts[0].status = 200;
    scripts[0].body = g_pat; scripts[0].body_len = 200000;   /* >64KB */
    c.total = 0; c.bad = 0; c.abort_at = 0;
    rc = host_get("http://x/big", cap_sink, &c, &http, &nb);
    check(rc == 0, "get: rc 0");
    check(http == 200, "get: http_status 200");
    check(nb == 200000, "get: nbytes 200000");
    check(c.total == 200000, "get: sink saw whole body (streamed)");
    check(c.bad == 0, "get: pattern matches");
    check(fk_close_count == 1, "get: handle closed");
}

static void case_get_404_status_order(void)
{
    StatSink c;
    int rc, http = -1;
    u32 nb = 0;
    fk_reset();
    nscripts = 1;
    scripts[0].status = 404;
    scripts[0].body = g_pat; scripts[0].body_len = 50;   /* 404 のエラーページ */
    c.total = 0; c.http_val = -1; c.seen = 0; c.statp = &http;
    rc = host_get("http://x/nope", stat_sink, &c, &http, &nb);
    check(rc == 0, "get404: rc 0 (業務失敗はエラーにしない)");
    check(http == 404, "get404: http_status 404 via out");
    check(c.seen && c.http_val == 404, "get404: http_status set before read loop");
}

static void case_get_url_too_long(void)
{
    static char url[2000];
    CapSink c;
    int rc, http = -1;
    u32 nb = 0;
    memset(url, 'a', sizeof(url) - 1); url[sizeof(url) - 1] = '\0';
    fk_reset();
    c.total = 0; c.bad = 0; c.abort_at = 0;
    rc = host_get(url, cap_sink, &c, &http, &nb);
    check(rc == HOST_EINVAL, "urllong: EINVAL");
    check(fk_open_count == 0, "urllong: host_open never called");
}

static void case_get_sink_abort(void)
{
    CapSink c;
    int rc, http = -1;
    u32 nb = 0;
    fk_reset();
    nscripts = 1;
    scripts[0].status = 200;
    scripts[0].body = g_pat; scripts[0].body_len = 10000;
    scripts[0].read_chunk = 1400;
    c.total = 0; c.bad = 0; c.abort_at = 1400;   /* 最初のチャンクで中断 */
    rc = host_get("http://x/", cap_sink, &c, &http, &nb);
    check(rc == HOST_EABORT, "getabort: EABORT");
    check(fk_close_count == 1, "getabort: handle still closed");
}

static void case_open_stale_up(void)
{
    char out[HOST_TIME_BUF];
    int rc;
    /* 最初の open が STALE を 2 回 (200 tick < 3 秒) 返してから成功 */
    fk_reset();
    nscripts = 1;
    scripts[0].open_stale = 2;
    scripts[0].status = 200; scripts[0].body = (const u8 *)g_time; scripts[0].body_len = 19;
    rc = host_time(out);
    check(rc == 0, "openstale_up: link comes up within 3s");
    check(fk_now <= HOST_OPEN_STALE_TICKS, "openstale_up: succeeded before budget");
}

static void case_open_stale_timeout(void)
{
    char out[HOST_TIME_BUF];
    int rc;
    fk_reset();
    nscripts = 1;
    scripts[0].open_stale = 999;   /* ずっと STALE */
    scripts[0].status = 200;
    rc = host_time(out);
    check(rc == HOST_ELINK, "openstale_to: ELINK after 3s budget");
    check(fk_now >= HOST_OPEN_STALE_TICKS, "openstale_to: waited the budget");
}

static void case_second_stage_stale(void)
{
    u32 pages = 0, svc = 0;
    int rc;
    static const char *job = "job 7";
    /* stage1 (OPEN) 成功 -> g_link_up=1、stage2 (DATA) が STALE -> 即 ELINK */
    fk_reset();
    nscripts = 2;
    scripts[0].status = 200; scripts[0].body = (const u8 *)job; scripts[0].body_len = 5;
    scripts[1].open_stale = 999;   /* DATA の open が STALE */
    rc = host_print_text("doc", "hello", 5, &pages, &svc);
    check(rc == HOST_ELINK, "stage2stale: ELINK immediately (established)");
    check(fk_now < HOST_OPEN_STALE_TICKS, "stage2stale: did NOT wait the 3s budget");
    check(!reqlog_has_prefix("PRINT CLOSE"), "stage2stale: no PRINT CLOSE sent");
}

static void case_status_timeout(void)
{
    char out[HOST_TIME_BUF];
    int rc;
    fk_reset();
    nscripts = 1;
    scripts[0].status_forever = 1;   /* AGAIN のみ */
    rc = host_time(out);
    check(rc == HOST_ETIMEOUT, "statusto: ETIMEOUT on no progress");
    check(fk_now >= HOST_NOPROG_TICKS, "statusto: waited the 30s deadline");
}

static void case_noprogress_positive(void)
{
    CapSink c;
    int rc, http = -1;
    u32 nb = 0;
    /* 20 秒 (2000 tick) ごとに 1 チャンク、3 チャンク = 60 秒。基準取り直しで成功。 */
    fk_reset();
    fk_yield_step = 250;             /* 8 yield = 2000 tick */
    nscripts = 1;
    scripts[0].status = 200;
    scripts[0].body = g_pat; scripts[0].body_len = 3000; scripts[0].read_chunk = 1000;
    scripts[0].read_gap = 2000;
    c.total = 0; c.bad = 0; c.abort_at = 0;
    rc = host_get("http://x/slow", cap_sink, &c, &http, &nb);
    check(rc == 0, "noprog+: 60s transfer with 20s gaps succeeds");
    check(c.total == 3000, "noprog+: full body");
    check(fk_now >= 6000, "noprog+: really spanned ~60s");
}

static void case_noprogress_absolute_mutant(void)
{
    CapSink c;
    int rc, http = -1;
    u32 nb = 0;
    /* 変異: 絶対期限にすると同じ 60s/20s 転送が 30s で死ぬ (回帰の否定側) */
    fk_reset();
    fk_yield_step = 250;
    nscripts = 1;
    scripts[0].status = 200;
    scripts[0].body = g_pat; scripts[0].body_len = 3000; scripts[0].read_chunk = 1000;
    scripts[0].read_gap = 2000;
    host__np_absolute = 1;
    c.total = 0; c.bad = 0; c.abort_at = 0;
    rc = host_get("http://x/slow", cap_sink, &c, &http, &nb);
    check(rc == HOST_ETIMEOUT, "noprog-: absolute-deadline mutant times out");
    host__np_absolute = 0;
}

static void case_nosys(void)
{
    char out[HOST_TIME_BUF];
    int rc;
    fk_reset();
    nscripts = 1;
    scripts[0].open_nosys = 1;
    rc = host_time(out);
    check(rc == HOST_ENODEV, "nosys: NOSYS -> ENODEV");
}

static void case_open_again_yield(void)
{
    char out[HOST_TIME_BUF];
    int rc;
    /* 両スロット rel_pending: open が AGAIN を 2 回 -> yield -> 成功 */
    fk_reset();
    nscripts = 1;
    scripts[0].open_again = 2;
    scripts[0].status = 200; scripts[0].body = (const u8 *)g_time; scripts[0].body_len = 19;
    rc = host_time(out);
    check(rc == 0, "openagain: succeeds after AGAIN");
    check(fk_now > 0, "openagain: yielded (clock advanced)");
    check(fk_open_count == 3, "openagain: 2 AGAIN + 1 success");
}

static void case_open_full_yield(void)
{
    char out[HOST_TIME_BUF];
    int rc;
    /* N3-fix (b): host_open が FULL を 2 回 -> yield 待ち -> 成功。
     * FULL を EIO 扱いにする変異はここで落ちる (open_full 欄は従来未使用)。 */
    fk_reset();
    nscripts = 1;
    scripts[0].open_full = 2;
    scripts[0].status = 200; scripts[0].body = (const u8 *)g_time; scripts[0].body_len = 19;
    rc = host_time(out);
    check(rc == 0, "openfull: succeeds after FULL (yield 待ち)");
    check(strcmp(out, g_time) == 0, "openfull: body intact");
    check(fk_now > 0, "openfull: yielded (clock advanced)");
    check(fk_open_count == 3, "openfull: 2 FULL + 1 success");
}

static void case_write_again_yield(void)
{
    int rc;
    u32 svc = 0;
    /* N3-fix (a): send_decl の write が最初の 3 回 AGAIN -> yield -> 送り切る。
     * 実カーネルは REQUEST 転送 ACK まで必ず write AGAIN。AGAIN を EIO 扱いに
     * する変異はここで落ちる (従来は write AGAIN 経路が試験に無かった)。 */
    fk_reset();
    nscripts = 1;
    scripts[0].status = 200;
    scripts[0].write_again = 3;
    rc = host_clip_put("hello", 5, &svc);
    check(rc == 0, "wagain: CLIP PUT succeeds after write AGAIN");
    check(reqlog_has_prefix("CLIP PUT 5"), "wagain: declared length 5");
    check(fk_now > 0, "wagain: yielded on write AGAIN (clock advanced)");
    check(fk_total_write == 5, "wagain: 5B delivered after retries");
}

static void case_write_noprogress_positive(void)
{
    static u8 big[3000];
    int rc, i;
    u32 svc = 0;
    /* N3-fix (c) 肯定側: write>0 が 20 秒 (2000 tick) ごとに進む 60 秒転送。
     * 無進捗基準の取り直しで成功する (write 経路の np_reset)。 */
    for (i = 0; i < 3000; i++) big[i] = (u8)i;
    fk_reset();
    fk_yield_step = 250;             /* 8 yield = 2000 tick */
    nscripts = 1;
    scripts[0].status = 200;
    scripts[0].write_gap = 2000; scripts[0].write_chunk = 1000;
    rc = host_clip_put((const char *)big, 3000, &svc);
    check(rc == 0, "wnoprog+: 60s write with 20s gaps succeeds");
    check(fk_total_write == 3000, "wnoprog+: full body written");
    check(fk_now >= 6000, "wnoprog+: really spanned ~60s");
}

static void case_write_noprogress_absolute_mutant(void)
{
    static u8 big[3000];
    int rc, i;
    u32 svc = 0;
    /* N3-fix (c) 否定側: 無進捗を絶対期限に戻すと同じ 60s/20s の write 転送が
     * 30s で ETIMEOUT に落ちる (回帰の否定側、read 経路の対応物)。 */
    for (i = 0; i < 3000; i++) big[i] = (u8)i;
    fk_reset();
    fk_yield_step = 250;
    nscripts = 1;
    scripts[0].status = 200;
    scripts[0].write_gap = 2000; scripts[0].write_chunk = 1000;
    host__np_absolute = 1;
    rc = host_clip_put((const char *)big, 3000, &svc);
    check(rc == HOST_ETIMEOUT, "wnoprog-: absolute-deadline mutant times out on write");
    host__np_absolute = 0;
}

static void case_clip_put_ok(void)
{
    int rc;
    u32 svc = 0;
    fk_reset();
    nscripts = 1;
    scripts[0].status = 200;
    rc = host_clip_put("hello", 5, &svc);
    check(rc == 0, "clipput: rc 0");
    check(reqlog_has_prefix("CLIP PUT 5"), "clipput: declared length 5");
}

static void case_clip_put_empty(void)
{
    int rc;
    u32 svc = 0;
    fk_reset();
    rc = host_clip_put("", 0, &svc);
    check(rc == HOST_EINVAL, "clipempty: EINVAL");
    check(fk_open_count == 0, "clipempty: no open");
    rc = host_clip_put("x", HOST_CLIP_MAX + 1, &svc);
    check(rc == HOST_EINVAL, "cliptoobig: EINVAL");
}

static void case_clip_get_503(void)
{
    CapSink c;
    int rc;
    u32 nb = 0, svc = 0;
    fk_reset();
    nscripts = 1;
    scripts[0].status = 503;
    c.total = 0; c.bad = 0; c.abort_at = 0;
    rc = host_clip_get(cap_sink, &c, &nb, &svc);
    check(rc == HOST_ESERVICE, "clipget503: ESERVICE");
    check(svc == 503, "clipget503: svc_status 503");
    check(fk_close_count == 1, "clipget503: handle closed");
}

static void case_write_inval_business(void)
{
    int rc;
    u32 svc = 0;
    /* write INVAL -> status が業務 500 -> ESERVICE */
    fk_reset();
    nscripts = 1;
    scripts[0].write_inval = 1; scripts[0].write_inval_mode = 0; scripts[0].status = 500;
    rc = host_clip_put("hello", 5, &svc);
    check(rc == HOST_ESERVICE, "winval_biz: ESERVICE");
    check(svc == 500, "winval_biz: svc 500");
}

static void case_write_inval_again(void)
{
    int rc;
    u32 svc = 0;
    /* write INVAL -> status AGAIN -> 真の EINVAL */
    fk_reset();
    nscripts = 1;
    scripts[0].write_inval = 1; scripts[0].write_inval_mode = 1;
    rc = host_clip_put("hello", 5, &svc);
    check(rc == HOST_EINVAL, "winval_again: EINVAL");
}

static void case_write_inval_stale(void)
{
    int rc;
    u32 svc = 0;
    /* write INVAL -> status STALE -> ELINK */
    fk_reset();
    nscripts = 1;
    scripts[0].write_inval = 1; scripts[0].write_inval_mode = 2;
    rc = host_clip_put("hello", 5, &svc);
    check(rc == HOST_ELINK, "winval_stale: ELINK");
}

static void case_print_text_70k(void)
{
    static u8 big[70000];
    u32 pages = 0, svc = 0;
    int rc, i;
    static const char *job = "job 9";
    static const char *pg = "pages 3";
    for (i = 0; i < 70000; i++) big[i] = (u8)i;
    fk_reset();
    nscripts = 4;   /* OPEN, DATA, DATA, CLOSE */
    scripts[0].status = 200; scripts[0].body = (const u8 *)job; scripts[0].body_len = 5;
    scripts[1].status = 200;
    scripts[2].status = 200;
    scripts[3].status = 200; scripts[3].body = (const u8 *)pg; scripts[3].body_len = 7;
    rc = host_print_text("report", (const char *)big, 70000, &pages, &svc);
    check(rc == 0, "print70k: rc 0");
    check(pages == 3, "print70k: pages parsed");
    check(reqlog_count_prefix("PRINT DATA") == 2, "print70k: DATA split into 2");
    check(reqlog_has_prefix("PRINT DATA 9 65536"), "print70k: first DATA declares 65536");
    check(reqlog_has_prefix("PRINT DATA 9 4464"), "print70k: second DATA declares 4464");
    check(fk_close_count == fk_open_count && fk_open_count == 4, "print70k: 4 open == 4 close");
}

static void case_print_stream_onebyte(void)
{
    OneSrc src;
    u32 pages = 0, svc = 0;
    int rc;
    static const char *job = "job 5";
    static const char *pg = "pages 1";
    /* 1 バイトずつ 5 バイト供給 -> 途中 EOF。宣言長 == write 合計 == 5、DATA 1 本 */
    fk_reset();
    nscripts = 3;   /* OPEN, DATA, CLOSE */
    scripts[0].status = 200; scripts[0].body = (const u8 *)job; scripts[0].body_len = 5;
    scripts[1].status = 200;
    scripts[2].status = 200; scripts[2].body = (const u8 *)pg; scripts[2].body_len = 7;
    src.remaining = 5;
    rc = host_print_stream("note", one_src, &src, &pages, &svc);
    check(rc == 0, "streamone: rc 0");
    check(reqlog_count_prefix("PRINT DATA") == 1, "streamone: single DATA");
    check(reqlog_has_prefix("PRINT DATA 5 5"), "streamone: declared length == 5 (実長)");
    check(fk_total_write == 5, "streamone: wrote exactly 5 (write 合計 == 宣言長)");
}

static void case_print_stream_empty(void)
{
    OneSrc src;
    u32 pages = 0, svc = 0;
    int rc;
    static const char *job = "job 2";
    static const char *pg = "pages 0";
    /* 詰め 0 (即 EOF) -> DATA を出さず CLOSE へ */
    fk_reset();
    nscripts = 2;   /* OPEN, CLOSE (DATA 無し) */
    scripts[0].status = 200; scripts[0].body = (const u8 *)job; scripts[0].body_len = 5;
    scripts[1].status = 200; scripts[1].body = (const u8 *)pg; scripts[1].body_len = 7;
    src.remaining = 0;
    rc = host_print_stream("empty", one_src, &src, &pages, &svc);
    check(rc == 0, "streamempty: rc 0");
    check(reqlog_count_prefix("PRINT DATA") == 0, "streamempty: no DATA");
    check(reqlog_has_prefix("PRINT CLOSE"), "streamempty: CLOSE still sent");
    check(pages == 0, "streamempty: pages 0");
}

static void case_print_stream_abort(void)
{
    u32 pages = 0, svc = 0;
    int rc;
    static const char *job = "job 1";
    fk_reset();
    nscripts = 2;   /* OPEN, DATA(あるいは中断) */
    scripts[0].status = 200; scripts[0].body = (const u8 *)job; scripts[0].body_len = 5;
    scripts[1].status = 200;
    rc = host_print_stream("bad", abort_src, (void *)0, &pages, &svc);
    check(rc == HOST_EABORT, "streamabort: src<0 -> EABORT");
    check(!reqlog_has_prefix("PRINT CLOSE"), "streamabort: no CLOSE (後始末)");
}

static void case_print_409_cleanup(void)
{
    static u8 big[100];
    u32 pages = 0, svc = 0;
    int rc, i;
    static const char *job = "job 4";
    for (i = 0; i < 100; i++) big[i] = (u8)i;
    /* DATA 段で業務 409 -> ESERVICE、CLOSE を出さず、close 回数 == open 回数 */
    fk_reset();
    nscripts = 3;   /* OPEN, DATA(409), (CLOSE は呼ばれない) */
    scripts[0].status = 200; scripts[0].body = (const u8 *)job; scripts[0].body_len = 5;
    scripts[1].status = 409;
    rc = host_print_text("doc", (const char *)big, 100, &pages, &svc);
    check(rc == HOST_ESERVICE, "print409: ESERVICE");
    check(svc == 409, "print409: svc 409");
    check(!reqlog_has_prefix("PRINT CLOSE"), "print409: no PRINT CLOSE after 409");
    check(fk_close_count == fk_open_count && fk_open_count == 2, "print409: close == open == 2");
}

static void case_print_stream_bufsrc(void)
{
    static u8 big[40000];
    BufSrc src;
    u32 pages = 0, svc = 0;
    int rc, i;
    static const char *job = "job 8";
    static const char *pg = "pages 2";
    for (i = 0; i < 40000; i++) big[i] = (u8)i;
    /* 40KB を 16KB バッファで流す -> DATA 3 本 (16384 + 16384 + 7232) */
    fk_reset();
    nscripts = 5;   /* OPEN, DATA, DATA, DATA, CLOSE */
    scripts[0].status = 200; scripts[0].body = (const u8 *)job; scripts[0].body_len = 5;
    scripts[1].status = 200; scripts[2].status = 200; scripts[3].status = 200;
    scripts[4].status = 200; scripts[4].body = (const u8 *)pg; scripts[4].body_len = 7;
    src.p = big; src.len = 40000; src.off = 0;
    rc = host_print_stream("big", buf_src, &src, &pages, &svc);
    check(rc == 0, "streambuf: rc 0");
    check(reqlog_count_prefix("PRINT DATA") == 3, "streambuf: 40KB -> 3 DATA (16KB 単位)");
    check(reqlog_has_prefix("PRINT DATA 8 16384"), "streambuf: DATA declares 16384");
    check(reqlog_has_prefix("PRINT DATA 8 7232"), "streambuf: last DATA declares remainder 7232");
    check(fk_total_write == 40000, "streambuf: total write == 40000 (宣言長合計)");
}

/* ========================================================================= */
int main(void)
{
    u32 i;
    for (i = 0; i < (u32)sizeof(g_pat); i++) g_pat[i] = (u8)i;

    g_fake.host_open = fk_open_fn;
    g_fake.host_status = fk_status_fn;
    g_fake.host_read = fk_read_fn;
    g_fake.host_write = fk_write_fn;
    g_fake.host_close = fk_close_fn;
    g_fake.get_tick = fk_get_tick;
    g_fake.sys_yield = fk_sys_yield;

    case_time();
    case_get_stream();
    case_get_404_status_order();
    case_get_url_too_long();
    case_get_sink_abort();
    case_open_stale_up();
    case_open_stale_timeout();
    case_second_stage_stale();
    case_status_timeout();
    case_noprogress_positive();
    case_noprogress_absolute_mutant();
    case_nosys();
    case_open_again_yield();
    case_open_full_yield();
    case_write_again_yield();
    case_write_noprogress_positive();
    case_write_noprogress_absolute_mutant();
    case_clip_put_ok();
    case_clip_put_empty();
    case_clip_get_503();
    case_write_inval_business();
    case_write_inval_again();
    case_write_inval_stale();
    case_print_text_70k();
    case_print_stream_onebyte();
    case_print_stream_empty();
    case_print_stream_abort();
    case_print_409_cleanup();
    case_print_stream_bufsrc();

    if (failures) {
        fprintf(stderr, "host_lib_host: FAIL %d/%d\n", checks - failures, checks);
        return 1;
    }
    printf("host_lib_host: PASS %d/%d\n", checks, checks);
    return 0;
}
