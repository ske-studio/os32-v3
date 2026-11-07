/* Purpose-specific clients over the host_call transport. Legacy request lines
 * and streaming callbacks remain supported. */
#include "host_call_internal.h"
#include "os32api.h"

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


static int service_error(int rc)
{
    if (rc >= 0) return 0;
    switch (rc) {
    case OS32_ERR_HOSTDOWN:
    case OS32_ERR_STALE: return HOST_ELINK;
    case OS32_ERR_NOSYS: return HOST_ENODEV;
    case OS32_ERR_TIMEOUT: return HOST_ETIMEOUT;
    case OS32_ERR_INVAL: return HOST_EINVAL;
    case HOST_ESERVICE: return HOST_ESERVICE;
    case HOST_EABORT: return HOST_EABORT;
    default: return HOST_EIO;
    }
}


struct ServiceBuffer { u8 *out; u32 cap, used; };
static int service_buffer(void *ctx, const u8 *buf, u32 n)
{
    struct ServiceBuffer *b = ctx;
    if (n > b->cap - b->used) return OS32_ERR_NOSPC;
    for (u32 i = 0; i < n; i++) b->out[b->used++] = buf[i];
    return 0;
}

static int request(SB *sb, const void *in, u32 len, u8 *out, u32 cap,
                   int *st, u32 *got)
{
    struct ServiceBuffer b = { out, cap, 0 };
    int rc;
    if (sb->ov) return HOST_EINVAL;
    rc = host_exchange(sb->p, sb->len, in, len, cap ? service_buffer : 0, &b, st,
                       HOST_CALL_TIMEOUT_MS);
    if (got) *got = b.used;
    return service_error(rc);
}

int host_time(char out[HOST_TIME_BUF])
{
    SB sb;
    int st = 0;
    u32 got = 0;
    int rc;
    if (!out) return HOST_EINVAL;
    sb_init(&sb, "TIME", 4);
    sb.len = 4;
    rc = request(&sb, 0, 0, (u8 *)out, HOST_TIME_LEN, &st, &got);
    if (rc) return rc;
    if (st != 200) return HOST_ESERVICE;
    out[got] = 0;
    return 0;
}

struct ServiceSink { host_sink_fn sink; void *ud; int *status; int *http; };
static int service_sink(void *ctx, const u8 *buf, u32 n)
{
    struct ServiceSink *s = ctx;
    if (s->http) *s->http = (int)*s->status;
    if (!s->http && *s->status != 200) return HOST_ESERVICE;
    return s->sink && s->sink(s->ud, buf, n) ? HOST_EABORT : 0;
}

static int stream_request(SB *sb, host_sink_fn sink, void *ud, int *http,
                          u32 *nbytes, u32 *svc_status)
{
    int st = 0;
    int *status = http ? http : &st;
    struct ServiceSink s = { sink, ud, status, http };
    int rc;
    if (sb->ov) return HOST_EINVAL;
    rc = host_exchange(sb->p, sb->len, 0, 0, service_sink, &s, status,
                       HOST_CALL_TIMEOUT_MS);
    if (svc_status && st != 200) *svc_status = st;
    if (rc < 0) return service_error(rc);
    if (!http && st != 200) return HOST_ESERVICE;
    if (nbytes) *nbytes = (u32)rc;
    return 0;
}

int host_get(const char *url, host_sink_fn sink, void *ud,
             int *http_status, u32 *nbytes)
{
    char req[HOST_REQ_MAX];
    SB sb;
    int status;
    sb_init(&sb, req, sizeof(req));
    sb_str(&sb, "GET "); sb_str(&sb, url);
    return stream_request(&sb, sink, ud, http_status ? http_status : &status, nbytes, 0);
}

int host_clip_get(host_sink_fn sink, void *ud, u32 *nbytes, u32 *svc_status)
{
    SB sb;
    sb_init(&sb, "CLIP GET", 8); sb.len = 8;
    return stream_request(&sb, sink, ud, 0, nbytes, svc_status);
}

int host_clip_put(const char *buf, u32 len, u32 *svc_status)
{
    char req[64];
    SB sb;
    int st = 0;
    int rc;
    if (!len || len > HOST_CLIP_MAX) return HOST_EINVAL;
    sb_init(&sb, req, sizeof(req));
    sb_str(&sb, "CLIP PUT "); sb_uint(&sb, len);
    rc = request(&sb, buf, len, 0, 0, &st, 0);
    if (svc_status && st != 200) *svc_status = st;
    if (rc) return rc;
    return st == 200 ? 0 : HOST_ESERVICE;
}

static u8 g_stream_buf[HOST_STREAM_BUF];

static int print_open_job(const char *name, u32 *job, u32 *svc_status)
{
    char req[HOST_REQ_MAX];
    u8 body[64];
    int st = 0;
    u32 got = 0;
    SB sb;
    int rc;
    sb_init(&sb, req, sizeof(req));
    sb_str(&sb, "PRINT OPEN "); sb_str(&sb, name); sb_str(&sb, " text");
    rc = request(&sb, 0, 0, body, sizeof(body), &st, &got);
    if (svc_status && st != 200) *svc_status = st;
    if (rc) return rc;
    if (st != 200) return HOST_ESERVICE;
    return h_first_uint(body, got, job) ? HOST_EIO : 0;
}

static int print_data(u32 job, const u8 *data, u32 decl, u32 *svc_status)
{
    char req[64];
    int st = 0;
    SB sb;
    int rc;
    sb_init(&sb, req, sizeof(req));
    sb_str(&sb, "PRINT DATA "); sb_uint(&sb, job); sb_str(&sb, " "); sb_uint(&sb, decl);
    rc = request(&sb, data, decl, 0, 0, &st, 0);
    if (svc_status && st != 200) *svc_status = st;
    if (rc) return rc;
    return st == 200 ? 0 : HOST_ESERVICE;
}

static int print_close_job(u32 job, u32 *pages, u32 *svc_status)
{
    char req[64];
    u8 body[64];
    int st = 0;
    u32 got = 0, pg;
    SB sb;
    int rc;
    sb_init(&sb, req, sizeof(req));
    sb_str(&sb, "PRINT CLOSE "); sb_uint(&sb, job);
    rc = request(&sb, 0, 0, body, sizeof(body), &st, &got);
    if (svc_status && st != 200) *svc_status = st;
    if (rc) return rc;
    if (st != 200) return HOST_ESERVICE;
    if (!h_first_uint(body, got, &pg) && pages) *pages = pg;
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
