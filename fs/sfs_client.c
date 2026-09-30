/* ======================================================================== */
/*  SFS_CLIENT.C — SerialFS の要求 / 応答 (規則は sfs_client.h の頭)         */
/* ======================================================================== */
#include "sfs_client.h"
#include "os32_kapi_shared.h"   /* OS32_ERR_* */

void sfs_client_init(SfsClient *c, const SfsIo *io, void *ctx, u32 baud)
{
    u32 i;
    u8 *p = (u8 *)c;

    for (i = 0; i < (u32)sizeof(*c); i++) p[i] = 0;
    c->io = io;
    c->ctx = ctx;
    c->baud = baud;
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
        if (f->len < 12) return 0;
        if (sfs_get32(f->payload) != 0) return 0;
        if (sfs_get32(f->payload + 8) != nonce) return 0;
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
    u32 trial = sfs_trial_ticks(c->baud);
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
    u8 pl[8];
    u16 flen;
    SfsFrame f;

    if (!c->io->can_wait(c->ctx)) return OS32_ERR_IO;
    sfs_put16(pl, (u16)SFS_VERSION);
    sfs_put16(pl + 2, (u16)SFS_MAX_PAYLOAD);
    sfs_put32(pl + 4, nonce);
    flen = sfs_encode(c->tx, SFS_T_HELLO, 0, 0, pl, 8);
    if (sfs_exchange(c, flen, SFS_T_HELLO, 0, 1, nonce, &f) != 0)
        return OS32_ERR_IO;
    c->sid = f.sid;
    c->seq = 0;
    c->fails = 0;
    c->dead = SFS_DEAD_NONE;
    return 0;
}

int sfs_client_call(SfsClient *c, u8 type, const u8 *payload, u16 len,
                    const u8 **resp, u16 *resp_len)
{
    u16 flen;
    SfsFrame f;

    if (c->sid == 0 || c->dead != SFS_DEAD_NONE) return OS32_ERR_IO;
    if (!c->io->can_wait(c->ctx)) return OS32_ERR_IO;
    /* 番号は周回させない。使い切ったらこの要求は失敗、セッションは閉じる */
    if (sfs_seq_next(&c->seq) != 0) {
        c->dead = SFS_DEAD_SEQ;
        return OS32_ERR_IO;
    }
    flen = sfs_encode(c->tx, type, c->sid, c->seq, payload, len);
    if (flen == 0) return OS32_ERR_INVAL;
    c->calls++;
    if (sfs_exchange(c, flen, type, c->seq, 0, 0, &f) != 0) {
        sfs_note_fail(c);
        return OS32_ERR_IO;
    }
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
    u16 flen = sfs_encode(c->tx, type, c->sid, 0, payload, len);

    if (flen == 0) return OS32_ERR_INVAL;
    return c->io->put(c->ctx, c->tx, flen) == 0 ? 0 : OS32_ERR_IO;
}
