/* A3 real queues/demux, fake NIC boundary. Limits: design §2 and 82557 RFD ring. */
#include "hostsvc_fixture.h"
#include "net/nic_rx.h"
#include "net/rx_work.h"
#include "net/link.h"
#include "drivers/net/e82557/e82557_regs.h"
#include "os32_kapi_shared.h"

#define REQUIRE(e) do { if (!(e)) return __LINE__; } while (0)
#define FRAME_BYTES (ETH_FRAME_MAX - ETH_FCS_LEN)
#define EXPECT_SLOTS 4u /* NET_HOSTSVC_DESIGN §2, per protocol */
extern u32 net_rx_sfs_dropped, net_rx_link_dropped;
static const u8 local[6] = {2,3,4,5,6,7}, peer[6] = {8,9,10,11,12,13};
static u8 input[E82557_RFD_COUNT][FRAME_BYTES], output[FRAME_BYTES];
static u32 lengths[E82557_RFD_COUNT], head, count, calls, polls;
static int bound, ticking, bad_if;
static const u8 *fixed_peer;

void *kmemcpy(void *dst, const void *src, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)dst)[i] = ((const u8 *)src)[i];
    return dst;
}
static int equal(const void *a, const void *b, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (((const u8 *)a)[i] != ((const u8 *)b)[i]) return 0;
    return 1;
}
unsigned int nic_test_irq_save(void)
{
    unsigned int flags = hs_if();
    hs_set_if(0);
    return flags;
}
void nic_test_irq_restore(unsigned int flags) { hs_set_if(flags); }
static int rx(void *out, u32 cap, u32 *len)
{
    int rc;
    calls++;
    if (!hs_if()) bad_if++;
    if (hs_failure_take("nic.rx", &rc)) return rc;
    if (!count) return OS32_ERR_AGAIN;
    *len = lengths[head];
    if (*len > cap) return OS32_ERR_NOSPC;
    kmemcpy(out, input[head], *len);
    head = (head + 1) % E82557_RFD_COUNT;
    count--;
    return 0;
}
static const struct NetNicOps ops = {0, rx, 0, 0, 0};
/* Same A2 boot state and tick contract: unbound/ticking does no poll. */
int net_nic_poll_once(void)
{
    u32 before = count;
    if (bound && !ticking && hs_if()) {
        polls++;
        net_rx_demux(&ops, local, fixed_peer);
    }
    return (int)(before - count);
}
static u8 *frame(u8 op, u32 len, u8 tag)
{
    if (count == E82557_RFD_COUNT || len > FRAME_BYTES) __builtin_trap();
    u32 tail = (head + count) % E82557_RFD_COUNT;
    u8 *p = input[tail];
    for (u32 i = 0; i < FRAME_BYTES; i++) p[i] = 0;
    kmemcpy(p, local, ETH_ALEN);
    kmemcpy(p + ETH_ALEN, peer, ETH_ALEN);
    p[ETH_TYPE_OFF] = OS32_ETHERTYPE >> 8;
    p[ETH_TYPE_OFF + 1] = OS32_ETHERTYPE & 255;
    p[ETH_HLEN] = op;
    if (op == 0x05) { p[ETH_HLEN+1] = 0x53; p[ETH_HLEN+2] = 0x46; }
    p[FRAME_BYTES-1] = tag;
    lengths[tail] = len;
    count++;
    return p;
}
static void broadcast(u8 *p)
{
    for (u32 i = 0; i < ETH_ALEN; i++) p[i] = 255;
}
static void reset(void)
{
    net_rx_reset(NET_RX_SFS);
    net_rx_reset(NET_RX_LINK);
    head = count = calls = polls = 0;
}
static int suite(void)
{
    u32 n = 0;
    hs_reset(0xffffff00u);
    REQUIRE(net_rx_take(NET_RX_SFS, output, sizeof(output), &n) == OS32_ERR_AGAIN);
    REQUIRE(polls == 0 && hs_if());
    bound = 1;
    for (u32 round = 0; round < 12; round++) {
        u8 *sfs = frame(0x05, FRAME_BYTES, (u8)round);
        u8 *eof = frame(LINK_OP_EOF, FRAME_BYTES, (u8)(round+1));
        eof[ETH_HLEN+1] = 0; /* op=5 alone is link EOF, not SFS MARK. */
        u32 prior = polls;
        REQUIRE(net_rx_take(NET_RX_SFS, output, sizeof(output), &n) == 0);
        REQUIRE(n == FRAME_BYTES && output[n-1] == round && equal(output, sfs, n));
        REQUIRE(polls == prior+1 && hs_if());
        REQUIRE(net_rx_take(NET_RX_LINK, output, sizeof(output), &n) == 0);
        REQUIRE(output[n-1] == round+1 && polls == prior+1 && equal(output, eof, n));
    }
    /* Exact production queue capacity, both directions; full drops newest. */
    for (u32 proto = NET_RX_SFS; proto <= NET_RX_LINK; proto++) {
        reset();
        u8 op = proto == NET_RX_SFS ? 0x05 : LINK_OP_DATA;
        u16 other = proto == NET_RX_SFS ? NET_RX_LINK : NET_RX_SFS;
        u32 before = proto == NET_RX_SFS ? net_rx_sfs_dropped : net_rx_link_dropped;
        for (u32 i = 0; i < EXPECT_SLOTS; i++) frame(op, FRAME_BYTES, (u8)i);
        REQUIRE(net_nic_poll_once() == EXPECT_SLOTS);
        frame(op, FRAME_BYTES, 99);
        frame(other == NET_RX_SFS ? 0x05 : LINK_OP_DATA, FRAME_BYTES, 77);
        REQUIRE(net_nic_poll_once() == 2);
        REQUIRE((proto == NET_RX_SFS ? net_rx_sfs_dropped : net_rx_link_dropped) == before+1);
        REQUIRE(net_rx_take(other, output, sizeof(output), &n) == 0 && output[n-1] == 77);
        for (u32 i = 0; i < EXPECT_SLOTS; i++) {
            REQUIRE(net_rx_take(proto, output, sizeof(output)-1, &n) == OS32_ERR_NOSPC);
            REQUIRE(n == FRAME_BYTES);
            REQUIRE(net_rx_take(proto, output, sizeof(output), &n) == 0 && output[n-1] == i);
        }
        REQUIRE(net_rx_take(proto, output, sizeof(output), &n) == OS32_ERR_AGAIN);
    }
    reset();
    for (u32 i = 0; i < NET_RX_SCAN_MAX; i++) frame(LINK_OP_DATA, ETH_ZLEN, 0);
    REQUIRE(net_nic_poll_once() == NET_RX_SCAN_MAX);
    REQUIRE(calls == NET_RX_SCAN_MAX && count == 0);
    reset();
    /* Invalid Ethernet headers and fixed sender fail before dispatch. */
    frame(0x05, ETH_HLEN-1, 0);
    frame(0x05, ETH_ZLEN, 0)[ETH_TYPE_OFF] = 0;
    frame(0x05, ETH_ZLEN, 0)[0] ^= 1;
    frame(0x05, ETH_ZLEN, 0)[ETH_ALEN] ^= 1;
    fixed_peer = peer;
    net_nic_poll_once();
    REQUIRE(net_rx_take(NET_RX_SFS, output, sizeof(output), &n) == OS32_ERR_AGAIN);
    REQUIRE(net_rx_take(NET_RX_LINK, output, sizeof(output), &n) == OS32_ERR_AGAIN);
    /* NULL peer accepts different senders, without learning one in demux. */
    fixed_peer = 0;
    frame(0x05, ETH_ZLEN, 0)[ETH_ALEN] ^= 1;
    frame(LINK_OP_DATA, ETH_ZLEN, 0)[ETH_ALEN] ^= 2;
    REQUIRE(net_rx_take(NET_RX_SFS, output, sizeof(output), &n) == 0);
    REQUIRE(net_rx_take(NET_RX_LINK, output, sizeof(output), &n) == 0);
    broadcast(frame(0x05, ETH_ZLEN, 0));
    broadcast(frame(LINK_OP_DATA, ETH_ZLEN, 0));
    broadcast(frame(LINK_OP_HELLO, ETH_ZLEN, 0));
    REQUIRE(net_rx_take(NET_RX_LINK, output, sizeof(output), &n) == 0);
    REQUIRE(output[ETH_HLEN] == LINK_OP_HELLO);
    REQUIRE(net_rx_take(NET_RX_SFS, output, sizeof(output), &n) == OS32_ERR_AGAIN);
    /* Zero/two-byte payload cannot match the full MARK. */
    frame(0x05, ETH_HLEN+2, 0);
    frame(0, ETH_HLEN, 0);
    REQUIRE(net_rx_take(NET_RX_LINK, output, sizeof(output), &n) == 0 && n == ETH_HLEN+2);
    REQUIRE(net_rx_take(NET_RX_LINK, output, sizeof(output), &n) == 0 && n == ETH_HLEN);
    frame(0x05, ETH_ZLEN, 0);
    frame(LINK_OP_DATA, ETH_ZLEN, 0);
    net_nic_poll_once();
    net_rx_reset(NET_RX_SFS);
    REQUIRE(net_rx_take(NET_RX_LINK, output, sizeof(output), &n) == 0);
    REQUIRE(net_rx_take(NET_RX_SFS, output, sizeof(output), &n) == OS32_ERR_AGAIN);
    frame(0x05, ETH_ZLEN, 0);
    hs_fail("nic.rx", OS32_ERR_IO);
    REQUIRE(net_rx_take(NET_RX_SFS, output, sizeof(output), &n) == OS32_ERR_AGAIN && count == 1);
    REQUIRE(net_rx_take(NET_RX_SFS, output, sizeof(output), &n) == 0);
    hs_set_if(0);
    REQUIRE(net_rx_take(NET_RX_LINK, output, sizeof(output), &n) == OS32_ERR_AGAIN && !hs_if());
    net_rx_reset(NET_RX_LINK);
    REQUIRE(!hs_if());
    hs_set_if(1);
    ticking = 1;
    frame(LINK_OP_DATA, ETH_ZLEN, 0);
    u32 prior = polls;
    REQUIRE(net_rx_take(NET_RX_LINK, output, sizeof(output), &n) == OS32_ERR_AGAIN);
    REQUIRE(polls == prior && count == 1);
    ticking = 0;
    REQUIRE(net_rx_take(99, output, sizeof(output), &n) == OS32_ERR_INVAL);
    REQUIRE(!bad_if && hs_if());
    return 0;
}
void _start(void)
{
    int rc = suite();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? rc % 250 + 1 : 0));
    __builtin_unreachable();
}

int platform_clock_read_us(u32 *lo, u32 *hi) { *lo = *hi = 0; return 0; }
u32 tick_budget_cnt(u32 us) { return us; }
int tick_budget_take(u32 share, u32 tail) { (void)share; (void)tail; return 1; }
void net_rx_schedule(void) {}
int net_rx_defer_reset(u16 protocol) { (void)protocol; return 0; }
