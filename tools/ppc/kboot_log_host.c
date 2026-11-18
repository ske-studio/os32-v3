/* Freestanding 32-bit host: only MMIO is replaced, product code is linked. */
#include "kboot_log.h"
#include "ofw.h"
static char output[256];
static unsigned used, reads, stalled, drain_stalled, indexed, calls, last_used;
/* Independent Z8530 fixture values. Error status follows the FIFO head. */
static const unsigned char rx_fifo[] = { 0x00, 0xff, 0x53, 0x80, 0x41, 0x50 };
static const unsigned char rx_status[] = { 0, 0x20, 0x20, 0x40, 0x10, 0x50 };
static unsigned rx_head, rx_tail, rx_resets, rx_error, interleave;
static unsigned rx_arrival, single_poll;
/* Stable exit codes used by the receive mutations in kboot_log.py. */
enum {
    RX_EXTRA_POLL = 200,
    RX_FIRST_BYTE,
    RX_DIAG_CLEAN,
    RX_DIAG_FIRST_OVERRUN,
    RX_DIAG_SECOND_OVERRUN,
    RX_DIAG_FRAMING,
    RX_DIAG_PARITY,
    RX_DIAG_FRAMING_AND_PARITY,
};
static void finish(int status)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status));
    __builtin_unreachable();
}
#define REQUIRE(x) do { if (!(x)) finish(__LINE__); } while (0)
static void expect_diag(u32 expected_oe, u32 expected_fe, u32 expected_pe, int failure)
{
    u32 oe, fe, pe, overflow;
    pm_escc_diag(&oe, &fe, &pe, &overflow);
    if (oe != expected_oe || fe != expected_fe || pe != expected_pe || overflow)
        finish(failure);
}

unsigned ppc_load8(cell a)
{
    if (a == 0xfe300010) {
        REQUIRE(!indexed && rx_head < rx_tail && !rx_error);
        rx_error = rx_status[rx_head];
        return rx_fifo[rx_head++];
    }
    REQUIRE(a == 0xfe300000);
    reads++;
    if (single_poll && reads != 1) finish(RX_EXTRA_POLL);
    if (rx_arrival && reads == rx_arrival) rx_tail = sizeof(rx_fifo);
    /* Runtime watchdog: removal of the product bound fails, never hangs. */
    REQUIRE(reads <= PM_POLL_LIMIT + 1 + (drain_stalled ? 7 : 0));
    if (indexed) {
        indexed = 0;
        return (drain_stalled ? 0 : 0x01) |
               (rx_head < rx_tail ? rx_status[rx_head] : 0);
    }
    return (stalled ? 0 : 0x04) | (rx_head < rx_tail ? 0x01 : 0);
}
void ppc_store8(cell a, unsigned value)
{
    if (a == 0xfe300000) {
        REQUIRE(!indexed);
        if (value == 0x30) {
            REQUIRE(rx_error);
            rx_error = 0;
            ++rx_resets;
        } else {
            REQUIRE(value == 1);
            indexed = 1;
        }
    } else {
        REQUIRE(a == 0xfe300010 && used < sizeof(output));
        output[used++] = (char)value;
        if (interleave) {
            interleave = 0;
            REQUIRE(pm_escc_trygetchar() == 0xff);
            expect_diag(1, 0, 0, RX_DIAG_FIRST_OVERRUN);
        }
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

    drain_stalled = reads = used = last_used = 0;
    single_poll = 1;
    REQUIRE(pm_escc_trygetchar() == -1 && reads == 1);
    single_poll = 0;
    rx_arrival = reads + 3;
    if (pm_escc_getchar_kapi() != 0x00) finish(RX_FIRST_BYTE);
    expect_diag(0, 0, 0, RX_DIAG_CLEAN);
    interleave = 1;
    pm_log_write("LOG", 3);
    expect("LOG", 3);
    REQUIRE(!interleave);
    REQUIRE(pm_escc_trygetchar() == 0x53);
    expect_diag(2, 0, 0, RX_DIAG_SECOND_OVERRUN);
    REQUIRE(pm_escc_getchar_kapi() == 0x80);
    expect_diag(2, 1, 0, RX_DIAG_FRAMING);
    REQUIRE(pm_escc_trygetchar() == 0x41);
    expect_diag(2, 1, 1, RX_DIAG_PARITY);
    REQUIRE(pm_escc_trygetchar() == 0x50);
    expect_diag(2, 2, 2, RX_DIAG_FRAMING_AND_PARITY);
    REQUIRE(rx_head == rx_tail && rx_resets == 5);
    pm_escc_diag(0, 0, 0, 0);
    reads = 0;
    single_poll = 1;
    REQUIRE(pm_escc_trygetchar() == -1 && reads == 1);
    finish(0);
}
