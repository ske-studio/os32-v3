/* Real providers and TSC state transitions; only hardware boundaries are fake. */
#include "types.h"
#include "pit.h"
#include "platform_timer.h"
#include "sys/kern/io_clock.h"
#include "sys/kern/io_work.h"
#include "os32_kapi_shared.h"
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) { unsigned int line = __LINE__; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(&line), "d"(4) : "memory"); \
    finish(1); } } while (0)
static unsigned int enabled = 1, saves, restores, samples, retries;
static u32 raw_low, raw_high, phase, pending, fake_ms;
static unsigned long long fake_tsc;
static struct pit_setup setup;
static const struct io_clock_ops *installed;
volatile u32 kctx_irq_depth;
unsigned int irq_save(void) { unsigned int old = enabled; enabled = 0; ++saves; return old; }
void irq_restore(unsigned int old) { CHECK(!enabled); enabled = old; ++restores; }
int _irq_enabled(void) { return enabled; }
static void tsc_fake(u32 *lo, u32 *hi)
{ CHECK(!enabled); *lo = (u32)fake_tsc; *hi = (u32)(fake_tsc >> 32); }
const struct pit_setup *pit_get_setup(void) { return &setup; }
int pit_init(unsigned int hz) { (void)hz; return 0; }
int tick_clock_config(u32 num, u32 den) { return num && den ? 0 : -1; }
void tick_budget_config(void) {}
void tick_clock_raw(u32 *lo, u32 *hi)
{ CHECK(!enabled); *lo = raw_low; *hi = raw_high; }
u32 tick_clock_ms(void) { return fake_ms; }
int io_register(const struct io_desc *d) { (void)d; return 0; }
void io_request(u32 id) { (void)id; }
void platform_timer_sample(struct platform_timer_sample *out)
{
    CHECK(!enabled); ++samples;
    *out = (struct platform_timer_sample){.count = phase, .p1 = pending, .p2 = samples <= retries};
}
void io_clock_set_ops(const struct io_clock_ops *ops) { CHECK(!enabled); installed = ops; }
#include "sys/platform/pc98/tsc_clock.c"
#include "sys/platform/pc98/clock_source.c"

static void pit(u32 hz, u32 reload)
{
    setup = (struct pit_setup){hz, 1000, reload, 1000, PIT_MODE_TIMER0, 1};
    CHECK(platform_clock_init(1000) == 0);
    samples = retries = pending = raw_low = raw_high = 0; phase = reload;
}
static void token(u32 *count, u32 *source, u32 *gen)
{
    unsigned int old = enabled;
    CHECK(pc98_io_clock_ops.read(count, source, gen) == 0);
    CHECK(enabled == old && saves == restores);
}
static void conversion(u32 us, u32 source, u32 gen, u32 want)
{
    u32 out = 0x12345678;
    CHECK(pc98_io_clock_ops.us_to_count(us, source, gen, &out) == 0);
    CHECK(out == want && saves == restores);
}
static void calibrated(void)
{
    collected = TSC_WINDOWS;
    for (u32 i = 0; i < collected; ++i) windows[i] = (struct window){20000000, 100, 200000};
    start = (struct endpoint){fake_tsc, 1000000, 100};
    CHECK(calibrate());
}
static void tests(void)
{
    u32 c = 17, src = 18, gen = 19, out = 20, m = 21, s = 22, g = 23;
    pc98_io_clock_install(); CHECK(!installed);
    CHECK(pc98_io_clock_ops.read(&c, &src, &gen) == OS32_ERR_NOSYS);
    CHECK(c == 17 && src == 18 && gen == 19);
    CHECK(pc98_io_clock_ops.us_to_count(1, 1, 0, &out) == OS32_ERR_NOSYS && out == 20);
    CHECK(pc98_tsc_rate(&m, &s, &g) == OS32_ERR_NOSYS && m == 21 && s == 22 && g == 23);
    CHECK(pc98_io_clock_ops.read_us(&m, &s) == OS32_ERR_NOSYS && m == 21 && s == 22);
    /* PIT coefficient: low rate, near-u32 mul, both physical oscillators. */
    static const u32 rates[] = {1, 999999, 1000000, 1996800, 2457600};
    static const u32 shifts[] = {32, 32, 31, 31, 30};
    for (u32 i = 0; i < sizeof(rates)/sizeof(rates[0]); ++i) {
        pit(rates[i], 1); token(&c, &src, &gen);
        CHECK(io_shift == shifts[i]);
        unsigned long long mul = ((unsigned long long)rates[i] << shifts[i]) / 1000000;
        CHECK(io_mul == mul);
        for (u32 us = 0; us < 10000000; us += 999983)
            conversion(us, src, gen, (u32)(((unsigned long long)us * mul) >> shifts[i]));
    }
    /* Fractions truncate, including a coefficient whose top bit is set. */
    pit(999999, 1); token(&c, &src, &gen);
    CHECK(io_mul == 4294963001u); conversion(1000000, src, gen, 999998);
    pit(1000000, 1); token(&c, &src, &gen);
    conversion(0x7fffffff, src, gen, 0x7fffffff);
    CHECK(pc98_io_clock_ops.us_to_count(0x80000000, src, gen, &out) == OS32_ERR_AGAIN);
    CHECK(pc98_io_clock_ops.us_to_count(0xffffffff, src, gen, &out) == OS32_ERR_AGAIN && out == 20);
    CHECK(pc98_io_clock_ops.us_to_count(1, src + 1, gen, &out) == OS32_ERR_AGAIN);
    CHECK(pc98_io_clock_ops.us_to_count(1, src, gen - 1, &out) == OS32_ERR_AGAIN);
    pc98_io_clock_install(); CHECK(installed == &pc98_io_clock_ops);
    /* Phase, pending, retries, physical-period rollover and raw u32 rollover. */
    pit(1996800, 1997); raw_low = 10; phase = 1900;
    token(&c, &src, &gen); CHECK(c == 20067);
    pending = 1; token(&c, &src, &gen); CHECK(c == 22064);
    pending = 0; samples = 0; retries = 2;
    token(&c, &src, &gen); CHECK(c == 20067 && samples == 3);
    samples = 0; retries = TIME_RETRY_MAX;
    CHECK(pc98_io_clock_ops.read(&c, &src, &gen) == OS32_ERR_AGAIN && c == 20067);
    retries = 0; raw_low = 0xffffffff; raw_high = 7; pending = 1; phase = 1997;
    token(&c, &src, &gen); CHECK(c == 0);
    pending = 0; raw_low = 0; raw_high = 8; phase = 1996;
    token(&c, &src, &gen); CHECK(c == 1);
    pit(1000000, 1); raw_low = 0xfffffff0; token(&c, &src, &gen);
    raw_low = 0x10; token(&out, &src, &gen); CHECK(out - c == 32);
    /* Real calibration advances the exported generation, changes the token. */
    pit(1996800, 1997); token(&c, &src, &gen);
    u32 pit_source = src, pit_gen = gen, old_tsc_gen = tsc_generation;
    fake_tsc = 0xfffffff0; calibrated(); CHECK(tsc_generation == old_tsc_gen + 1);
    CHECK(pc98_tsc_rate(&m, &s, &g) == 0 && m == epoch.mult && s == epoch.shift && g == tsc_generation);
    /* Refresh also happens on conversion, before any new raw read. */
    CHECK(pc98_io_clock_ops.us_to_count(1, pit_source, pit_gen, &out) == OS32_ERR_AGAIN);
    token(&c, &src, &gen); CHECK(src != pit_source && gen == pit_gen + 1 && c == 0xfffffff0);
    fake_tsc += 32; token(&out, &src, &gen); CHECK(out - c == 32);
    u32 stable_gen = gen;
    token(&c, &src, &gen); CHECK(gen == stable_gen);
    /* TSC reciprocal boundaries: 4GHz and the largest representable mul. */
    epoch.mult = 1073741; epoch.shift = 32; ++tsc_generation;
    token(&c, &src, &gen); CHECK(io_shift == 20 && io_mul == 4194307218u);
    conversion(500000, src, gen, 2000001534u);
    CHECK(pc98_io_clock_ops.us_to_count(1000000000, src, gen, &out) == OS32_ERR_AGAIN);
    epoch.mult = 0x80000001; epoch.shift = 31; ++tsc_generation;
    token(&c, &src, &gen); CHECK(io_shift == 32 && io_mul == 0xfffffffeu);
    conversion(1000000, src, gen, 999999);
    /* Leave and re-enter READY between reads: same source, stale generation. */
    u32 before = gen, old_rate = tsc_generation;
    CHECK(pc98_tsc_init() == 0); CHECK(tsc_generation == old_rate + 1);
    calibrated(); CHECK(tsc_generation == old_rate + 2);
    CHECK(pc98_io_clock_ops.us_to_count(1, src, before, &out) == OS32_ERR_AGAIN);
    token(&c, &src, &gen); CHECK(gen == before + 1);
    /* All READY exit paths advance once; rejection while OFF does not. */
    old_rate = tsc_generation;
    for (u32 i = 0; i < TSC_REJECT_LIMIT; ++i) reject(REJECT_READ);
    CHECK(!pc98_tsc_ready() && tsc_generation == old_rate + 1);
    CHECK(pc98_io_clock_ops.us_to_count(1, src, gen, &out) == OS32_ERR_AGAIN);
    token(&c, &src, &gen); CHECK(src == pit_source);
    old_rate = tsc_generation; reject(REJECT_READ); CHECK(tsc_generation == old_rate);
    calibrated(); old_rate = tsc_generation;
    struct window bad = {21000000, 100, 200000};
    monitor(&bad); monitor(&bad); CHECK(state == TSC_FAILED && tsc_generation == old_rate + 1);
    calibrated(); old_rate = tsc_generation;
    have_start = 1; start.tsc = fake_tsc; next_work_ms = 0; fake_ms = 1;
    pc98_tsc_calibrate_work(); CHECK(state == TSC_FAILED && tsc_generation == old_rate + 1);
    /* Microseconds are exactly the public clock, preserving IF=0 and IF=1. */
    pit(1000000, 1); raw_low = 123; phase = 1;
    for (u32 flag = 0; flag < 2; ++flag) {
        enabled = flag;
        CHECK(pc98_io_clock_ops.read_us(&m, &s) == 0 && enabled == flag);
        unsigned int saved = irq_save();
        CHECK(platform_clock_read_us(&c, &g) == 0); irq_restore(saved);
        CHECK(m == c && s == g);
        token(&c, &src, &gen); conversion(123, src, gen, 123);
        CHECK(enabled == flag && saves == restores);
    }
}
void _start(void) { tests(); finish(0); }
