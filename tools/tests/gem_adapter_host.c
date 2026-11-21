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
/* OpenBSD gemvar.h selects 128 RX slots. Exercise the high bits of each
 * diagnostic index independently (PowerMac GEM observation layout). */
#define KBOOT_GEM_RX_DIAG
volatile u32 gem_rx_last_kick;
static u32 read_completion(void *ctx, u32 reg) {
    (void)ctx;
    return reg == GEM_RX_COMPLETION ? 95 : 0;
}
static struct gem device;
static int running, calls, tx_budget, tx_rc, rx_rc, rx_calls;
static u32 good, bad, inspected, delivered;
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
/* 本線 PM 決定 2026-11-15 B の条件 1, 2, 3: shared descriptor budget. */
int gem_rx_some(struct gem *g, void *p, u32 cap, u32 *n, u32 *budget) {
    CHECK(g == &device && cap == 1514);
    CHECK(*budget == 8u - inspected);
    rx_calls++;
    while (*budget) {
        (*budget)--; inspected++;
        CHECK(*budget <= 8);
        if (bad) { bad--; continue; }
        if (!good) return GEM_AGAIN; /* OWN is inspected too. */
        good--; delivered++;
        ((u8 *)p)[0] = 42; ((u8 *)p)[1] = (u8)delivered;
        ((u8 *)p)[12] = 0x86; ((u8 *)p)[13] = 0xdd;
        *n = 14; return rx_rc;
    }
    return GEM_AGAIN;
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
    CHECK(op->rx_credit_bytes() / LINK_PAGE_BYTES > LINK_CREDIT_MARGIN);
    CHECK(op->rx_credit_bytes() == 4 * 1514);
    CHECK(op->rx(frame, sizeof(frame), &n) == OS32_ERR_AGAIN && !rx_calls);
    /* 本線 PM 決定 2026-11-15 B の条件 1, 3: fill only free slots, FIFO. */
    device.io.read32 = read_completion; device.rx_head = 127; gem_rx_last_kick = 124;
    good = 6; inspected = 0;
    op->poll(); CHECK(calls == 1 && tx_budget == 8 && rx_calls == 4);
    CHECK(gem_rx_diag == (1ull | (4ull << 16) | (127ull << 19) |
                          (95ull << 26) | (124ull << 33)));
    CHECK(inspected == 4 && good == 2 && op->rx_credit_bytes() == 0);
    op->poll(); CHECK(calls == 2 && rx_calls == 4);
    CHECK(op->rx(frame, 1, &n) == OS32_ERR_NOSPC && n == 14);
    CHECK(op->rx_credit_bytes() == 0);
    for (int i = 1; i <= 2; i++) {
        CHECK(op->rx(frame, sizeof(frame), &n) == 0 && frame[1] == i);
    }
    CHECK(op->rx_credit_bytes() == 2 * 1514);
    inspected = 0; op->poll();
    CHECK(inspected == 2 && rx_calls == 6 && op->rx_credit_bytes() == 0);
    for (int i = 3; i <= 6; i++) {
        CHECK(op->rx(frame, sizeof(frame), &n) == 0 && n == 14);
        CHECK(frame[0] == 42 && frame[1] == i && frame[12] == 0x86);
    }
    CHECK(op->rx_credit_bytes() / LINK_PAGE_BYTES > LINK_CREDIT_MARGIN);
    /* Seven bad descriptors leave room in the budget for only one good. */
    bad = 7; good = 3; inspected = 0; op->poll();
    CHECK(inspected == 8 && good == 2 && op->rx_credit_bytes() == 3 * 1514);
    CHECK(op->rx(frame, sizeof(frame), &n) == 0 && frame[1] == 7);
    good = 0; inspected = 0; op->poll();
    CHECK(inspected == 1 && op->rx_credit_bytes() == 4 * 1514);
    CHECK(op->rx(frame, sizeof(frame), &n) == OS32_ERR_AGAIN);
    good = 1; rx_rc = GEM_EIO; inspected = 0; op->poll();
    CHECK(inspected == 1 && op->rx_credit_bytes() == 4 * 1514);
    rx_rc = GEM_OK; good = 1; inspected = 0; op->poll();
    CHECK(op->rx_credit_bytes() == 3 * 1514);
    running = 0; op->poll();
    int old_calls = calls;
    CHECK(op->tx(frame, 14) == OS32_ERR_HOSTDOWN);
    CHECK(op->rx_credit_bytes() == 0);
    net_gem_adapter.release(); running = 1;
    op->poll(); CHECK(calls == old_calls);
    CHECK(net_gem_adapter.acquire(&op) == 0);
    CHECK(op->rx(frame, sizeof(frame), &n) == OS32_ERR_AGAIN);
    CHECK(op->rx_credit_bytes() == 4 * 1514);
    net_gem_adapter.release(); finish(0);
}
