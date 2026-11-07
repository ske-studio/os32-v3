/* ======================================================================== */
/*  SFS_CLIENT.C — SerialFS の要求 / 応答 (規則は sfs_client.h の頭)         */
/* ======================================================================== */
#include "serialfs.h"
#include "os32_kapi_shared.h"   /* OS32_ERR_* */

void sfs_client_init(SfsClient *c, const SfsIo *io, void *ctx, u32 baud)
{
    u32 i;
    u8 *p = (u8 *)c;

    for (i = 0; i < (u32)sizeof(*c); i++) p[i] = 0;
    c->io = io;
    c->ctx = ctx;
    c->baud = baud;
    c->max_payload = SFS_MAX_PAYLOAD;
    sfs_dec_reset(&c->dec);
}

int sfs_client_quiesce(SfsClient *c, u32 quiet_ms, u32 max_ms)
{
    u32 start, last, t, quiet, limit;
    int b, n;

    if (!c->io->can_wait(c->ctx)) {
        /* 時間が進まない。読めるだけ捨てて「静まったとは言えない」 */
        for (n = 0; n < SFS_MAX_FRAME; n++) {
            if (c->io->get(c->ctx) < 0) break;
        }
        return 0;
    }
    quiet = sfs_ms_ticks(quiet_ms);
    limit = sfs_ms_ticks(max_ms);
    start = c->io->now(c->ctx);
    last = start;
    for (;;) {
        b = c->io->get(c->ctx);
        t = c->io->now(c->ctx);
        if (b >= 0) {
            last = t;
        } else if ((u32)(t - last) >= quiet) {
            return 1;
        }
        /* **期限は毎周見る** — 相手が送り続けると b < 0 に来ない */
        if ((u32)(t - start) >= limit) return 0;
        if (b < 0) c->io->idle(c->ctx);
    }
}

/* 受け取ってよい応答か。hello のときは nonce で見る */
static int sfs_match(const SfsClient *c, const SfsFrame *f, u8 type,
                     u16 seq, int hello, u32 nonce)
{
    if (hello) {
        if (f->type != (u8)(SFS_T_HELLO | SFS_T_RESP)) return 0;
        if (f->seq != 0 || f->sid == 0) return 0;
        if (f->len < SFS_HELLO_RSP_LEN) return 0;
        if (sfs_get32(f->payload) != 0) return 0;
        if (sfs_get32(f->payload + SFS_HELLO_RSP_NONCE) != nonce) return 0;
        return 1;
    }
    if (f->sid != c->sid || f->seq != seq) return 0;
    if (f->type != (u8)(type | SFS_T_RESP) && f->type != SFS_T_ERR) return 0;
    if (f->len < SFS_STATUS_LEN) return 0;
    return 1;
}

/* tx[0..flen) を送って応答を待つ (再送を含む)。0 = *got に応答 / -1 */
static int sfs_exchange(SfsClient *c, u16 flen, u8 type, u16 seq,
                        int hello, u32 nonce, SfsFrame *got)
{
    u32 trial = sfs_trial_ticks_limit(c->baud, c->max_payload);
    u32 gap = sfs_ms_ticks(SFS_GAP_MS);
    u32 start, last, t;
    int attempt, b, r;
    SfsFrame f;

    for (attempt = 0; attempt <= SFS_RETRIES; attempt++) {
        if (attempt > 0) {
            c->resends++;
            /* 遅れて届く前の試行の応答を読み捨ててから送り直す */
            (void)sfs_client_quiesce(c, SFS_RESYNC_QUIET_MS, SFS_RESYNC_MAX_MS);
        }
        sfs_dec_abort(&c->dec);
        if (c->io->put(c->ctx, c->tx, flen) != 0) {
            c->timeouts++;
            continue;
        }
        start = c->io->now(c->ctx);
        last = start;
        for (;;) {
            b = c->io->get(c->ctx);
            t = c->io->now(c->ctx);
            /* **期限は送り終えた時点から固定。** 受け取らなかったフレームで
             * 延ばさない。毎周見る (ごみが流れ続けても抜ける)。 */
            if ((u32)(t - start) >= trial) {
                c->timeouts++;
                break;
            }
            if (b < 0) {
                if (sfs_dec_busy(&c->dec) && (u32)(t - last) >= gap)
                    sfs_dec_abort(&c->dec);   /* フレームの途中で途切れた */
                c->io->idle(c->ctx);
                continue;
            }
            last = t;
            r = sfs_dec_feed(&c->dec, (u8)b, &f);
            if (r != 1) continue;
            if (sfs_match(c, &f, type, seq, hello, nonce)) {
                *got = f;
                return 0;
            }
            c->stray++;
        }
    }
    return -1;
}

static void sfs_note_fail(SfsClient *c)
{
    if (c->fails < 255) c->fails++;
    if (c->fails >= SFS_DEAD_AFTER && c->dead == SFS_DEAD_NONE)
        c->dead = SFS_DEAD_FAILS;
}

int sfs_client_hello(SfsClient *c, u32 nonce)
{
    u8 pl[SFS_HELLO_REQ_LEN];
    u16 flen;
    SfsFrame f;

    if (!c->io->can_wait(c->ctx)) return OS32_ERR_IO;
    sfs_put16(pl, (u16)SFS_VERSION);
    sfs_put16(pl + SFS_HELLO_REQ_MAX, c->max_payload);
    sfs_put32(pl + SFS_HELLO_REQ_NONCE, nonce);
    flen = sfs_encode(c->tx, SFS_T_HELLO, 0, 0, pl, SFS_HELLO_REQ_LEN);
    if (sfs_exchange(c, flen, SFS_T_HELLO, 0, 1, nonce, &f) != 0)
        return OS32_ERR_IO;
    if (sfs_get16(f.payload + SFS_HELLO_RSP_VER) != SFS_VERSION ||
        sfs_get16(f.payload + SFS_HELLO_RSP_MAX) < SFS_MAX_PAYLOAD) return OS32_ERR_IO;
    if (sfs_get16(f.payload + SFS_HELLO_RSP_MAX) < c->max_payload)
        /* A smaller peer falls back to the serial wire limit. */
        c->max_payload = SFS_MAX_PAYLOAD;
    c->dec.max_payload = c->max_payload;
    c->sid = f.sid;
    c->seq = 0;
    c->fails = 0;
    c->dead = SFS_DEAD_NONE;
    return 0;
}

static void sfs_note_rtt(SfsClient *c, u32 lo, u32 hi, int valid)
{
    u32 end_lo, end_hi;
    unsigned long long elapsed;
    if (!valid || c->time_us(&end_lo, &end_hi) != 0) return;
    elapsed = (((unsigned long long)end_hi << 32) | end_lo) -
              (((unsigned long long)hi << 32) | lo);
    /* Saturate a single sample rather than wrap after 71 minutes. */
    if (elapsed > U32_MAX) elapsed = U32_MAX;
    c->rtt_total_us += elapsed;
    c->rtt_samples++;
    if ((u32)elapsed > c->rtt_max_us) c->rtt_max_us = (u32)elapsed;
}

int sfs_client_call(SfsClient *c, u8 type, const u8 *payload, u16 len,
                    const u8 **resp, u16 *resp_len)
{
    u16 flen;
    SfsFrame f;
    u32 lo = 0, hi = 0;
    int timed;

    if (c->sid == 0 || c->dead != SFS_DEAD_NONE) return OS32_ERR_IO;
    if (!c->io->can_wait(c->ctx)) return OS32_ERR_IO;
    /* 番号は周回させない。使い切ったらこの要求は失敗、セッションは閉じる */
    if (sfs_seq_next(&c->seq) != 0) {
        c->dead = SFS_DEAD_SEQ;
        return OS32_ERR_IO;
    }
    flen = sfs_encode_limit(c->tx, type, c->sid, c->seq, payload, len, c->max_payload);
    if (flen == 0) return OS32_ERR_INVAL;
    c->calls++;
    timed = c->time_us && c->time_us(&lo, &hi) == 0;
    if (sfs_exchange(c, flen, type, c->seq, 0, 0, &f) != 0) {
        sfs_note_rtt(c, lo, hi, timed);
        sfs_note_fail(c);
        return OS32_ERR_IO;
    }
    sfs_note_rtt(c, lo, hi, timed);
    if (f.type == SFS_T_ERR) {
        /* ホストがこのセッションを知らない (再起動した) — 続けても同じ */
        c->dead = SFS_DEAD_STALE;
        return OS32_ERR_IO;
    }
    c->fails = 0;
    *resp = f.payload;
    *resp_len = f.len;
    return 0;
}

int sfs_client_oneway(SfsClient *c, u8 type, const u8 *payload, u16 len)
{
    u16 flen = sfs_encode_limit(c->tx, type, c->sid, 0, payload, len, c->max_payload);

    if (flen == 0) return OS32_ERR_INVAL;
    return c->io->put(c->ctx, c->tx, flen) == 0 ? 0 : OS32_ERR_IO;
}

int sfs_client_oneway_deadline(SfsClient *c, u8 type, const u8 *payload,
                                u16 len, u32 deadline)
{
    if (!c->io->can_wait(c->ctx)) return OS32_ERR_IO;
    if ((i32)(c->io->now(c->ctx) - deadline) >= 0) return OS32_ERR_TIMEOUT;
    u16 flen = sfs_encode_limit(c->tx, type, c->sid, 0, payload, len, c->max_payload);
    if (!flen) return OS32_ERR_INVAL;
    int rc = c->io->put_until ?
        c->io->put_until(c->ctx, c->tx, flen, deadline) :
        c->io->put(c->ctx, c->tx, flen);
    if (rc == OS32_ERR_TIMEOUT ||
        (i32)(c->io->now(c->ctx) - deadline) >= 0) return OS32_ERR_TIMEOUT;
    return rc == 0 ? 0 : OS32_ERR_IO;
}

/* Return the send count on success, or a negative error.
 * LAN retries share one absolute deadline, including time spent in put/get.
 * Do not quiesce here: a delayed reply to this same sid/seq is still valid. */
#define SFS_LAN_RETRY_TICKS 10u
static int sfs_exchange_deadline(SfsClient *c, u16 flen, u8 type, u16 seq,
                                 int hello, u32 nonce, SfsFrame *got, u32 deadline)
{
    u32 retry = c->io->now(c->ctx);
    int sent = 0;
    sfs_dec_abort(&c->dec);
    for (;;) {
        u32 now = c->io->now(c->ctx);
        if ((i32)(now - deadline) >= 0) {
            c->timeouts++;
            return OS32_ERR_HOSTDOWN;
        }
        if ((i32)(now - retry) >= 0) {
            if (sent) c->resends++;
            if (c->io->put_until)
                (void)c->io->put_until(c->ctx, c->tx, flen, deadline);
            else
                (void)c->io->put(c->ctx, c->tx, flen);
            sent++;
            retry = c->io->now(c->ctx) + SFS_LAN_RETRY_TICKS;
        }
        int b = c->io->get(c->ctx);
        if ((i32)(c->io->now(c->ctx) - deadline) >= 0) {
            c->timeouts++;
            return OS32_ERR_HOSTDOWN;
        }
        if (b < 0) {
            c->io->idle(c->ctx);
            continue;
        }
        SfsFrame f;
        if (sfs_dec_feed(&c->dec, (u8)b, &f) != 1) continue;
        if ((hello && f.type == (SFS_T_HELLO | SFS_T_RESP) && f.seq == 0 && f.sid) ||
            (!hello && f.sid == c->sid && f.seq == seq &&
             (f.type == (u8)(type | SFS_T_RESP) || f.type == SFS_T_ERR))) {
            if (f.len < (hello ? SFS_HELLO_RSP_LEN : SFS_STATUS_LEN))
                return OS32_ERR_IO;
        }
        if (sfs_match(c, &f, type, seq, hello, nonce)) {
            *got = f;
            return sent;
        }
        c->stray++;
    }
}

int sfs_client_hello_deadline(SfsClient *c, u32 nonce, u32 deadline)
{
    u8 pl[SFS_HELLO_REQ_LEN];
    SfsFrame f;
    if (!c->io->can_wait(c->ctx)) return OS32_ERR_IO;
    sfs_put16(pl, SFS_VERSION);
    sfs_put16(pl + SFS_HELLO_REQ_MAX, c->max_payload);
    sfs_put32(pl + SFS_HELLO_REQ_NONCE, nonce);
    u16 flen = sfs_encode(c->tx, SFS_T_HELLO, 0, 0, pl, sizeof(pl));
    int rc = sfs_exchange_deadline(c, flen, SFS_T_HELLO, 0, 1, nonce, &f, deadline);
    if (rc < 0) return rc;
    if (sfs_get16(f.payload + SFS_HELLO_RSP_VER) != SFS_VERSION ||
        sfs_get16(f.payload + SFS_HELLO_RSP_MAX) < SFS_MAX_PAYLOAD)
        return OS32_ERR_IO;
    if (sfs_get16(f.payload + SFS_HELLO_RSP_MAX) < c->max_payload)
        c->max_payload = SFS_MAX_PAYLOAD;
    c->dec.max_payload = c->max_payload;
    c->sid = f.sid;
    c->seq = 0;
    c->fails = 0;
    c->dead = SFS_DEAD_NONE;
    return 0;
}

static int sfs_call_deadline(SfsClient *c, u8 op, const void *in, u16 n,
                             struct SfsReply *out, u32 deadline, int *sent)
{
    SfsFrame f;
    if (!out) return OS32_ERR_INVAL;
    out->length = 0;
    if ((n && !in) || (out->capacity && !out->data)) return OS32_ERR_INVAL;
    if (!c->io->can_wait(c->ctx)) return OS32_ERR_IO;
    if ((i32)(c->io->now(c->ctx) - deadline) >= 0) return OS32_ERR_HOSTDOWN;
    if (!c->sid) return OS32_ERR_HOSTDOWN;
    if (sfs_seq_next(&c->seq) != 0) return OS32_ERR_IO;
    u16 flen = sfs_encode_limit(c->tx, op, c->sid, c->seq, in, n, c->max_payload);
    if (!flen) return OS32_ERR_INVAL;
    c->calls++;
    int rc = sfs_exchange_deadline(c, flen, op, c->seq, 0, 0, &f, deadline);
    if (rc < 0) return rc;
    if (sent) *sent = rc;
    if (f.type == SFS_T_ERR) {
        c->sid = 0;
        return OS32_ERR_STALE;
    }
    out->length = f.len;
    if (out->capacity < f.len) return OS32_ERR_NOSPC;
    for (u16 i = 0; i < f.len; i++) ((u8 *)out->data)[i] = f.payload[i];
    return 0;
}

int sfs_client_call_deadline(SfsClient *c, u8 op, const void *in, u16 n,
                             struct SfsReply *out, u32 deadline)
{
    return sfs_call_deadline(c, op, in, n, out, deadline, 0);
}

int sfs_request_deadline(u8 op, const void *in, u16 n,
                         struct SfsReply *out, u32 deadline)
{
    if (!out) return OS32_ERR_INVAL;
    out->length = 0;
    int rc = sfs_ensure_session(deadline);
    if (rc) return rc;
    /* Only this mutation endpoint uses C1's borrowed client for communication. */
    SfsClient *c = (SfsClient *)sfs_persistent_client();
    if (!c) return OS32_ERR_HOSTDOWN;
    for (int attempt = 0; ; attempt++) {
        int sent = 0;
        rc = sfs_call_deadline(c, op, in, n, out, deadline, &sent);
        if (rc != OS32_ERR_STALE) return rc;
        sfs_persistent_stale();
        /* Only a single send proves the request was never executed. */
        if (attempt == 1 || sent != 1) return rc;
        rc = sfs_ensure_session(deadline);
        if (rc) return rc;
    }
}
