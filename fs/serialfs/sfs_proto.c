/* ======================================================================== */
/*  SFS_PROTO.C — SerialFS のフレームと時間の規則 (純粋)                    */
/*                                                                          */
/*  形式の正典は fs/serialfs/sfs_proto.h の頭。ホスト側の写しは                       */
/*  tools/serialfs_host.py (tools/tests/test_serialfs.py が相互に照合する)。 */
/*  カーネルの外 (ホスト試験) でもそのまま組めるよう、依存は crc32 だけ。   */
/* ======================================================================== */
#include "sfs_proto.h"
#include "config.h"
#include "crc32.h"
#include "le_internal.h"

void sfs_put16(u8 *p, u16 v)
{
    le_write16(p, v);
}

void sfs_put32(u8 *p, u32 v)
{
    le_write32(p, v);
}

u16 sfs_get16(const u8 *p)
{
    return le_read16(p);
}

u32 sfs_get32(const u8 *p)
{
    return le_read32(p);
}

u32 sfs_crc32(const u8 *p, u32 n)
{
    return crc32_calc(p, n);
}

u16 sfs_encode(u8 *out, u8 type, u32 sid, u16 seq,
               const u8 *payload, u16 len)
{
    return sfs_encode_limit(out, type, sid, seq, payload, len, SFS_MAX_PAYLOAD);
}

u16 sfs_encode_limit(u8 *out, u8 type, u32 sid, u16 seq,
                     const u8 *payload, u16 len, u16 limit)
{
    u16 i;
    u32 crc;

    if (limit > SFS_LAN_MAX_PAYLOAD || len > limit) return 0;
    out[0] = SFS_ENQ;
    out[SFS_OFF_MAGIC1] = SFS_MAGIC1;
    out[SFS_OFF_MAGIC2] = SFS_MAGIC2;
    out[SFS_OFF_TYPE] = type;
    sfs_put32(out + SFS_OFF_SID, sid);
    sfs_put16(out + SFS_OFF_SEQ, seq);
    sfs_put16(out + SFS_OFF_LEN, len);
    for (i = 0; i < len; i++) out[SFS_HDR_LEN + i] = payload[i];
    /* CRC は種別 (off 3) からペイロードの末尾まで。ENQ と 'SF' は含めない */
    crc = sfs_crc32(out + SFS_OFF_TYPE, (u32)(SFS_HDR_LEN - SFS_OFF_TYPE + len));
    sfs_put32(out + SFS_HDR_LEN + len, crc);
    return (u16)(SFS_HDR_LEN + len + SFS_CRC_LEN);
}

void sfs_dec_reset(SfsDec *d)
{
    d->pos = 0;
    d->need = 0;
    d->bad_crc = 0;
    d->bad_len = 0;
    d->max_payload = SFS_MAX_PAYLOAD;
}

void sfs_dec_abort(SfsDec *d)
{
    d->pos = 0;
    d->need = 0;
}

int sfs_dec_busy(const SfsDec *d)
{
    return d->pos != 0;
}

int sfs_dec_feed(SfsDec *d, u8 byte, SfsFrame *out)
{
    u16 len;
    u32 crc;

    if (d->pos == 0) {
        /* ENQ 待ち。それ以外のバイトは読み捨てる */
        if (byte == SFS_ENQ) d->buf[d->pos++] = byte;
        return 0;
    }
    if (d->pos == SFS_OFF_MAGIC1 || d->pos == SFS_OFF_MAGIC2) {
        u8 want = (d->pos == SFS_OFF_MAGIC1) ? SFS_MAGIC1 : SFS_MAGIC2;
        if (byte != want) {
            /* 印が合わない。この 1 バイトが次の ENQ なら、そこから始め直す */
            d->pos = 0;
            d->need = 0;
            if (byte == SFS_ENQ) d->buf[d->pos++] = byte;
            return 0;
        }
        d->buf[d->pos++] = byte;
        return 0;
    }

    d->buf[d->pos++] = byte;
    if (d->pos == SFS_HDR_LEN) {
        /* **長さは確保・コピーの前に上限と照合する** (§1-v2 B-5') */
        len = sfs_get16(d->buf + SFS_OFF_LEN);
        if (len > d->max_payload || len > SFS_LAN_MAX_PAYLOAD) {
            d->bad_len++;
            d->pos = 0;
            d->need = 0;
            return -1;
        }
        d->need = (u16)(SFS_HDR_LEN + len + SFS_CRC_LEN);
        return 0;
    }
    if (d->need == 0 || d->pos < d->need) return 0;

    /* フレーム全体が揃った。ここで初めて判定する */
    len = (u16)(d->need - SFS_HDR_LEN - SFS_CRC_LEN);
    crc = sfs_crc32(d->buf + SFS_OFF_TYPE, (u32)(SFS_HDR_LEN - SFS_OFF_TYPE + len));
    d->pos = 0;
    d->need = 0;
    if (crc != sfs_get32(d->buf + SFS_HDR_LEN + len)) {
        d->bad_crc++;
        return -1;
    }
    out->type = d->buf[SFS_OFF_TYPE];
    out->sid = sfs_get32(d->buf + SFS_OFF_SID);
    out->seq = sfs_get16(d->buf + SFS_OFF_SEQ);
    out->len = len;
    out->payload = d->buf + SFS_HDR_LEN;
    return 1;
}

u32 sfs_byte_us(u32 baud)
{
    if (baud == 0) baud = SYS_SERIAL_BAUD;
    /* 8N1 = スタート 1 + データ 8 + ストップ 1 = 10 ビット。切り上げ */
    return (SFS_SERIAL_BITS_PER_BYTE * USEC_PER_SEC + baud - 1) / baud;
}

u32 sfs_ms_ticks(u32 ms)
{
    return (ms + SFS_TICK_MS - 1) / SFS_TICK_MS + 1;
}

u32 sfs_trial_ticks(u32 baud)
{
    return sfs_trial_ticks_limit(baud, SFS_MAX_PAYLOAD);
}

u32 sfs_trial_ticks_limit(u32 baud, u16 limit)
{
    u32 frame_ms = ((u32)(SFS_HDR_LEN + limit + SFS_CRC_LEN) * sfs_byte_us(baud) * 2UL + 999UL)
                   / SFS_USEC_PER_MSEC;
    return sfs_ms_ticks((u32)SFS_FIRST_BYTE_MS + frame_ms + (u32)SFS_SLACK_MS);
}

int sfs_seq_next(u16 *seq)
{
    if (*seq >= SFS_SEQ_LAST) return -1;
    *seq = (u16)(*seq + 1);
    return 0;
}

int sfs_put_path(u8 *out, u16 room, const char *path)
{
    u16 n = 0;

    while (path[n]) {
        if (n >= SFS_PATH_MAX) return -1;
        n++;
    }
    if ((u32)n + 1 > room) return -1;
    out[0] = (u8)n;
    {
        u16 i;
        for (i = 0; i < n; i++) out[1 + i] = (u8)path[i];
    }
    return (int)(n + 1);
}
