#include "types.h"
#include "platform/powermac/lan_boot.h"
#include "drivers/net/gem/gem.h"
#include "os32/os32_kapi_shared.h"
#if KBOOT_LINK
#include "rh_observe.h"
static int hello_marks;
void pm_rh_stage(unsigned stage) {
    if (stage == PM_RH_LINK_HELLO_SENT) hello_marks++;
}
#endif
static struct gem device;
static int running, calls, tx_budget, tx_rc, rx_rc, rx_calls;
static u8 expected[6] = {1,2,3,4,5,6};
static void finish(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) finish(__LINE__); } while (0)
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
int gem_rx(struct gem *g, void *p, u32 cap, u32 *n) {
    CHECK(g == &device && cap == 1514);
    rx_calls++; ((u8 *)p)[0] = 42; *n = 14; return rx_rc;
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
    CHECK(op->rx_credit_bytes() == 1514);
    CHECK(op->rx(frame, sizeof(frame), &n) == OS32_ERR_AGAIN && !rx_calls);
    op->poll(); CHECK(calls == 1 && tx_budget == 8 && rx_calls == 1);
    CHECK(op->rx_credit_bytes() == 0);
    op->poll(); CHECK(calls == 2 && rx_calls == 1);
    CHECK(op->rx(frame, 1, &n) == OS32_ERR_NOSPC && n == 14);
    CHECK(op->rx(frame, sizeof(frame), &n) == 0 && (n == 14 || n == 34) && frame[0] == 42);
    CHECK(op->rx_credit_bytes() == 1514);
    rx_rc = GEM_AGAIN; op->poll(); CHECK(rx_calls == 2);
    CHECK(op->rx(frame, sizeof(frame), &n) == OS32_ERR_AGAIN);
    running = 0; op->poll(); CHECK(calls == 3);
    CHECK(op->tx(frame, 14) == OS32_ERR_HOSTDOWN);
    CHECK(op->rx_credit_bytes() == 0);
    net_gem_adapter.release(); running = 1;
    op->poll(); CHECK(calls == 3);
    CHECK(net_gem_adapter.acquire(&op) == 0);
    net_gem_adapter.release(); finish(0);
}
