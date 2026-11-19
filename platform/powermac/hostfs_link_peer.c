/* HostFS shares the link's fixed peer and the common HostAlive generation.
 * No PLN1 nonce observer or duplicate host_alive_snapshot lives in this image. */
#include "hostfs.h"
#include "fs/serialfs/serialfs.h"
#include "kstring.h"
#include "net/link.h"
#include "net/host_mailbox.h"

static u8 allowed[6];
static int fixed;
void pm_hfs_bind_peer(const u8 mac[6])
{
    fixed = 0;
    for (u32 i = 0; i < 6; ++i) {
        allowed[i] = mac[i];
        if (mac[i] != 255) fixed = 1;
    }
}
const u8 *pm_hfs_link_allowed_peer(void) { return fixed ? allowed : 0; }
const u8 *pm_hfs_peer(void)
{
    struct HostAlive alive;
    host_alive_snapshot(&alive);
    return alive.valid && link_hello_ok ? link_peer_mac : 0;
}

int pm_hfs_describe(void *ctx, struct SfsTransportInfo *out)
{
    const u8 *peer = pm_hfs_peer();
    (void)ctx;
    *out = (struct SfsTransportInfo){ .nic = "link", .peer_valid = peer != 0 };
    if (peer) kmemcpy(out->peer, peer, sizeof(out->peer));
    return 0;
}
