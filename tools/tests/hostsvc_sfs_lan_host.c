/* A10: real LAN transport/protocol and A3 queues; fake hardware/clock only. */
#include "fs/serialfs/sfs_lan.h"
#include "net/nic_rx.h"
#include "net/rx_work.h"
#include "os32_kapi_shared.h"
#define REQUIRE(e) do { if (!(e)) return __LINE__; } while (0)
static const u8 local[6] = {2,0,0,0,0,1}, peer[6] = {2,0,0,0,0,2};
static SfsLan lan;
static u8 frames[8][1514], out[1514], request[SFS_BUFFER_FRAME];
static u32 sizes[8], count, head, now, sends, private_calls;
static int busy, wait_ok = 1;
void *kmemcpy(void *d, const void *s, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)d)[i] = ((const u8 *)s)[i];
    return d;
}
unsigned int nic_test_irq_save(void) { return 0; }
void nic_test_irq_restore(unsigned int flags) { (void)flags; }
static int rx(void *buf, u32 cap, u32 *n)
{
    if (head == count) return OS32_ERR_AGAIN;
    *n = sizes[head];
    if (cap < *n) return OS32_ERR_NOSPC;
    kmemcpy(buf, frames[head++], *n);
    return 0;
}
static const struct NetNicOps ops = {0, rx, 0, 0, 0};
int net_nic_poll_once(void) { net_rx_demux(&ops, local, 0); return 0; }
int net_mac(u8 mac[6]) { kmemcpy(mac, local, 6); return 0; }
int net_tx(const void *buf, u32 n)
{
    sends++;
    if (busy) return OS32_ERR_AGAIN;
    kmemcpy(out, buf, n);
    return 0;
}
/* Poison old per-client callbacks: any private NIC access fails acceptance. */
static int old_send(const void *p, u32 n)
{ (void)p; (void)n; private_calls++; return OS32_ERR_IO; }
static int old_recv(void *p, u32 n, u32 *len)
{ (void)p; (void)n; (void)len; private_calls++; return OS32_ERR_AGAIN; }
static void old_mac(u8 *mac) { (void)mac; private_calls++; }
static const SfsLanNic old_nic = {old_send, old_recv, old_mac};
static u32 clock_now(void *ctx) { (void)ctx; return now; }
static int clock_wait(void *ctx) { (void)ctx; return wait_ok; }
static void clock_idle(void *ctx) { (void)ctx; now++; if (busy == 1) busy = 0; }
static const SfsIo clock_io = {0, 0, clock_now, clock_idle, clock_wait, 0};
static u8 *frame(int sfs, int corrupt, int stranger)
{
    u8 payload[SFS_HELLO_RSP_LEN] = {0};
    u8 *p = frames[count];
    for (u32 i = 0; i < 1514; i++) p[i] = 0;
    kmemcpy(p, local, 6); kmemcpy(p + 6, peer, 6);
    if (stranger) p[11]++;
    p[12] = SFS_LAN_ETHERTYPE >> 8; p[13] = SFS_LAN_ETHERTYPE & 255;
    if (sfs) {
        sfs_put32(payload + SFS_HELLO_RSP_NONCE, 123);
        u16 n = sfs_encode(p + 14, SFS_T_HELLO | SFS_T_RESP, 7, 0,
                           payload, sizeof(payload));
        if (corrupt) p[14 + n - 1] ^= 1;
    } else p[14] = 5; /* link EOF shares SFS ENQ byte, but not MARK. */
    sizes[count++] = 60;
    return p;
}
static int suite(void)
{
    sfs_lan_init(&lan, &old_nic, &clock_io, 0);
    REQUIRE(!private_calls && lan.mac[5] == 1);
    u8 payload[SFS_HELLO_REQ_LEN] = {0};
    sfs_put32(payload + SFS_HELLO_REQ_NONCE, 123);
    u16 n = sfs_encode(request, SFS_T_HELLO, 0, 0, payload, sizeof(payload));
    frame(0, 0, 0); frame(1, 0, 0);
    busy = 1;
    REQUIRE(sfs_lan_io.put(&lan, request, n) == 0);
    REQUIRE(sends == 2 && !private_calls && now == 1);
    REQUIRE(out[0] == 255 && out[6 + 5] == 1 && out[14] == SFS_ENQ);
    REQUIRE(sfs_lan_io.get(&lan) == SFS_ENQ && lan.linked);
    REQUIRE(lan.peer[5] == peer[5]);
    u32 len;
    REQUIRE(net_rx_take(NET_RX_LINK, out, sizeof(out), &len) == 0 && out[14] == 5);
    /* Reset partial local bytes plus queued SFS, preserving queued link. */
    frame(0, 0, 0); frame(1, 0, 0);
    net_nic_poll_once();
    sfs_lan_reset(&lan);
    REQUIRE(!lan.linked && !lan.hello && !lan.prepared && !lan.rx_end);
    REQUIRE(sfs_lan_io.get(&lan) == -1);
    REQUIRE(net_rx_take(NET_RX_LINK, out, sizeof(out), &len) == 0 && out[14] == 5);
    REQUIRE(!private_calls);
    /* CRC failures and other senders never establish a peer. */
    REQUIRE(sfs_lan_set_peer(&lan, "02:00:00:00:00:02") == 0);
    REQUIRE(sfs_lan_io.put(&lan, request, n) == 0);
    frame(1, 1, 0); frame(1, 0, 1);
    REQUIRE(sfs_lan_io.get(&lan) == -1 && !lan.linked);
    frame(1, 0, 0);
    REQUIRE(sfs_lan_io.get(&lan) == SFS_ENQ && lan.linked);
    /* A non-HELLO uses the selected peer, and IF-forbidden put never sends. */
    n = sfs_encode(request, SFS_T_BYE, 7, 1, 0, 0);
    REQUIRE(sfs_lan_io.put(&lan, request, n) == 0 && out[5] == peer[5]);
    wait_ok = 0;
    u32 before = sends;
    REQUIRE(sfs_lan_io.put(&lan, request, n) == -1 && sends == before);
    REQUIRE(!private_calls);
    wait_ok = 1;
    /* Absolute deadline wins after ensure has consumed 90 ticks. */
    now = 90; busy = 2; before = sends;
    REQUIRE(sfs_lan_io.put_until(&lan, request, n, 100) == OS32_ERR_TIMEOUT);
    REQUIRE(now == 100 && sends - before == 10);
    before = sends; busy = 0;
    REQUIRE(sfs_lan_io.put_until(&lan, request, n, 100) == OS32_ERR_TIMEOUT);
    REQUIRE(sends == before);
    /* The transport's own 1000ms + one-tick bound still wins for a distant deadline. */
    now = 0xfffffff0u; busy = 2; before = sends;
    REQUIRE(sfs_lan_io.put_until(&lan, request, n, now + 200) == OS32_ERR_TIMEOUT);
    REQUIRE(now == 85 && sends - before == 101);
    now = 0xfffffff0u; before = sends;
    REQUIRE(sfs_lan_io.put_until(&lan, request, n, now + 10) == OS32_ERR_TIMEOUT);
    REQUIRE(now == 0xfffffffau && sends - before == 10);
    busy = 0;
    REQUIRE(sfs_lan_io.put_until(&lan, request, n, now + 10) == 0);
    return 0;
}
void _start(void)
{
    int rc = suite();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}

int platform_clock_read_us(u32 *lo, u32 *hi) { *lo = *hi = 0; return 0; }
u32 tick_budget_cnt(u32 us) { return us; }
int tick_budget_take(u32 share, u32 tail) { (void)share; (void)tail; return 1; }
void net_rx_schedule(void) {}
int net_rx_defer_reset(u16 protocol) { (void)protocol; return 0; }

int tick_budget_peek(u32 share, u32 tail) { (void)share; (void)tail; return 1; }
