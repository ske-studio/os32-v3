/* ======================================================================== */
/*  LIBOS32HOST.C — Host Services クライアント層の実体 (票 N3 §1)            */
/*                                                                          */
/*  KAPI v51 の host_open / host_status / host_read / host_write /          */
/*  host_close を、AGAIN/FULL の yield 待ち・STALE の生存判定・無進捗期限・   */
/*  PRINT の多段・sink/src ストリーミングで包む。C89 [C1]、malloc しない。   */
/*  `kapi` は crt0 が設定する大域 (cfg_backend.c と同じ)。                    */
/*                                                                          */
/*  ホスト TDD (tools/tests/host_lib_host.c) はこの翻訳単位を #include し、   */
/*  贋 `kapi` (状態機械) と get_tick を与える。HOST_LIB_TEST_SEAMS を定義     */
/*  すると無進捗期限を「絶対期限」に落とす変異点が開く (回帰の否定側)。       */
/* ======================================================================== */

#include "libos32host.h"
#include "os32api.h"     /* KernelAPI, OS32_ERR_* */

extern KernelAPI *kapi;  /* crt0_c.c / ホスト試験の贋テーブル */

/* 最初の open が成功したら立つ。以降の STALE は待たず即 ELINK にする。 */
static int g_link_up;

/* ======================================================================== */
/*  無進捗期限 (30 秒)。status 0 / read>0 / write>0 / open 成功で基準取り直し */
/* ======================================================================== */

typedef struct { u32 mark; } NoProg;

static u32 h_tick(void) { return kapi->get_tick(); }

#ifdef HOST_LIB_TEST_SEAMS
/* 回帰の否定側: これを 1 にすると基準を取り直さない = 絶対期限になり、
 * 20 秒間隔 60 秒の転送が 30 秒で ETIMEOUT に落ちる (正しい実装では成功)。 */
int host__np_absolute = 0;
static void np_reset(NoProg *d) { if (!host__np_absolute) d->mark = h_tick(); }
#else
static void np_reset(NoProg *d) { d->mark = h_tick(); }
#endif

static int np_expired(const NoProg *d)
{
    return (u32)(h_tick() - d->mark) >= (u32)HOST_NOPROG_TICKS;
}

/* ======================================================================== */
/*  文字列組み立て (要求行) と整数解析                                        */
/* ======================================================================== */

typedef struct { char *p; u32 cap; u32 len; int ov; } SB;

static void sb_init(SB *s, char *p, u32 cap) { s->p = p; s->cap = cap; s->len = 0; s->ov = 0; }

static void sb_str(SB *s, const char *z)
{
    while (*z) {
        if (s->len >= s->cap) { s->ov = 1; return; }
        s->p[s->len++] = *z++;
    }
}

static void sb_uint(SB *s, u32 v)
{
    char tmp[10];
    int i = 0;
    if (v == 0) {
        if (s->len >= s->cap) { s->ov = 1; return; }
        s->p[s->len++] = '0';
        return;
    }
    while (v) { tmp[i++] = (char)('0' + (int)(v % 10)); v /= 10; }
    while (i > 0) {
        i--;
        if (s->len >= s->cap) { s->ov = 1; return; }
        s->p[s->len++] = tmp[i];
    }
}

/* 本文中の最初の 10 進整数を拾う ("job 7" -> 7、"pages 1" -> 1)。 */
static int h_first_uint(const u8 *b, u32 len, u32 *val)
{
    u32 i = 0, v = 0;
    while (i < len && !(b[i] >= '0' && b[i] <= '9')) i++;
    if (i >= len) return -1;
    while (i < len && b[i] >= '0' && b[i] <= '9') { v = v * 10 + (u32)(b[i] - '0'); i++; }
    *val = v;
    return 0;
}

/* ======================================================================== */
/*  KAPI 待ちループ                                                          */
/* ======================================================================== */

/* open。AGAIN/FULL は yield 待ち、最初の open だけ STALE を 3 秒待つ。 */
static int wait_open(const char *req, u32 len, NoProg *np, i32 *out_h)
{
    u32 t0 = h_tick();
    for (;;) {
        i32 h = kapi->host_open(req, len);
        if (h >= 0) { g_link_up = 1; np_reset(np); *out_h = h; return 0; }
        switch (h) {
        case OS32_ERR_AGAIN:
        case OS32_ERR_FULL:
            if (np_expired(np)) return HOST_ETIMEOUT;
            kapi->sys_yield();
            break;
        case OS32_ERR_STALE:
            if (g_link_up) return HOST_ELINK;
            if ((u32)(h_tick() - t0) >= (u32)HOST_OPEN_STALE_TICKS) return HOST_ELINK;
            kapi->sys_yield();
            break;
        case OS32_ERR_NOSYS:
            return HOST_ENODEV;
        case OS32_ERR_INVAL:
            return HOST_EINVAL;
        default:
            return HOST_EIO;
        }
    }
}

/* 業務 RESPONSE を待つ。0 で status/length 確定、STALE で ELINK。 */
static int wait_status(i32 h, NoProg *np, u32 *status, u32 *length)
{
    for (;;) {
        i32 rc = kapi->host_status(h, status, length);
        if (rc == 0) { np_reset(np); return 0; }
        if (rc == OS32_ERR_AGAIN) {
            if (np_expired(np)) return HOST_ETIMEOUT;
            kapi->sys_yield();
            continue;
        }
        if (rc == OS32_ERR_STALE) return HOST_ELINK;
        return HOST_EIO;
    }
}

/* 本文を read ループで sink へ流す (溜めない)。total に総バイト数。 */
static int drain_sink(i32 h, NoProg *np, host_sink_fn sink, void *ud, u32 *total)
{
    u8 buf[HOST_WRITE_CHUNK];
    u32 got = 0;
    for (;;) {
        i32 n = kapi->host_read(h, buf, (u32)sizeof(buf));
        if (n == 0) break;
        if (n == OS32_ERR_AGAIN) {
            if (np_expired(np)) return HOST_ETIMEOUT;
            kapi->sys_yield();
            continue;
        }
        if (n == OS32_ERR_STALE) return HOST_ELINK;
        if (n < 0) return HOST_EIO;
        np_reset(np);
        got += (u32)n;
        if (sink && sink(ud, buf, (u32)n)) return HOST_EABORT;
    }
    if (total) *total = got;
    return 0;
}

/* 本文を小さな固定バッファへ (先頭 cap まで保持、残りは捨てる)。 */
static int drain_buf(i32 h, NoProg *np, u8 *buf, u32 cap, u32 *outlen)
{
    u8 scratch[64];
    u32 got = 0;
    for (;;) {
        u8 *dst;
        u32 room;
        i32 n;
        if (got < cap) { dst = buf + got; room = cap - got; }
        else { dst = scratch; room = (u32)sizeof(scratch); }
        n = kapi->host_read(h, dst, room);
        if (n == 0) break;
        if (n == OS32_ERR_AGAIN) {
            if (np_expired(np)) return HOST_ETIMEOUT;
            kapi->sys_yield();
            continue;
        }
        if (n == OS32_ERR_STALE) return HOST_ELINK;
        if (n < 0) return HOST_EIO;
        np_reset(np);
        if (got < cap) {
            u32 add = (u32)n;
            if (got + add > cap) add = cap - got;
            got += add;
        }
    }
    if (outlen) *outlen = got;
    return 0;
}

/* 宣言長 decl バイトを host_write で送り切る (≤1400 ずつ)。
 * write が INVAL を返したら host_status を引いて分岐:
 *   業務 (status 0)  -> ESERVICE (+ *svc)
 *   AGAIN            -> EINVAL (真の引数不正)
 *   STALE            -> ELINK                                              */
static int send_decl(i32 h, NoProg *np, const u8 *data, u32 decl, u32 *svc)
{
    u32 off = 0;
    while (off < decl) {
        u32 want = decl - off;
        i32 n;
        if (want > HOST_WRITE_CHUNK) want = HOST_WRITE_CHUNK;
        n = kapi->host_write(h, data + off, want);
        if (n == OS32_ERR_AGAIN) {
            if (np_expired(np)) return HOST_ETIMEOUT;
            kapi->sys_yield();
            continue;
        }
        if (n == OS32_ERR_STALE) return HOST_ELINK;
        if (n == OS32_ERR_INVAL) {
            u32 st = 0, ln = 0;
            i32 rc = kapi->host_status(h, &st, &ln);
            if (rc == 0) { if (svc) *svc = st; return HOST_ESERVICE; }
            if (rc == OS32_ERR_STALE) return HOST_ELINK;
            if (rc == OS32_ERR_AGAIN) return HOST_EINVAL;
            return HOST_EIO;
        }
        if (n < 0) return HOST_EIO;
        np_reset(np);
        off += (u32)n;
    }
    return 0;
}

/* ======================================================================== */
/*  TIME                                                                     */
/* ======================================================================== */

int host_time(char out[HOST_TIME_BUF])
{
    NoProg np;
    i32 h;
    int rc;
    u32 st = 0, ln = 0, got = 0;

    np_reset(&np);
    rc = wait_open("TIME", 4, &np, &h);
    if (rc) return rc;
    rc = wait_status(h, &np, &st, &ln);
    if (rc) { kapi->host_close(h); return rc; }
    if (st != 200) { kapi->host_close(h); return HOST_ESERVICE; }
    rc = drain_buf(h, &np, (u8 *)out, HOST_TIME_LEN, &got);
    kapi->host_close(h);
    if (rc) return rc;
    out[(got < HOST_TIME_LEN) ? got : HOST_TIME_LEN] = '\0';
    return 0;
}

/* ======================================================================== */
/*  GET (本文は sink へストリーミング、http_status は read の前に書く)        */
/* ======================================================================== */

int host_get(const char *url, host_sink_fn sink, void *ud,
             int *http_status, u32 *nbytes)
{
    NoProg np;
    i32 h;
    int rc;
    u32 st = 0, ln = 0, total = 0;
    char req[HOST_REQ_MAX];
    SB sb;

    sb_init(&sb, req, (u32)sizeof(req));
    sb_str(&sb, "GET ");
    sb_str(&sb, url);
    if (sb.ov) return HOST_EINVAL;    /* url が長すぎて 1400B 超 */

    np_reset(&np);
    rc = wait_open(req, sb.len, &np, &h);
    if (rc) return rc;
    rc = wait_status(h, &np, &st, &ln);
    if (rc) { kapi->host_close(h); return rc; }
    if (http_status) *http_status = (int)st;   /* read ループの前に確定 */
    rc = drain_sink(h, &np, sink, ud, &total);
    kapi->host_close(h);
    if (rc) return rc;
    if (nbytes) *nbytes = total;
    return 0;
}

/* ======================================================================== */
/*  CLIP GET / PUT                                                           */
/* ======================================================================== */

int host_clip_get(host_sink_fn sink, void *ud, u32 *nbytes, u32 *svc_status)
{
    NoProg np;
    i32 h;
    int rc;
    u32 st = 0, ln = 0, total = 0;

    np_reset(&np);
    rc = wait_open("CLIP GET", 8, &np, &h);
    if (rc) return rc;
    rc = wait_status(h, &np, &st, &ln);
    if (rc) { kapi->host_close(h); return rc; }
    if (st != 200) { kapi->host_close(h); if (svc_status) *svc_status = st; return HOST_ESERVICE; }
    rc = drain_sink(h, &np, sink, ud, &total);
    kapi->host_close(h);
    if (rc) return rc;
    if (nbytes) *nbytes = total;
    return 0;
}

int host_clip_put(const char *buf, u32 len, u32 *svc_status)
{
    NoProg np;
    i32 h;
    int rc;
    u32 st = 0, ln = 0;
    char req[64];
    SB sb;

    if (len < 1 || len > HOST_CLIP_MAX) return HOST_EINVAL;

    sb_init(&sb, req, (u32)sizeof(req));
    sb_str(&sb, "CLIP PUT ");
    sb_uint(&sb, len);
    if (sb.ov) return HOST_EINVAL;

    np_reset(&np);
    rc = wait_open(req, sb.len, &np, &h);
    if (rc) return rc;
    rc = send_decl(h, &np, (const u8 *)buf, len, svc_status);
    if (rc) { kapi->host_close(h); return rc; }
    rc = wait_status(h, &np, &st, &ln);
    if (rc) { kapi->host_close(h); return rc; }
    kapi->host_close(h);
    if (st != 200) { if (svc_status) *svc_status = st; return HOST_ESERVICE; }
    return 0;
}

/* ======================================================================== */
/*  PRINT — 多段 (OPEN -> DATA×n -> CLOSE)                                   */
/*                                                                          */
/*  途中で STALE / 業務 4xx-5xx が起きたら CLOSE を送らず job を放置し、      */
/*  開いた host ハンドルは全経路で close する (票 §1 の後始末)。             */
/* ======================================================================== */

static u8 g_stream_buf[HOST_STREAM_BUF];   /* print_stream の詰めバッファ */

/* PRINT OPEN <name> text -> "job <id>"。0 で成功、負で失敗 (ハンドルは
 * この関数内で close 済み)。 */
static int print_open_job(const char *name, u32 *job, u32 *svc_status)
{
    NoProg np;
    i32 h;
    int rc;
    u32 st = 0, ln = 0, bl = 0;
    u8 body[64];
    char req[HOST_REQ_MAX];
    SB sb;

    sb_init(&sb, req, (u32)sizeof(req));
    sb_str(&sb, "PRINT OPEN ");
    sb_str(&sb, name);
    sb_str(&sb, " text");
    if (sb.ov) return HOST_EINVAL;

    np_reset(&np);
    rc = wait_open(req, sb.len, &np, &h);
    if (rc) return rc;
    rc = wait_status(h, &np, &st, &ln);
    if (rc) { kapi->host_close(h); return rc; }
    if (st != 200) { kapi->host_close(h); if (svc_status) *svc_status = st; return HOST_ESERVICE; }
    rc = drain_buf(h, &np, body, (u32)sizeof(body), &bl);
    kapi->host_close(h);
    if (rc) return rc;
    if (h_first_uint(body, bl, job) != 0) return HOST_EIO;
    return 0;
}

/* PRINT DATA <id> <decl> + 本文。0 で成功、負で失敗。 */
static int print_data(u32 job, const u8 *data, u32 decl, u32 *svc_status)
{
    NoProg np;
    i32 h;
    int rc;
    u32 st = 0, ln = 0;
    char req[64];
    SB sb;

    sb_init(&sb, req, (u32)sizeof(req));
    sb_str(&sb, "PRINT DATA ");
    sb_uint(&sb, job);
    sb_str(&sb, " ");
    sb_uint(&sb, decl);
    if (sb.ov) return HOST_EINVAL;

    np_reset(&np);
    rc = wait_open(req, sb.len, &np, &h);
    if (rc) return rc;
    rc = send_decl(h, &np, data, decl, svc_status);
    if (rc) { kapi->host_close(h); return rc; }
    rc = wait_status(h, &np, &st, &ln);
    if (rc) { kapi->host_close(h); return rc; }
    kapi->host_close(h);
    if (st != 200) { if (svc_status) *svc_status = st; return HOST_ESERVICE; }
    return 0;
}

/* PRINT CLOSE <id> -> "pages <n>"。0 で成功、負で失敗。 */
static int print_close_job(u32 job, u32 *pages, u32 *svc_status)
{
    NoProg np;
    i32 h;
    int rc;
    u32 st = 0, ln = 0, bl = 0, pg = 0;
    u8 body[64];
    char req[64];
    SB sb;

    sb_init(&sb, req, (u32)sizeof(req));
    sb_str(&sb, "PRINT CLOSE ");
    sb_uint(&sb, job);
    if (sb.ov) return HOST_EINVAL;

    np_reset(&np);
    rc = wait_open(req, sb.len, &np, &h);
    if (rc) return rc;
    rc = wait_status(h, &np, &st, &ln);
    if (rc) { kapi->host_close(h); return rc; }
    if (st != 200) { kapi->host_close(h); if (svc_status) *svc_status = st; return HOST_ESERVICE; }
    rc = drain_buf(h, &np, body, (u32)sizeof(body), &bl);
    kapi->host_close(h);
    if (rc) return rc;
    if (h_first_uint(body, bl, &pg) == 0 && pages) *pages = pg;
    return 0;
}

int host_print_text(const char *name, const char *buf, u32 len,
                    u32 *pages, u32 *svc_status)
{
    u32 job = 0, off = 0;
    int rc;

    rc = print_open_job(name, &job, svc_status);
    if (rc) return rc;

    while (off < len) {
        u32 decl = len - off;
        if (decl > HOST_DECL_MAX) decl = HOST_DECL_MAX;
        rc = print_data(job, (const u8 *)buf + off, decl, svc_status);
        if (rc) return rc;               /* 後始末: CLOSE を送らない */
        off += decl;
    }
    return print_close_job(job, pages, svc_status);
}

int host_print_stream(const char *name, host_source_fn src, void *ud,
                      u32 *pages, u32 *svc_status)
{
    u32 job = 0;
    int rc;

    rc = print_open_job(name, &job, svc_status);
    if (rc) return rc;

    for (;;) {
        u32 filled = 0;
        /* src が EOF (0) を返すか満杯になるまで詰めてから、実長で宣言する。 */
        while (filled < HOST_STREAM_BUF) {
            i32 g = src(ud, g_stream_buf + filled, HOST_STREAM_BUF - filled);
            if (g == 0) break;
            if (g < 0) return HOST_EABORT;   /* 後始末: CLOSE を送らない */
            filled += (u32)g;
        }
        if (filled == 0) break;              /* これ以上本文が無い */
        rc = print_data(job, g_stream_buf, filled, svc_status);
        if (rc) return rc;                   /* 後始末: CLOSE を送らない */
        if (filled < HOST_STREAM_BUF) break; /* 途中で EOF に当たった = 最終 */
    }
    return print_close_job(job, pages, svc_status);
}
