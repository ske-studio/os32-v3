/* A8 real link.c, ILP32 without libc. A9 RX remains legacy. */
#define LINK_HOST_TEST
#include "types.h"
volatile u32 tick_count;
static unsigned int test_if;
static int tx_result, tx_calls;
static u32 credit_bytes, tx_length;
static u8 tx_frame[1520];
unsigned int link_test_irq_save(void) { unsigned int f = test_if; test_if = 0; return f; }
void link_test_irq_restore(unsigned int f) { test_if = f; }
void link_test_idle(void) { }
#include "net/link.c"
void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = dst; const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
void *kmemset(void *dst, int v, u32 n)
{
    u8 *d = dst; while (n--) *d++ = v; return dst;
}
int net_tx(const void *frame, u32 n)
{
    tx_calls++;
    tx_length = n;
    kmemcpy(tx_frame, frame, n);
    return tx_result;
}
u32 net_rx_credit_bytes(void) { return credit_bytes; }
/* No LGY TX/credit/busy stubs: any remaining core call fails to link. */
static int nic_bound = 1;
int net_mac(u8 out[6])
{
    if (!nic_bound) return OS32_ERR_HOSTDOWN;
    kmemcpy(out, link_my_mac, ETH_ALEN);
    return 0;
}
int ne2k_recv(void *out, unsigned int cap, unsigned int *len)
{
    (void)out; (void)cap; (void)len;
    return NE2K_ERR_AGAIN; /* A9 owns shared-RX migration. */
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int verify(void)
{
    const u8 mac[6] = {2, 3, 4, 5, 6, 7};
    link_init(mac); /* No tick_register stub: init must not register a tick. */
    CHECK(link_ready && link_hs_due == 1);
    tx_result = OS32_ERR_AGAIN;
    link_tick();
    CHECK(tx_calls == 1 && link_hs_due == 1 && link_hs_tick == 0);
    tick_count = 9;
    tx_result = 0;
    link_tick();
    CHECK(tx_calls == 2 && link_hs_due == 0 && link_hs_tick == 9);
    CHECK(tx_length == LINK_ETH_HDR_LEN + LINK_HDR_LEN);
    CHECK(mac_eq(tx_frame + ETH_ALEN, mac));
    CHECK(tx_frame[ETH_TYPE_OFF] == (LINK_ETHERTYPE >> 8));
    CHECK(tx_frame[ETH_TYPE_OFF + 1] == (LINK_ETHERTYPE & 255));
    CHECK(tx_frame[LINK_ETH_HDR_LEN + LINK_OFF_OP] == LINK_OP_HELLO);
    tx_result = OS32_ERR_HOSTDOWN;
    CHECK(!link_send(LINK_OP_STATUS, 0, 0, 0, 1, 0, 0));
    credit_bytes = 100 * LINK_PAGE_BYTES + LINK_PAGE_BYTES - 1;
    CHECK(link_credit_pages() == LINK_STREAM_BUF / LINK_PAGE_BYTES - LINK_CREDIT_MARGIN);
    credit_bytes = (LINK_CREDIT_MARGIN + 3) * LINK_PAGE_BYTES + LINK_PAGE_BYTES - 1;
    CHECK(link_credit_pages() == 3);
    credit_bytes = LINK_CREDIT_MARGIN * LINK_PAGE_BYTES;
    CHECK(link_credit_pages() == 0);
    credit_bytes = 0;
    CHECK(link_credit_pages() == 0);

    link_hs_due = 0; link_state = LINK_S_UP; link_epoch = 3;
    link_hello_ok = 1;
    tick_count = U32_MAX - 49;
    nic_bound = 0;
    CHECK(link_host_open("TIME", 4, 1) == OS32_ERR_HOSTDOWN);
    nic_bound = 1;
    CHECK(link_host_open("TIME", 4, 1) == 0);
    CHECK(link_host_open("TIME", 4, 1) == 1);
    /* Exhausted legacy RTO would resync unless expiry runs before timers. */
    link_h[0].req_due = 0;
    link_h[0].last_tx_tick = tick_count;
    link_h[0].retries = LINK_TRIES;
    link_h[1].req_acked = 1; link_h[1].req_due = 0;
    tick_count += 99;
    link_host_progress(1); /* Explicit hook; A9 receive wiring is outstanding. */
    link_h[0].last_tx_tick = tick_count; /* Avoid earlier unrelated RTO. */
    link_tick();
    CHECK(link_host_status(0, 0, 0, 1) == OS32_ERR_AGAIN);
    tick_count++;
    link_h[0].last_tx_tick = tick_count - LINK_RTO_TICKS;
    link_tick();
    CHECK(link_host_status(0, 0, 0, 1) == OS32_ERR_HOSTDOWN);
    CHECK(link_host_status(1, 0, 0, 1) == OS32_ERR_AGAIN);
    CHECK(link_epoch == 3 && link_state == LINK_S_UP && link_resyncs == 0);
    CHECK(link_h[0].req_due == 0 && link_h[0].want_status == 0);
    CHECK(test_if == 0);
    CHECK(link_host_close(0, 1) == 0);
    CHECK(link_host_open("TIME", 4, 1) == 0);
    CHECK(link_host_status(0, 0, 0, 1) == OS32_ERR_AGAIN);
    return 0;
}
void _start(void)
{
    int rc = verify();
    if (rc) rc = 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
