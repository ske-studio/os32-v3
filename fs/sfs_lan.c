#include "sfs_lan.h"
#include "os32_kapi_shared.h"

static int mac_equal(const u8 *a, const u8 *b)
{
    u32 i;
    for (i = 0; i < SFS_LAN_MAC_LEN; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static u32 lan_now(void *ctx)
{
    SfsLan *lan = ctx;
    return lan->clock->now(lan->clock_ctx);
}

static int lan_can_wait(void *ctx)
{
    SfsLan *lan = ctx;
    return lan->clock->can_wait(lan->clock_ctx);
}

static void lan_idle(void *ctx)
{
    SfsLan *lan = ctx;
    if (lan_can_wait(ctx)) lan->clock->idle(lan->clock_ctx);
}

/* One datagram at a time: SfsDec owns length/CRC validation. Never join the
 * tail of a short datagram to the next, or expose Ethernet padding to client. */
static int lan_receive(SfsLan *lan)
{
    u32 n;

    if (lan->rx_pos < lan->rx_end) return 1;
    lan->rx_pos = lan->rx_end = 0;
    if (lan->nic->recv(lan->rx, sizeof(lan->rx), &n) != 0) return 0;
    if (n > sizeof(lan->rx) || n < SFS_LAN_HDR_LEN + 3) return 0;
    if (lan->rx[12] != (SFS_LAN_ETHERTYPE >> 8) ||
        lan->rx[13] != (SFS_LAN_ETHERTYPE & 0xff) ||
        !mac_equal(lan->rx, lan->mac) ||
        mac_equal(lan->rx + SFS_LAN_MAC_LEN, lan->mac)) return 0;
    if ((lan->linked || lan->peer_fixed) &&
        !mac_equal(lan->rx + SFS_LAN_MAC_LEN, lan->peer))
        return 0;
    if (lan->rx[SFS_LAN_HDR_LEN] != SFS_ENQ ||
        lan->rx[SFS_LAN_HDR_LEN + 1] != SFS_MAGIC1 ||
        lan->rx[SFS_LAN_HDR_LEN + 2] != SFS_MAGIC2) return 0;
    lan->rx_pos = SFS_LAN_HDR_LEN;
    lan->rx_end = n;
    lan->prepared = 0;
    return 1;
}

/* Called by get only: put preserves every matching SFS datagram, including
 * one with a bad CRC. SfsDec rejects it here before any bytes reach client. */
static int lan_prepare(SfsLan *lan)
{
    SfsDec dec;
    SfsFrame frame;
    u32 i, n;
    int r;

    if (lan->prepared) return 1;
    sfs_dec_reset(&dec);
    dec.max_payload = SFS_LAN_MAX_PAYLOAD;
    for (i = SFS_LAN_HDR_LEN; i < lan->rx_end; i++) {
        r = sfs_dec_feed(&dec, lan->rx[i], &frame);
        if (r < 0) break;
        if (r != 1) continue;
        if (!lan->linked) {
            if (!lan->hello || frame.type != (SFS_T_HELLO | SFS_T_RESP) ||
                frame.sid == 0 || frame.seq != 0 || frame.len < 12 ||
                sfs_get32(frame.payload) != 0 ||
                sfs_get32(frame.payload + 8) != lan->nonce) break;
            for (n = 0; n < SFS_LAN_MAC_LEN; n++)
                lan->peer[n] = lan->rx[SFS_LAN_MAC_LEN + n];
            lan->linked = 1;
        }
        lan->rx_pos = SFS_LAN_HDR_LEN;
        lan->rx_end = i + 1;
        lan->prepared = 1;
        return 1;
    }
    lan->rx_pos = lan->rx_end = 0;
    return 0;
}

static int lan_get(void *ctx)
{
    SfsLan *lan = ctx;
    u32 i;

    for (i = 0; i < SFS_LAN_RX_BUDGET; i++) {
        if (lan_receive(lan) && lan_prepare(lan))
            return lan->rx[lan->rx_pos++];
    }
    return -1;
}

static int lan_put(void *ctx, const u8 *buf, u32 n)
{
    SfsLan *lan = ctx;
    u32 i, size, start;
    int r;

    if (!lan_can_wait(ctx)) return -1;
    if (n < SFS_HDR_LEN + SFS_CRC_LEN || n > SFS_BUFFER_FRAME) return -1;
    if (buf[3] == SFS_T_HELLO && n >= SFS_HDR_LEN + 8 + SFS_CRC_LEN) {
        lan->nonce = sfs_get32(buf + SFS_HDR_LEN + 4);
        lan->hello = 1;
        lan->linked = 0;
    } else if (!lan->linked) {
        return -1;
    }
    for (i = 0; i < SFS_LAN_MAC_LEN; i++) {
        lan->tx[i] = lan->linked ? lan->peer[i] : 0xff;
        lan->tx[SFS_LAN_MAC_LEN + i] = lan->mac[i];
    }
    lan->tx[12] = SFS_LAN_ETHERTYPE >> 8;
    lan->tx[13] = SFS_LAN_ETHERTYPE & 0xff;
    for (i = 0; i < n; i++) lan->tx[SFS_LAN_HDR_LEN + i] = buf[i];
    size = SFS_LAN_HDR_LEN + n;
    while (size < SFS_LAN_MIN_FRAME) lan->tx[size++] = 0;
    start = lan_now(ctx);
    for (;;) {
        r = lan->nic->send(lan->tx, size);
        if (r == 0) return 0;
        if (r != OS32_ERR_AGAIN && r != OS32_ERR_BUSY) return -1;
        /* Preserve an SFS response in the same one-frame get buffer. Once it
         * is occupied, stop draining rather than overwrite unread bytes. */
        for (i = 0; i < SFS_LAN_RX_BUDGET; i++)
            if (lan_receive(lan)) break;
        if ((u32)(lan_now(ctx) - start) >= sfs_ms_ticks(SFS_LAN_PUT_MS))
            return -1;
        if (!lan_can_wait(ctx)) return -1;
        lan_idle(ctx);
    }
}

const SfsIo sfs_lan_io = {lan_put, lan_get, lan_now, lan_idle, lan_can_wait};

void sfs_lan_init(SfsLan *lan, const SfsLanNic *nic,
                  const SfsIo *clock, void *clock_ctx)
{
    lan->nic = nic;
    lan->clock = clock;
    lan->clock_ctx = clock_ctx;
    lan->linked = lan->hello = lan->prepared = lan->peer_fixed = 0;
    lan->nonce = 0;
    lan->rx_pos = lan->rx_end = 0;
    nic->mac(lan->mac);
}

int sfs_lan_set_peer(SfsLan *lan, const char *mac)
{
    u8 peer[SFS_LAN_MAC_LEN];
    u32 i, j;
    if (!mac[0]) { lan->peer_fixed = 0; return 0; }
    for (i = 0; i < SFS_LAN_MAC_LEN; i++) {
        u32 value = 0;
        for (j = 0; j < 2; j++) {
            unsigned char c = (unsigned char)*mac++;
            if (c >= '0' && c <= '9') value = value * 16 + c - '0';
            else if (c >= 'a' && c <= 'f') value = value * 16 + c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value = value * 16 + c - 'A' + 10;
            else return -1;
        }
        peer[i] = (u8)value;
        if (i + 1 < SFS_LAN_MAC_LEN && *mac++ != ':') return -1;
    }
    if (*mac) return -1;
    for (i = 0; i < SFS_LAN_MAC_LEN; i++) lan->peer[i] = peer[i];
    lan->peer_fixed = 1;
    return 0;
}

void sfs_lan_reset(SfsLan *lan)
{
    u32 i, n;
    /* Never wait for quiet: continuous unrelated traffic cannot hold end. */
    for (i = 0; i < SFS_LAN_RX_BUDGET; i++)
        if (lan->nic->recv(lan->rx, sizeof(lan->rx), &n) != 0) break;
    lan->rx_pos = lan->rx_end = 0;
    lan->linked = lan->hello = lan->prepared = lan->peer_fixed = 0;
    lan->nonce = 0;
    for (i = 0; i < SFS_LAN_MAC_LEN; i++) lan->peer[i] = 0;
}
