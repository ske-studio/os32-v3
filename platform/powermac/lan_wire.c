/* PLN1 payload codec; no Ethernet header or device access. */
#include "lan_boot.h"
#include "types.h"
#include "lib/kstring.h"

static void put_be(u8 *p, u64 v, u32 n)
{
    u32 i;
    for (i = 0; i < n; ++i)
        p[i] = (u8)(v >> (8 * (n - 1 - i)));
}

static u64 get_be(const u8 *p, u32 n)
{
    u64 v = 0;
    u32 i;
    for (i = 0; i < n; ++i)
        v = (v << 8) | p[i];
    return v;
}

static int valid_header(const struct lan_header *h)
{
    u32 i;
    if (h->type < PLN1_TYPE_BEACON || h->type > PLN1_FS_RSP)
        return 0;
    if (h->type <= PLN1_TERM_STATE) {
        if (h->payload_bytes > PLN1_PAYLOAD_MAX) return 0;
    } else {
        if (h->payload_bytes > PLN1_FS_FRAME_MAX ||
            (h->type == PLN1_FS_OPEN && h->payload_bytes != 0) ||
            h->seq || h->drop_records || h->drop_bytes || h->request_id ||
            h->opcode || h->status || h->flags) return 0;
    }
    if (h->type <= PLN1_TYPE_LOG || h->type == PLN1_FS_REQ)
        for (i = 0; i < 16; ++i)
            if (h->host_nonce[i]) return 0;
    return 1;
}

int pm_lan_encode_payload(u8 *dst, u32 cap, const struct lan_header *h,
                          const u8 *payload, u32 len, u32 *used)
{
    u32 i;
    if (!dst || !h || !used) return LAN_EINVAL;
    *used = 0;
    if (len != h->payload_bytes || !valid_header(h) || (len && !payload))
        return LAN_EINVAL;
    if (cap < PLN1_HEADER_BYTES || len > cap - PLN1_HEADER_BYTES)
        return LAN_ERANGE;
    put_be(dst + PLN1_OFF_MAGIC, PLN1_MAGIC, 4);
    dst[PLN1_OFF_VERSION] = PLN1_VERSION;
    dst[PLN1_OFF_TYPE] = h->type;
    put_be(dst + PLN1_OFF_HEADER_BYTES, PLN1_HEADER_BYTES, 2);
    for (i = 0; i < 16; ++i) dst[PLN1_OFF_BOOT_ID + i] = h->boot_id[i];
    put_be(dst + PLN1_OFF_SEQ, h->seq, 8);
    put_be(dst + PLN1_OFF_DROP_RECORDS, h->drop_records, 8);
    put_be(dst + PLN1_OFF_DROP_BYTES, h->drop_bytes, 8);
    put_be(dst + PLN1_OFF_REQUEST_ID, h->request_id, 4);
    put_be(dst + PLN1_OFF_OPCODE, h->opcode, 2);
    put_be(dst + PLN1_OFF_STATUS, h->status, 2);
    put_be(dst + PLN1_OFF_PAYLOAD_BYTES, h->payload_bytes, 2);
    put_be(dst + PLN1_OFF_FLAGS, h->flags, 2);
    put_be(dst + PLN1_OFF_RESERVED, 0, 4);
    for (i = 0; i < 16; ++i) dst[PLN1_OFF_HOST_NONCE + i] = h->host_nonce[i];
    for (i = 0; i < len; ++i) dst[PLN1_HEADER_BYTES + i] = payload[i];
    *used = PLN1_HEADER_BYTES + len;
    return LAN_OK;
}

int pm_lan_decode_header(const u8 *src, u32 bytes, struct lan_header *h)
{
    u32 i, len;
    if (!h) return LAN_EINVAL;
    kmemset(h, 0, sizeof(*h));
    if (!src || bytes < PLN1_HEADER_BYTES) return LAN_EINVAL;
    len = (u32)get_be(src + PLN1_OFF_PAYLOAD_BYTES, 2);
    if (get_be(src + PLN1_OFF_MAGIC, 4) != PLN1_MAGIC ||
        src[PLN1_OFF_VERSION] != PLN1_VERSION ||
        get_be(src + PLN1_OFF_HEADER_BYTES, 2) != PLN1_HEADER_BYTES ||
        get_be(src + PLN1_OFF_RESERVED, 4) != 0 ||
        len > bytes - PLN1_HEADER_BYTES) return LAN_EINVAL;
    h->type = src[PLN1_OFF_TYPE];
    for (i = 0; i < 16; ++i) h->boot_id[i] = src[PLN1_OFF_BOOT_ID + i];
    h->seq = get_be(src + PLN1_OFF_SEQ, 8);
    h->drop_records = get_be(src + PLN1_OFF_DROP_RECORDS, 8);
    h->drop_bytes = get_be(src + PLN1_OFF_DROP_BYTES, 8);
    h->request_id = (u32)get_be(src + PLN1_OFF_REQUEST_ID, 4);
    h->opcode = (u16)get_be(src + PLN1_OFF_OPCODE, 2);
    h->status = (u16)get_be(src + PLN1_OFF_STATUS, 2);
    h->payload_bytes = (u16)len;
    h->flags = (u16)get_be(src + PLN1_OFF_FLAGS, 2);
    for (i = 0; i < 16; ++i) h->host_nonce[i] = src[PLN1_OFF_HOST_NONCE + i];
    return valid_header(h) ? LAN_OK : LAN_EINVAL;
}

/* Both structures have matching fields, but copying them as objects would
 * depend on padding; transfer each field explicitly. */
#define COPY_HEADER(d, s) do { \
    u32 j; \
    for (j = 0; j < 16; ++j) { (d)->boot_id[j] = (s)->boot_id[j]; \
        (d)->host_nonce[j] = (s)->host_nonce[j]; } \
    (d)->seq = (s)->seq; (d)->drop_records = (s)->drop_records; \
    (d)->drop_bytes = (s)->drop_bytes; (d)->request_id = (s)->request_id; \
    (d)->opcode = (s)->opcode; (d)->status = (s)->status; \
    (d)->payload_bytes = (s)->payload_bytes; (d)->flags = (s)->flags; \
    (d)->type = (s)->type; \
} while (0)

int pm_lan_encode(u8 *dst, u32 cap, const struct lan_message *m, u32 *used)
{
    struct lan_header h;
    if (!m || !used) return LAN_EINVAL;
    *used = 0;
    if (m->type < PLN1_TYPE_BEACON || m->type > PLN1_TERM_STATE) return LAN_EINVAL;
    if (m->payload_bytes > PLN1_PAYLOAD_MAX) return LAN_ERANGE;
    COPY_HEADER(&h, m);
    return pm_lan_encode_payload(dst, cap, &h, m->payload, m->payload_bytes, used);
}

int pm_lan_decode(const u8 *src, u32 bytes, struct lan_message *m)
{
    struct lan_header h;
    u32 i;
    if (!m) return LAN_EINVAL;
    kmemset(m, 0, sizeof(*m));
    if (pm_lan_decode_header(src, bytes, &h) != LAN_OK ||
        h.type > PLN1_TERM_STATE)
        return LAN_EINVAL;
    COPY_HEADER(m, &h);
    for (i = 0; i < h.payload_bytes; ++i) m->payload[i] = src[PLN1_HEADER_BYTES + i];
    return LAN_OK;
}
