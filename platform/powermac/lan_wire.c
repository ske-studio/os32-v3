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

int pm_lan_encode(u8 *dst, u32 cap, const struct lan_message *m, u32 *used)
{
    u32 i;
    if (!dst || !m || !used)
        return LAN_EINVAL;
    *used = 0;
    if (m->type < PLN1_TYPE_BEACON || m->type > PLN1_TYPE_RSP)
        return LAN_EINVAL;
    if (m->payload_bytes > PLN1_PAYLOAD_MAX)
        return LAN_ERANGE;
    if (cap < PLN1_HEADER_BYTES + (u32)m->payload_bytes)
        return LAN_ERANGE;
    if (m->type <= PLN1_TYPE_LOG)
        for (i = 0; i < 16; ++i)
            if (m->host_nonce[i]) return LAN_EINVAL;
    put_be(dst + PLN1_OFF_MAGIC, PLN1_MAGIC, 4);
    dst[PLN1_OFF_VERSION] = PLN1_VERSION;
    dst[PLN1_OFF_TYPE] = m->type;
    put_be(dst + PLN1_OFF_HEADER_BYTES, PLN1_HEADER_BYTES, 2);
    for (i = 0; i < 16; ++i) dst[PLN1_OFF_BOOT_ID + i] = m->boot_id[i];
    put_be(dst + PLN1_OFF_SEQ, m->seq, 8);
    put_be(dst + PLN1_OFF_DROP_RECORDS, m->drop_records, 8);
    put_be(dst + PLN1_OFF_DROP_BYTES, m->drop_bytes, 8);
    put_be(dst + PLN1_OFF_REQUEST_ID, m->request_id, 4);
    put_be(dst + PLN1_OFF_OPCODE, m->opcode, 2);
    put_be(dst + PLN1_OFF_STATUS, m->status, 2);
    put_be(dst + PLN1_OFF_PAYLOAD_BYTES, m->payload_bytes, 2);
    put_be(dst + PLN1_OFF_FLAGS, m->flags, 2);
    put_be(dst + PLN1_OFF_RESERVED, 0, 4);
    for (i = 0; i < 16; ++i) dst[PLN1_OFF_HOST_NONCE + i] = m->host_nonce[i];
    for (i = 0; i < m->payload_bytes; ++i) dst[PLN1_HEADER_BYTES + i] = m->payload[i];
    *used = PLN1_HEADER_BYTES + m->payload_bytes;
    return LAN_OK;
}

int pm_lan_decode(const u8 *src, u32 bytes, struct lan_message *m)
{
    u32 i, len;
    if (!m) return LAN_EINVAL;
    memset(m, 0, sizeof(*m));
    if (!src || bytes < PLN1_HEADER_BYTES) return LAN_EINVAL;
    len = (u32)get_be(src + PLN1_OFF_PAYLOAD_BYTES, 2);
    if (get_be(src + PLN1_OFF_MAGIC, 4) != PLN1_MAGIC ||
        src[PLN1_OFF_VERSION] != PLN1_VERSION ||
        get_be(src + PLN1_OFF_HEADER_BYTES, 2) != PLN1_HEADER_BYTES ||
        src[PLN1_OFF_TYPE] < PLN1_TYPE_BEACON || src[PLN1_OFF_TYPE] > PLN1_TYPE_RSP ||
        get_be(src + PLN1_OFF_RESERVED, 4) != 0 ||
        len > PLN1_PAYLOAD_MAX || len > bytes - PLN1_HEADER_BYTES)
        return LAN_EINVAL;
    m->type = src[PLN1_OFF_TYPE];
    for (i = 0; i < 16; ++i) m->boot_id[i] = src[PLN1_OFF_BOOT_ID + i];
    m->seq = get_be(src + PLN1_OFF_SEQ, 8);
    m->drop_records = get_be(src + PLN1_OFF_DROP_RECORDS, 8);
    m->drop_bytes = get_be(src + PLN1_OFF_DROP_BYTES, 8);
    m->request_id = (u32)get_be(src + PLN1_OFF_REQUEST_ID, 4);
    m->opcode = (u16)get_be(src + PLN1_OFF_OPCODE, 2);
    m->status = (u16)get_be(src + PLN1_OFF_STATUS, 2);
    m->payload_bytes = (u16)len;
    m->flags = (u16)get_be(src + PLN1_OFF_FLAGS, 2);
    for (i = 0; i < 16; ++i) m->host_nonce[i] = src[PLN1_OFF_HOST_NONCE + i];
    for (i = 0; i < len; ++i) m->payload[i] = src[PLN1_HEADER_BYTES + i];
    return LAN_OK;
}
