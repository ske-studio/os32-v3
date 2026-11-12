/* FS nonce observation is independent of the foreground terminal session. */
#include "hostfs.h"
#include "net/host_mailbox.h"
#include "lib/kstring.h"

static u8 allowed[6], peer[6], nonce[16];
static struct HostAlive alive;

void pm_hfs_bind_peer(const u8 mac[6])
{
    kmemcpy(allowed, mac, 6);
}
const u8 *pm_hfs_peer(void) { return alive.valid ? peer : 0; }
void host_alive_snapshot(struct HostAlive *out) { *out = alive; }

static int same(const u8 *a, const u8 *b, u32 n)
{
    for (u32 i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static void observe(const struct lan_header *h, const u8 src[6])
{
    if (alive.valid && same(nonce, h->host_nonce, 16)) return;
    kmemcpy(nonce, h->host_nonce, 16);
    kmemcpy(peer, src, 6);
    ++alive.host_generation;
    ++alive.observation;
    alive.valid = 1;
}
void pm_hfs_receive(const struct lan_header *h, const u8 src[6], const u8 *payload)
{
    static const u8 broadcast[6] = {255, 255, 255, 255, 255, 255};
    u32 nonzero = 0, mac_nonzero = 0;
    if (h->type != PLN1_FS_OPEN && h->type != PLN1_FS_RSP) return;
    if (h->type == PLN1_FS_OPEN && h->payload_bytes) return;
    for (u32 i = 0; i < 6; ++i) mac_nonzero |= src[i];
    if (!mac_nonzero || (src[0] & 1) ||
        (!same(allowed, broadcast, 6) && !same(allowed, src, 6))) return;
    for (u32 i = 0; i < 16; ++i) nonzero |= h->host_nonce[i];
    if (!nonzero) return;
    if (h->type == PLN1_FS_OPEN) observe(h, src);
    if (h->type == PLN1_FS_RSP) {
        observe(h, src); /* Responses can be the first host observation. */
        (void)pm_hfs_rx_frame(payload, h->payload_bytes);
    }
}
