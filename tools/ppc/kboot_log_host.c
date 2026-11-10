/* Freestanding 32-bit host: only MMIO is replaced, product code is linked. */
#include "kboot_log.h"
#include "ofw.h"
static char output[256];
static unsigned used, reads, stalled, drain_stalled, indexed, calls, last_used;
static void finish(int status)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status));
    __builtin_unreachable();
}
#define REQUIRE(x) do { if (!(x)) finish(__LINE__); } while (0)
unsigned ppc_load8(cell a)
{
    REQUIRE(a == 0xfe300000);
    reads++;
    /* Runtime watchdog: removal of the product bound fails, never hangs. */
    REQUIRE(reads <= PM_POLL_LIMIT + 1 + (drain_stalled ? 7 : 0));
    return indexed ? (drain_stalled ? 0 : PM_RR1_ALL_SENT) :
                     (stalled ? 0 : PM_RR0_TX_READY);
}
void ppc_store8(cell a, unsigned value)
{
    if (a == 0xfe300000) {
        REQUIRE(value == PM_RR1_INDEX);
        indexed = 1;
    } else {
        REQUIRE(a == 0xfe300010 && used < sizeof(output));
        output[used++] = (char)value;
    }
}
static int queued(void *ctx, const char *bytes, u32 n)
{
    REQUIRE(ctx == &calls && bytes && n);
    if (!stalled) REQUIRE(used == last_used + n);
    last_used = used;
    calls++;
    return -1; /* queue full; no connection wait */
}
static const struct pm_log_sink queue = { queued, &calls };
static const struct pm_log_sink third = { queued, 0 };
static void expect(const char *text, unsigned n)
{
    REQUIRE(used == n);
    for (unsigned i = 0; i < n; i++) REQUIRE(output[i] == text[i]);
}
void _start(void)
{
    struct pm_log_stats s;
    u32 words[9] = { 0, 0x1234abcd, 0xffffffff, 3, 4, 5, 6, 7, 8 };
    REQUIRE(pm_log_add_sink(0) == -1);
    REQUIRE(pm_log_get_stats(2, &s) == -1);
    REQUIRE(pm_log_get_stats(0, 0) == -1);
    pm_log_write("x", 1);
    REQUIRE(!pm_log_get_stats(0, &s) && s.dropped == 1 && !s.failures);
    REQUIRE(pm_log_fatal_hex(0, 0, words, 1) == -1);
    REQUIRE(!pm_log_fatal_hex(0xfe300000, 0xfe300010, words, 9));
    expect("FATAL 00000000 1234abcd ffffffff 00000003 00000004 00000005 00000006 00000007\r\n", 79);
    used = reads = indexed = 0;
    REQUIRE(!pm_log_escc_init(0xfe300000, 0xfe300010));
    REQUIRE(pm_log_escc_init(1, 2) == -1);
    REQUIRE(!pm_log_add_sink(&queue));
    REQUIRE(pm_log_add_sink(&third) == -1);
    pm_log_write("AB", 2);
    expect("AB", 2);
    REQUIRE(calls == 1);
    REQUIRE(!pm_log_get_stats(1, &s) && s.failures == 1 && s.dropped == 2);
    stalled = 1; reads = 0;
    pm_log_write("CD", 2);
    REQUIRE(reads == PM_POLL_LIMIT + 1 && used == 2 && calls == 2);
    REQUIRE(!pm_log_get_stats(0, &s) && s.failures == 1 && s.dropped == 3);
    REQUIRE(!pm_log_get_stats(1, &s) && s.failures == 2 && s.dropped == 4);
    reads = 0;
    pm_log_write("x", (u32)-1);
    REQUIRE(!pm_log_get_stats(0, &s) && s.dropped == (u32)-1);
    REQUIRE(!pm_log_get_stats(1, &s) && s.dropped == (u32)-1);
    reads = 0;
    REQUIRE(pm_log_fatal_hex(0xfe300000, 0xfe300010, words, 1) == -1);
    REQUIRE(reads == PM_POLL_LIMIT + 1 && calls == 3);
    stalled = 0; drain_stalled = 1; reads = used = 0;
    REQUIRE(pm_log_fatal_hex(0xfe300000, 0xfe300010, words, 0) == -1);
    REQUIRE(reads == PM_POLL_LIMIT + 1 + 7);
    finish(0);
}
