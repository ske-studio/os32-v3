/* Cooperative early LAN service; all wire fields are encoded as bytes. */
#include "lan_boot.h"
#include "drivers/net/gem/gem.h"
#include "arch/ppc/kernel_boot.h"
#include "kprintf.h"
#if KBOOT_SH
#include "hostfs.h"
#endif

enum service_state { OFF = 0, RESETTING = 1, PREPARE = 2, PUBLISH = 3,
                     PHY_WAIT = 4, RUNNING = 5, QUARANTINED = 6 };
static struct {
    enum service_state state;
    struct gem g;
    const struct pm_gmac_boot *boot;
#if KBOOT_SH
    u8 tx[14 + PLN1_HEADER_BYTES + PLN1_FS_FRAME_MAX], rx[1514];
#else
    u8 tx[14 + PLN1_HEADER_BYTES + PLN1_PAYLOAD_MAX], rx[1514];
#endif
    struct lan_message response;
    u8 response_dst[6], enabling;
    u64 beacon_at, state_at, poll_at, phy_retry_at, request_drops;
    u8 out_turn, state_due;
} s;

static int same(const u8 *a, const u8 *b, u32 n)
{
    for (u32 i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static void copy(u8 *a, const u8 *b, u32 n)
{ for (u32 i = 0; i < n; ++i) a[i] = b[i]; }
static void off(int code)
{
    s.state = OFF;
    kprintf  (0x07, "LAN OFF code=%d\r\n", code);
}
static void reset(void)
{
    if (s.g.state != GEM_STATE_RESETTING) gem_enter_reset(&s.g);
    s.state = RESETTING;
}
static int send(const u8 *dst, const struct lan_message *m)
{
    u32 used;
    copy(s.tx, dst, 6); copy(s.tx + 6, s.boot->mac, 6);
    s.tx[12] = PLN1_ETHERTYPE >> 8; s.tx[13] = PLN1_ETHERTYPE & 255;
    if (pm_lan_encode(s.tx + 14, sizeof(s.tx) - 14, m, &used)) return GEM_EINVAL;
    return gem_tx(&s.g, s.tx, used + 14);
}
#if KBOOT_SH
int pm_lan_fs_running(void) { return s.state == RUNNING; }
int pm_lan_fs_send(const u8 *frame, u32 len)
{
    struct lan_header h = {.type = PLN1_FS_REQ, .payload_bytes = (u16)len};
    const u8 *dst;
    u32 used;
    int r;
    if (!pm_lan_fs_running()) return LAN_ENODEV;
    dst = pm_hfs_peer();
    if (!dst) dst = s.boot->peer_mac;
    copy(s.tx, dst, 6); copy(s.tx + 6, s.boot->mac, 6);
    s.tx[12] = PLN1_ETHERTYPE >> 8; s.tx[13] = PLN1_ETHERTYPE & 255;
    copy(h.boot_id, s.boot->boot_id, 16);
    r = pm_lan_encode_payload(s.tx + 14, sizeof(s.tx) - 14, &h, frame, len, &used);
    if (r) return r;
    r = gem_tx(&s.g, s.tx, used + 14);
    return r == GEM_AGAIN ? LAN_EBUSY : r;
}
#endif
static int beacon(u64 now)
{
    struct lan_message m = {0};
    const struct pm_lan_diag *d = pm_lan_diag_snapshot(s.boot, &s.g, s.state, s.request_drops);
    u32 flags;
    m.type = PLN1_TYPE_BEACON; m.payload_bytes = 68;
    copy(m.boot_id, s.boot->boot_id, 16);
    pm_lan_log_stats(&m.seq, &m.drop_records, &m.drop_bytes, &flags);
    m.flags = (u16)flags;
    if (!s.beacon_at) m.seq = 0;
    copy(m.payload, d->diag, 68);
    int r = send(s.boot->peer_mac, &m);
    s.beacon_at = now + s.g.io.tb_hz;
    return r;
}
static int logs(void)
{
    for (u32 i = 0; i < 4; ++i) {
        struct lan_message m = {0}, out;
        const u8 *data;
        u32 len, flags;
        int have_out = pm_term_tx_peek(&out);
        int have_log = pm_lan_log_peek(&data, &len, &m.seq, &m.drop_records, &m.drop_bytes, &flags) == LAN_OK;
        int use_out = have_out && (s.out_turn || !have_log);
        int r;
        if (!have_out && !have_log) break;
        if (use_out) {
            r = send(pm_term_peer(), &out);
            if (r == GEM_OK) pm_term_tx_complete();
        } else {
            m.type = PLN1_TYPE_LOG; m.payload_bytes = (u16)len; m.flags = (u16)flags;
            copy(m.boot_id, s.boot->boot_id, 16); copy(m.payload, data, len);
            r = send(s.boot->peer_mac, &m);
            pm_lan_log_complete(r);
        }
        s.out_turn = !use_out;
        if (r != GEM_OK) return r;
    }
    return GEM_OK;
}
static int term_state(u64 now)
{
    struct lan_message m;
    int r = GEM_OK;
    if (pm_term_state(&m) == 1) r = send(pm_term_peer(), &m);
    s.state_due = 0;
    s.state_at = now + s.g.io.tb_hz;
    return r;
}
static int receive(void)
{
    struct lan_message req, rsp;
    const struct pm_lan_diag *d;
    const u8 broadcast[6] = {255, 255, 255, 255, 255, 255};
    u32 n, nonzero = 0;
    int r = gem_rx(&s.g, s.rx, sizeof(s.rx), &n);
    if (r != GEM_OK) return r;
    if (n < 14 + PLN1_HEADER_BYTES || !same(s.rx, s.boot->mac, 6)) return GEM_OK;
    for (u32 i = 0; i < 6; ++i) nonzero |= s.rx[6 + i];
    if (!nonzero || (s.rx[6] & 1)) return GEM_OK;
    if (s.rx[12] != (PLN1_ETHERTYPE >> 8) || s.rx[13] != (PLN1_ETHERTYPE & 255)) return GEM_OK;
#if KBOOT_SH
    struct lan_header h;
    if (pm_lan_decode_header(s.rx + 14, n - 14, &h) ||
        !same(h.boot_id, s.boot->boot_id, 16)) return GEM_OK;
    if (h.type >= PLN1_FS_OPEN) {
        pm_hfs_receive(&h, s.rx + 6, s.rx + 14 + PLN1_HEADER_BYTES);
        return GEM_OK;
    }
#endif
    if (pm_lan_decode(s.rx + 14, n - 14, &req) ||
        !same(req.boot_id, s.boot->boot_id, 16)) return GEM_OK;
    if (req.type == PLN1_TERM_OPEN || req.type == PLN1_TERM_IN) {
        pm_term_receive(&req, s.rx + 6);
        if (req.type == PLN1_TERM_OPEN && !req.payload_bytes && !req.seq) s.state_due = 1;
        return GEM_OK;
    }
    if (req.type != PLN1_TYPE_REQ ||
        (!same(s.boot->peer_mac, broadcast, 6) && !same(s.rx + 6, s.boot->peer_mac, 6))) return GEM_OK;
    if (s.response.type) { ++s.request_drops; return GEM_OK; }
    d = pm_lan_diag_snapshot(s.boot, &s.g, s.state, s.request_drops);
    if (pm_lan_monitor(&req, s.rx + 6, d, &rsp) == 1) {
        s.response = rsp; copy(s.response_dst, s.rx + 6, 6);
    }
    return GEM_OK;
}
int pm_lan_start(const struct pm_gmac_boot *boot)
{
    const struct ppc_boot_devices *dev = ppc_boot_devices();
    struct pm_pci pci = {dev->pci_addr_va, dev->pci_data_va,
                         boot->bus_first, boot->bus_last, boot->bdf};
    u32 id, command;
    int r;
    if (boot->vendor_device != 0x0021106bu ||
        pm_pci_read32(&pci, boot->bdf, 0, &id) || id != boot->vendor_device ||
        pm_pci_read32(&pci, boot->bdf, 4, &command) || (command & 4)) {
        off(LAN_ENODEV); return LAN_ENODEV;
    }
    s.boot = boot;
#if KBOOT_SH
    pm_hfs_bind_peer(boot->peer_mac);
#endif
    r = pm_lan_gem_io(boot, &s.g.io);
    if (r != LAN_OK) { off(r); return r; }
    gem_enter_reset(&s.g); s.state = RESETTING;
    kprintf(0x07, "LAN STATE state=%u link=%u\r\n", (u32)s.state, 0u);
    return LAN_OK;
}
void pm_lan_poll(u64 now)
{
    enum service_state before = s.state;
    struct gem_dma dma;
    int r = GEM_AGAIN;
    if (s.state == OFF || s.state == QUARANTINED) return;
    if (s.poll_at && now < s.poll_at) return;
    s.poll_at = now + s.g.io.tb_hz / 1000u + (s.g.io.tb_hz % 1000u != 0);
    switch (s.state) {
    case RESETTING:
        r = gem_reset_step(&s.g, now);
        if (r == GEM_OK) {
            s.g.ring_prepare_step = s.g.ring_cursor = s.g.ring_publish_step = s.g.rx_head = 0;
            r = gem_mif_init(&s.g);
            if (r == GEM_OK) s.state = PREPARE;
        }
        break;
    case PREPARE:
        r = pm_gmac_dma_init(s.boot, &dma);
        if (r == GEM_OK) r = gem_ring_prepare(&s.g, &dma);
        if (r == GEM_OK) r = gem_mac_init(&s.g, s.boot->mac);
        if (r == GEM_OK) s.state = PUBLISH;
        break;
    case PUBLISH:
        r = gem_ring_publish(&s.g);
        if (r == GEM_OK) { s.enabling = 0; s.phy_retry_at = 0; s.state = PHY_WAIT; }
        break;
    case PHY_WAIT:
        if (!s.enabling && now >= s.phy_retry_at) {
            r = gem_phy_step(&s.g, now);
            if (r == GEM_OK) { s.g.step = 0; s.enabling = 1; }
            else if (r < 0 && r != GEM_EUNSUPPORTED) { s.phy_retry_at = now + s.g.io.tb_hz; r = GEM_AGAIN; }
        }
        if (s.enabling) {
            r = gem_mac_enable_step(&s.g, now);
            if (r == GEM_OK) { s.state = RUNNING; s.phy_retry_at = now + s.g.io.tb_hz; }
        }
        break;
    case RUNNING:
        r = gem_poll(&s.g, now, 8);
        if (s.g.state == GEM_STATE_RESETTING || r == GEM_EIO) { reset(); r = GEM_AGAIN; break; }
        if (r < 0) break;
        if (now >= s.phy_retry_at) {
            s.phy_retry_at = now + s.g.io.tb_hz;
            r = gem_phy_step(&s.g, now);
            if (r == GEM_EIO) { reset(); r = GEM_AGAIN; break; }
            if (r < 0) break;
        }
        if (!s.beacon_at) { r = beacon(now); if (r != GEM_OK) break; }
        r = receive();
        if (r == GEM_EIO) { reset(); r = GEM_AGAIN; break; }
        if (s.response.type) {
            r = send(s.response_dst, &s.response);
            if (r == GEM_OK) s.response.type = 0;
            else break;
        }
        r = logs();
        if (r != GEM_OK) break;
        if (s.state_due || now >= s.state_at) {
            r = term_state(now);
            if (r != GEM_OK) break;
        }
        if (s.beacon_at && now >= s.beacon_at) { r = beacon(now); if (r != GEM_OK) break; }
        r = GEM_AGAIN;
        break;
    default: break;
    }
    if (r < 0) {
        if (s.g.state == GEM_STATE_QUARANTINED) s.state = QUARANTINED;
        else { off(r); return; }
    }
    if (s.state != before)
        kprintf(0x07, "LAN STATE state=%u link=%u\r\n", (u32)s.state, (u32)(s.state == RUNNING));
}
