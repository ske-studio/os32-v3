#include "hostsvc_fixture.h"
#include "drivers/net/ne2000/ne2000.h"
#include "drivers/net/ne2000/ne2000_regs.h"
#include "net/link.h"
#include "os32/os32_kapi_shared.h"

static int state, send_rc, recv_rc, calls;
static const void *sent;
static u32 sent_len;
int ne2k_state(void) { calls = calls * 10 + 1; return state; }
int ne2k_send(const void *p, unsigned int n) { calls = calls * 10 + 2; sent = p; sent_len = n; return send_rc; }
int ne2k_recv(void *p, unsigned int cap, unsigned int *len) { calls = calls * 10 + 3; if (cap) *(u8 *)p = 42; *len = 1; return recv_rc; }
void ne2k_get_mac(u8 p[ETH_ALEN]) { calls = calls * 10 + 4; p[0] = 7; }
unsigned int ne2k_rx_ring_free_pages(void) { calls = calls * 10 + 5; return 20; }
unsigned int ne2k_rx_queue_free(void) { calls = calls * 10 + 6; return 2; }
static int run(void)
{
    const struct NetNicOps *ops = 0;
    u8 data[8]; u32 len = 0;
    hs_reset(0);
    if (net_lgy98_adapter.acquire(&ops) != OS32_ERR_NOSYS || ops || calls != 1) return 1;
    calls = 0; state = NE2K_STATE_RUNNING;
    if (net_lgy98_adapter.acquire(&ops) || !ops || calls != 1) return 2;
    send_rc = NE2K_ERR_BUSY;
    if (ops->tx(data, sizeof(data)) != OS32_ERR_AGAIN || sent != data || sent_len != sizeof(data)) return 3;
    send_rc = NE2K_ERR_STOPPED;
    if (ops->tx(data, sizeof(data)) != OS32_ERR_HOSTDOWN) return 4;
    send_rc = NE2K_ERR_IO;
    if (ops->tx(data, sizeof(data)) != OS32_ERR_IO) return 5;
    recv_rc = NE2K_ERR_NOSPACE;
    if (ops->rx(data, sizeof(data), &len) != OS32_ERR_NOSPC || len != 1 || data[0] != 42) return 6;
    ops->mac(data);
    if (data[0] != 7) return 7;
    ops->poll();
    if (ops->rx_credit_bytes() != 2 * LINK_MAXFRAME_PAGES * NE2K_PAGE_SIZE) return 8;
    if (calls != 12223456) return 9;
    net_lgy98_adapter.release();
    return 0;
}
void _start(void) { int rc = run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); __builtin_unreachable(); }
