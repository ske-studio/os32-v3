#include "types.h"
#include "platform_timer.h"
#include "pit.h"
static struct pit_setup setup;
static u32 now;
static unsigned int reads;
#define TICK_BUDGET_REAL
#define _start link_fixture_start
#include "hostsvc_link_core_host.c"
#undef _start
#undef CHECK
const struct pit_setup *pit_get_setup(void) { return &setup; }
unsigned int pit_read_count(void) { reads++; return now; }
#include "platform/pc98/tick_budget.c"
#include "kernel/irq/tick_budget.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static int run(void)
{
    setup.clk_hz = 2000000; setup.reload = 20000;
    tick_budget_config();
    CHECK(tick_budget_cnt(120) == 240 && tick_budget_cnt(60) == 120);
    CHECK(tick_budget_cnt(23) == 46 && tick_budget_cnt(17) == 34);
    /* Inactive foreground and IRQ5 ignore elapsed time. */
    now = 19900;
    CHECK(tick_budget_take(0, 99) && tick_budget_take(0, 99));
    tick_count = 0; now = 1000; tick_budget_begin();
    now = 999; CHECK(tick_budget_take(120, 46));
    now = 926; CHECK(!tick_budget_take(120, 46)); /* exact sum */
    tick_budget_end(); CHECK(tick_budget_take(0, 99));
    /* An over-budget entrance grants only the current side, once globally. */
    for (u32 parity = 0; parity < 2; ++parity) {
        tick_count = parity; now = 1000; tick_budget_begin(); now = 700;
        CHECK(!tick_budget_take(parity ? 120 : 240, 34));
        CHECK(tick_budget_take(parity ? 240 : 120, 46));
        CHECK(!tick_budget_take(parity ? 240 : 120, 46));
        CHECK(!tick_budget_take(parity ? 120 : 240, 34));
        tick_budget_end();
    }
    /* Product PIT latch/one-wrap formula, including base < current. */
    now = 10; tick_budget_begin(); now = 19970;
    CHECK(arch_tick_budget_elapsed(base) == 40);
    CHECK(tick_budget_take(120, 46));
    tick_budget_end();
    /* A prior RX unit consumed the shared first-unit guarantee. TX can take
     * just before its reserved-tail boundary, then U + notify stays <= B+U. */
    tick_count = 1; now = 1000; tick_budget_begin();
    CHECK(tick_budget_take(120, 46));
    now = 795; /* elapsed 205 = B - tail - 1 */
    CHECK(tick_budget_take(240, 34));
    now -= 30; /* U = 15us */
    CHECK(!tick_budget_take(240, 34));
    CHECK(arch_tick_budget_elapsed(base) + 34 <= 240 + 30);
    tick_budget_end();
    /* Exercise the actual link call sites at the tail boundary. A consumed
     * unit prevents the minimum-progress escape from hiding a missing tail. */
    const u8 mac[6] = {2, 3, 4, 5, 6, 7};
    link_init(mac); tx_result = 0; tx_calls = 0;
    now = 1000; tick_budget_begin();
    CHECK(tick_budget_take(120, 46));
    now = 793; /* elapsed 207, beyond B - TX tail, but before B */
    link_tx_round(); CHECK(tx_calls == 0 && link_hs_due == 1);
    tick_budget_end(); link_tx_round(); CHECK(tx_calls == 1 && !link_hs_due);
    rx_length = 1;
    now = 1000; tick_budget_begin(); CHECK(tick_budget_take(120, 46));
    now = 924; /* elapsed 76, beyond RX share - tail */
    rx_calls = 0; link_rx_pump(); CHECK(rx_calls == 0);
    tick_budget_end(); link_rx_pump(); CHECK(rx_calls == 1);
    /* Empty link tick reads only the fixed entrance clock. */
    link_init(mac); link_hs_due = 0; link_hs_tick = tick_count;
    rx_length = 0; reads = 0; now = 1000;
    tick_budget_begin(); link_tick(); tick_budget_end(); CHECK(reads == 1);
    /* Five accepted SYNs, then idle intervals; only agent input restores RTO. */
    link_init(mac); tx_result = 0; tick_count = 0; tx_calls = 0;
    link_tick();
    for (int i = 1; i < LINK_TRIES; i++) {
        tick_count += LINK_RTO_TICKS; link_tick();
        CHECK(tx_calls == i + 1);
    }
    tick_count += LINK_RTO_TICKS; link_tick(); CHECK(tx_calls == LINK_TRIES);
    tick_count += LINK_HELLO_IDLE_TICKS - LINK_RTO_TICKS; link_tick();
    CHECK(tx_calls == LINK_TRIES + 1);
    receive(LINK_OP_HELLO, LINK_HS_SYN, 0, 0, 0, 0, 0);
    link_tick();
    tick_count += LINK_RTO_TICKS; link_tick(); CHECK(tx_calls == LINK_TRIES + 1);
    const u8 synack[LINK_HELLO_SA_LEN] = {0};
    receive(LINK_OP_HELLO, LINK_HS_SYNACK, 0, 0, 0, synack, sizeof(synack));
    link_tick(); CHECK(tx_calls == LINK_TRIES + 2);
    setup.clk_hz = 2457600; tick_budget_config();
    CHECK(tick_budget_cnt(120) == 294 && tick_budget_cnt(23) == 56);
    setup.clk_hz = 1996800; tick_budget_config();
    CHECK(tick_budget_cnt(120) == 239 && tick_budget_cnt(17) == 33);
    return 0;
}
void _start(void)
{
    int rc = run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
