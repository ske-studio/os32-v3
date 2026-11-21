/* The e82557 demux path and the 2026-11-21 GEM decision require direct RX
 * during TX AGAIN. gem_rint publishes refills in groups of four, and the
 * OS32 poll budget includes discarded descriptors. */
#include "types.h"
#include "platform/powermac/lan_boot.h"
#include "drivers/net/gem/gem.h"
#include "os32/os32_kapi_shared.h"
#include "net/link.h"
#if KBOOT_LINK
#include "rh_observe.h"
static int hello_marks;
void pm_rh_stage(unsigned stage) {
    if (stage == PM_RH_LINK_HELLO_SENT) hello_marks++;
}
#endif
static struct gem device;
static int running, calls, tx_budget, tx_rc;
static u8 expected[6] = {1,2,3,4,5,6};
static void finish(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) finish(__LINE__); } while (0)
static u8 pool[GEM_POOL_SIZE];
static u32 completion, kick = GEM_RX_PUBLISHED;
static u32 read_reg(void *ctx, u32 reg) {
    CHECK(ctx == &device && reg == GEM_RX_COMPLETION);
    return completion;
}
static void write_reg(void *ctx, u32 reg, u32 value) {
    CHECK(ctx == &device && reg == GEM_RX_KICK);
    kick = value;
}
static int sync_dma(void *addr, u32 size, enum dma_sync direction) {
    (void)addr; (void)size; (void)direction;
    return 0;
}
struct gem *pm_lan_running_gem(u8 mac[6]) {
    if (!running) return 0;
    if (mac) for (u32 i = 0; i < 6; i++) mac[i] = expected[i];
    return &device;
}
static unsigned long long clock_now(void *ctx) { CHECK(ctx == &device); return 123; }
int gem_tx(struct gem *g, const void *p, u32 n) {
    CHECK(g == &device && (n == 14 || n == 34) && ((const u8 *)p)[0] == 42);
    return tx_rc;
}
int gem_poll(struct gem *g, unsigned long long now, u32 budget) {
    CHECK(g == &device && now == 123);
    calls++; tx_budget = budget; return 0;
}
#include GEM_ADAPTER_SOURCE
void _start(void) {
    const struct NetNicOps *op = 0;
    u8 frame[1514] = {42}, addr[6]; u32 n = 0;
    CHECK(net_gem_adapter.acquire(0) == OS32_ERR_INVAL);
    CHECK(net_gem_adapter.acquire(&op) == OS32_ERR_NOSYS && !op);
    running = 1; device.io.tb = clock_now; device.io.ctx = &device;
    CHECK(net_gem_adapter.acquire(&op) == 0 && op);
    CHECK(net_gem_adapter.acquire(&op) == OS32_ERR_BUSY);
    op->mac(addr); for (u32 i = 0; i < 6; i++) CHECK(addr[i] == expected[i]);
    CHECK(op->tx(frame, 14) == 0);
#if KBOOT_LINK
    frame[14] = 1;
    CHECK(op->tx(frame, 34) == 0 && hello_marks == 1);
    tx_rc = GEM_AGAIN;
    CHECK(op->tx(frame, 34) == OS32_ERR_AGAIN && hello_marks == 1);
    tx_rc = GEM_OK; frame[14] = 2;
    CHECK(op->tx(frame, 34) == 0 && hello_marks == 1);
#endif
    tx_rc = GEM_AGAIN; CHECK(op->tx(frame, 14) == OS32_ERR_AGAIN);
    tx_rc = GEM_EIO; CHECK(op->tx(frame, 14) == OS32_ERR_IO);
    device.io.read32 = read_reg; device.io.write32 = write_reg;
    device.io.sync = sync_dma; device.dma.cpu = pool;
    device.state = GEM_STATE_READY; device.ring_publish_step = 3;
    CHECK(op->rx_credit_bytes() == 28 * 1514);
    tx_rc = GEM_AGAIN;
    for (u32 round = 0; round < 5; round++) {
        u32 first = device.rx_head;
        for (u32 i = 0; i < 9; i++) {
            u32 slot = (first + i) % GEM_RING_COUNT;
            u8 *flags = pool + GEM_POOL_RX_RING + slot * GEM_DESC_SIZE;
            for (u32 j = 0; j < 8; j++) flags[j] = 0;
            flags[2] = 14;
            if (!i) flags[7] = 0x40; /* bad CRC consumes one inspection */
            pool[GEM_POOL_RX_BUF + slot * GEM_BUF_SIZE + 2] = (u8)slot;
        }
        completion = (first + 9) % GEM_RING_COUNT;
        op->poll();
        CHECK(device.rx_head == first && tx_budget == 8);
        CHECK(op->rx_credit_bytes() == (28 - first % 4 - 9) * 1514);
        for (u32 i = 1; i < 8; i++) {
            frame[0] = 42;
            CHECK(op->tx(frame, 14) == OS32_ERR_AGAIN);
            CHECK(op->rx(frame, sizeof(frame), &n) == 0 && n == 14);
            CHECK(frame[0] == (first + i) % GEM_RING_COUNT);
            u32 head = (first + i + 1) % GEM_RING_COUNT;
            CHECK(device.rx_head == head);
            CHECK(kick == ((head - head % 4 + 28) % GEM_RING_COUNT));
            CHECK(op->rx_credit_bytes() == ((kick - completion) & 31) * 1514);
        }
        CHECK(op->rx(frame, sizeof(frame), &n) == OS32_ERR_AGAIN);
        op->poll();
        CHECK(op->rx(frame, sizeof(frame), &n) == 0);
        CHECK(op->rx(frame, sizeof(frame), &n) == OS32_ERR_AGAIN);
    }
    running = 0; op->poll();
    int old_calls = calls;
    CHECK(op->tx(frame, 14) == OS32_ERR_HOSTDOWN);
    CHECK(op->rx_credit_bytes() == 0);
    net_gem_adapter.release(); running = 1;
    op->poll(); CHECK(calls == old_calls);
    CHECK(net_gem_adapter.acquire(&op) == 0);
    CHECK(op->rx(frame, sizeof(frame), &n) == OS32_ERR_AGAIN);
    CHECK(op->rx_credit_bytes() == (28 - device.rx_head % 4) * 1514);
    net_gem_adapter.release(); finish(0);
}
