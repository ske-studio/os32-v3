/* Integer-clock fixture executes the real aggregator, with no PIT model. */
#define TICK_PROBE_HOST
#define TICK_PROBE
unsigned long long tp_host_clock;
unsigned int kctx_irq_depth;
void *kmemset(void *p, int c, unsigned int n)
{
    unsigned char *q = p;
    while (n--) *q++ = (unsigned char)c;
    return p;
}
#include TP_SOURCE
static void fail(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) fail(); } while (0)
static const struct tp_snapshot *stop(void)
{
    struct tp_control c = {2, 0};
    CHECK(tick_probe_control(&c) == 0);
    return c.snapshot;
}
static void start(void)
{
    struct tp_control c = {1, 0};
    CHECK(tick_probe_control(&c) == 0);
}
static void sample(unsigned int id, unsigned long long d)
{
    tick_probe_begin(id); tp_host_clock += d; tick_probe_end(id);
}
static int splash_path;
static void boot_splash(void)
{
    sample(TP_SLOT0, 60000);
    if (splash_path == 0) return; /* platform_splash_begin failure */
    if (splash_path == 1) return; /* diagnostic branch */
    sample(TP_SLOT1, 10000);
}
static void boot_boundary(void)
{
#include TP_BOUNDARY
}
static unsigned int link_ready = 1, tick_count;
static unsigned int stage_duration;
static void link_rx_pump(void) { tp_host_clock += stage_duration; }
static void link_host_deadlines(void) { tp_host_clock += stage_duration * 2; }
static void link_timers(void) { tp_host_clock += stage_duration * 3; }
static void link_tx_round(void) { tp_host_clock += stage_duration * 4; }
static void link_notify_tick(unsigned int t) { (void)t; tp_host_clock += stage_duration * 5; }
#include TP_LINK
#define TICK_SLOT_COUNT 7
#define ARCH_TICK_BUDGET_FIRST_SLOT 2
struct test_slot { void (*fn)(void); };
static struct test_slot slots[TICK_SLOT_COUNT];
static void tick_budget_begin(void) { tp_host_clock += 3; }
static void tick_budget_end(void) { tp_host_clock += 5; }
static void slot_work(void) { tp_host_clock += 10; }
#include TP_TICK
void _start(void)
{
    tick_probe_boot(); stop(); start();
    for (unsigned int i = 0; i < 99; i++) sample(TP_SLOT0, 32);
    sample(TP_SLOT0, 60000);
    sample(TP_SLOT1, 0x100000001ULL);
    sample(TP_SLOT3, 100); sample(TP_SLOT3, 1);
    const struct tp_snapshot *s = stop();
    const struct tp_stat *v = &s->bank[0][TP_SLOT0];
    CHECK(v->count == 100 && v->worst == 60000 && v->total == 63168);
    CHECK(v->hist[5] == 99 && v->hist[15] == 1);
    CHECK(tick_probe_p99(v) == 32);
    CHECK(v->p99_upper == 32);
    CHECK(s->bank[0][TP_SLOT1].total == 0x100000001ULL);
    CHECK(s->bank[0][TP_SLOT3].worst == 100);
    CHECK(tick_probe_p99(&s->bank[0][TP_SLOT1]) == 0x100000001ULL);
    CHECK(s->elapsed[0] == 0x100000001ULL + 63168 + 101);
    for (splash_path = 0; splash_path < 3; splash_path++) {
        start(); boot_boundary();
        sample(TP_SLOT2, 1200); /* must remain visible AFTER every return */
        s = stop();
        CHECK(s->bank[1][TP_SLOT0].count == 1);
        CHECK(s->bank[0][TP_SLOT0].count == 0);
        CHECK(s->bank[0][TP_SLOT2].worst == 1200);
        CHECK(s->bank[1][TP_SLOT2].count == 0);
        CHECK(s->splash_leave >= s->splash_enter + 60000);
    }
    start();
    tick_probe_begin(TP_SLOT0); tp_host_clock += 5;
    tick_probe_splash(1); tp_host_clock += 7;
    tick_probe_splash(0); tp_host_clock += 9; tick_probe_end(TP_SLOT0);
    s = stop();
    CHECK(s->bank[1][TP_SLOT0].worst == 21);
    CHECK(s->bank[0][TP_SLOT0].count == 0);
    CHECK(s->elapsed[0] == 14 && s->elapsed[1] == 7);
    start();
    tick_probe_irq_enter(0); tp_host_clock += 10;
    sample(TP_TIMER, 100); sample(TP_EOI, 20);
    sample(TP_REFLECT, 30); sample(TP_TIMEOUT, 40);
    tp_host_clock += 10; tick_probe_irq_leave(0);
    tp_host_clock += 1000; tick_probe_irq_enter(0);
    tp_host_clock += 10; tick_probe_irq_leave(0);
    tick_probe_if_begin(0x200); tp_host_clock += 1200;
    tick_probe_if_begin(0); tick_probe_if_end(0);
    tp_host_clock += 100; tick_probe_if_end(0x200);
    kctx_irq_depth = 1;
    tick_probe_if_begin(0x200); tp_host_clock += 10000;
    tick_probe_if_end(0x200); kctx_irq_depth = 0;
    s = stop();
    CHECK(s->bank[0][TP_IRQ0].count == 2 && s->bank[0][TP_IRQ0].total == 220);
    CHECK(s->bank[0][TP_SHORT].total == 50);
    CHECK(s->bank[0][TP_INTERVAL].worst == 1210);
    CHECK(s->bank[0][TP_IF0].count == 1 && s->bank[0][TP_IF0].worst == 1300);
    CHECK(s->lost == 0);
    CHECK(s->if0_caller[0] != 0);
    start();
    stage_duration = 3; tick_probe_net_start(); tick_probe_net_begin(TP_NET0);
    tp_host_clock += 6; tick_probe_net_end(TP_NET0); link_tick(); tick_probe_net_finish();
    stage_duration = 1; tick_probe_net_start(); tick_probe_net_begin(TP_NET0);
    tp_host_clock += 2; tick_probe_net_end(TP_NET0); link_tick(); tick_probe_net_finish();
    s = stop();
    CHECK(s->net[0].total == 51 && s->net[0].part[0] == 6);
    for (unsigned int i = 1; i < TP_NET_COUNT; i++) CHECK(s->net[0].part[i] == i * 3);
    CHECK(s->lost == 0);
    start();
    tick_probe_irq_enter(0); kctx_irq_depth = 1;
    struct tp_device_mark rx = tick_probe_device_start(); tp_host_clock += 2;
    struct tp_device_mark dma = tick_probe_device_start(); tp_host_clock += 3;
    tick_probe_pio_begin(); tp_host_clock += 7; tick_probe_pio_end();
    tp_host_clock += 11; tick_probe_device_end(TP_DMA, dma, 60);
    tp_host_clock += 13; tick_probe_device_end(TP_RX, rx, 60);
    kctx_irq_depth = 0; tick_probe_irq_leave(0);
    tick_probe_irq_enter(5); kctx_irq_depth = 1;
    struct tp_device_mark tx = tick_probe_device_start(); tp_host_clock += 19;
    tick_probe_device_end(TP_TX, tx, 42); kctx_irq_depth = 0; tick_probe_irq_leave(5);
    s = stop();
    CHECK(s->device[0][TP_RX][TP_IRQ0_CTX].full.worst == 36);
    CHECK(s->device[0][TP_DMA][TP_IRQ0_CTX].full.worst == 21);
    CHECK(s->device[0][TP_RX][TP_IRQ0_CTX].pio.worst == 7);
    CHECK(s->device[0][TP_DMA][TP_IRQ0_CTX].pio.worst == 7);
    CHECK(s->device[0][TP_DMA][TP_IRQ0_CTX].bytes == 60);
    CHECK(s->device[0][TP_TX][TP_IRQ5_CTX].full.worst == 19);
    CHECK(s->device[0][TP_TX][TP_IRQ5_CTX].bytes == 42 && s->lost == 0);
    start();
    slots[2].fn = slot_work; slots[3].fn = slot_work; slots[4].fn = slot_work;
    tick_dispatch(); s = stop();
    CHECK(s->extra[0][TP_BUDGET].count == 1 && s->extra[0][TP_BUDGET].worst == 38);
    start();
    tick_probe_irq_enter(0); tick_probe_irq_leave(0);
    tp_host_clock += (1ULL << 22); tick_probe_irq_enter(0); tick_probe_irq_leave(0);
    tp_host_clock += (1ULL << 22) + 1; tick_probe_irq_enter(0); tick_probe_irq_leave(0);
    s = stop();
    CHECK(s->bank[0][TP_INTERVAL].hist[14] == 1 && s->bank[0][TP_INTERVAL].hist[15] == 1);
    CHECK(s->bank[0][TP_INTERVAL].p99_upper == (1ULL << 22) + 1);
    start(); tick_probe_begin(TP_SLOT0); tick_probe_begin(TP_SLOT0);
    s = stop(); CHECK(s->lost == 2); /* overwrite + unfinished */
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
