/* Freestanding fake endpoints run the product state machine and CPU guards. */
#include "types.h"
#include "pit.h"
#include "os32_kapi_shared.h"
static u32 fake_flags, mutable_id, max_leaf, features, cpuid_calls;
static u32 fake_ms, rdtsc_calls, endpoint_cycles = 100;
static unsigned long long counter, pit_us;
static int pit_rc, slots_rc, registrations;
static struct pit_setup setup = {1996800, 1000, 1997, 1000, PIT_MODE_TIMER0, 1};
volatile u32 kctx_irq_depth;
static unsigned int enabled = 1, saves, restores;
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) finish(1); } while (0)
static void tsc_fake(u32 *lo, u32 *hi)
{ ++rdtsc_calls; *lo = (u32)counter; *hi = (u32)(counter >> 32); }
unsigned int irq_save(void) { u32 old = enabled; enabled = 0; ++saves; return old; }
void irq_restore(unsigned int old) { CHECK(!enabled); enabled = old; ++restores; }
int _irq_enabled(void) { return enabled; }
u32 tick_clock_ms(void) { return fake_ms; }
const struct pit_setup *pit_get_setup(void) { return &setup; }
int pc98_pit_read_us(u32 *lo, u32 *hi)
{
    CHECK(!enabled);
    counter += endpoint_cycles;
    if (pit_rc) return pit_rc;
    *lo = (u32)pit_us; *hi = (u32)(pit_us >> 32); return 0;
}
int idle_work_register(void (*fn)(void))
{
    (void)fn; ++registrations; return slots_rc;
}
#include "platform/pc98/tsc_math.c"
#include "platform/pc98/tsc_clock.c"

static void reset(void)
{
    fake_flags = 0x202; mutable_id = 1; max_leaf = 1; features = 1u << 4;
    enabled = 1; kctx_irq_depth = 0; counter = 1000; pit_us = 10;
    fake_ms = 0; pit_rc = 0; endpoint_cycles = 100;
    registered = 0; slots_rc = 0; registrations = 0;
    diag = (__typeof__(diag)){0};
    CHECK(pc98_tsc_init() == 0);
}
static void advance(u32 us, long extra)
{
    fake_ms += us / 1000; pit_us += us;
    counter += (unsigned long long)us * 100u + extra;
    pc98_tsc_calibrate_work();
}
static void ready(void)
{
    reset(); pc98_tsc_calibrate_work();
    for (u32 i = 0; i < 5; ++i) advance(200000, 0);
    CHECK(state == TSC_READY && registrations == 1);
    CHECK(enabled == 1 && saves == restores);
}
static struct window make_window(u32 us, long difference)
{
    return (struct window){(unsigned long long)((long)us + difference) * 100u, 100, us};
}
static void monitor_pair(long difference)
{
    ready(); struct window w = make_window(200000, difference);
    monitor(&w); CHECK(state == TSC_READY);
    if (difference == 3000 || difference < 0) CHECK(diag.abnormal == 1);
    monitor(&w); CHECK(state == TSC_FAILED);
}
static void tests(void)
{
    /* Real capability control flow, fake immutable ID / CPUID leaves. */
    reset(); mutable_id = 0; cpuid_calls = 0; rdtsc_calls = 0;
    CHECK(pc98_tsc_init() == OS32_ERR_NOSYS && !cpuid_calls && !rdtsc_calls);
    mutable_id = 1; max_leaf = 0; cpuid_calls = 0;
    CHECK(pc98_tsc_init() == OS32_ERR_NOSYS && cpuid_calls == 1 && !rdtsc_calls);
    max_leaf = 1; features = 0;
    CHECK(pc98_tsc_init() == OS32_ERR_NOSYS && !rdtsc_calls);
    features = 1u << 4; slots_rc = -1; registered = 0;
    CHECK(pc98_tsc_init() == OS32_ERR_FULL && state == TSC_OFF);
    reset(); CHECK(registrations == 1); CHECK(pc98_tsc_init() == 0 && registrations == 1);
    u32 l = 123, h = 456;
    CHECK(pc98_tsc_read_us(&l, &h) != 0 && l == 123 && h == 456);
    enabled = 0; pc98_tsc_calibrate_work(); CHECK(!have_start && !enabled);
    enabled = 1; kctx_irq_depth = 1; pc98_tsc_calibrate_work(); CHECK(!have_start);
    kctx_irq_depth = 0;
    /* Scale high-word, coefficient bounds, 10 seconds, fraction retention. */
    for (u32 hz = 100000000; hz <= 4000000000u; hz += 100000000) {
        u32 m, s, f;
        CHECK(tsc_coefficient(hz / 5, 200000, &m, &s));
        unsigned long long d = (unsigned long long)hz * 10;
        unsigned long long us = tsc_scale(d, m, s, 0, &f);
        CHECK(us <= 10000000 && us >= 9999900);
        CHECK(us == d * m / (1ULL << s));
        unsigned long long a = tsc_scale(d / 3, m, s, 0, &f);
        a += tsc_scale(d - d / 3, m, s, f, &f);
        CHECK(a == us);
    }
    CHECK(!tsc_coefficient(0, 200000, &l, &h));
    ready(); unsigned long long initial = join(epoch.us_lo, epoch.us_hi);
    unsigned long long origin = join(epoch.lo, epoch.hi);
    for (u32 i = 0; i < 20; ++i) {
        counter += 1000000001u;
        CHECK(pc98_tsc_read_us(&l, &h) == 0);
        CHECK(join(l,h) == initial + (counter - origin) * epoch.mult / (1ULL << epoch.shift));
    }
    epoch.us_lo = 0xfffffff0u; epoch.us_hi = 2;
    epoch.lo = (u32)counter; epoch.hi = (u32)(counter >> 32);
    epoch_fraction = 0; last_us = join(epoch.us_lo, epoch.us_hi);
    counter += 10000;
    for (u32 flag = 0; flag < 2; ++flag) {
        enabled = flag; CHECK(pc98_tsc_read_us(&l, &h) == 0 && enabled == flag);
        CHECK(h == 3 && l >= 83 && l <= 84);
    }
    enabled = 1;
    counter = join(epoch.lo, epoch.hi) - 1; l = 123; h = 456;
    CHECK(pc98_tsc_read_us(&l, &h) != 0 && l == 123 && h == 456 && enabled);
    /* Monitor quantitative boundaries: rational P for both real PIT clocks. */
    for (u32 clk = 0; clk < 2; ++clk) {
        setup.clk_hz = clk ? 2457600 : 1996800; setup.reload = clk ? 2458 : 1997;
        for (u32 us = 180000; us <= 220000; us += 40000) {
            ready();
            unsigned long long p = (unsigned long long)setup.reload * 1000000u;
            u32 limit = 8u + (550u * us + 999999u) / 1000000u;
            for (u32 k = 1; k <= 2; ++k) {
                CHECK(missing_window((p*k + setup.clk_hz-1)/setup.clk_hz + limit-1, us));
                CHECK(missing_window(p*k/setup.clk_hz - limit+1, us));
            }
            CHECK(!missing_window(p*3/setup.clk_hz, us));
            CHECK(!missing_window(300, us));
        }
    }
    ready(); epoch.mult = 1u << 31; epoch.shift = 32;
    struct window boundary = {400400, 1, 200000};
    monitor(&boundary); monitor(&boundary); CHECK(state == TSC_READY && !diag.abnormal);
    boundary.cycles += 2; monitor(&boundary); monitor(&boundary);
    CHECK(state == TSC_FAILED);
    monitor_pair(300); monitor_pair(-300); monitor_pair(-1000); monitor_pair(3000); monitor_pair(1000); monitor_pair(2000);
    ready(); struct window bad = make_window(200000, 300), good = make_window(200000, 0);
    monitor(&bad); monitor(&good); monitor(&bad); CHECK(state == TSC_READY && diag.abnormal == 1);
    struct window suspect = make_window(200000, 1000);
    monitor(&suspect); CHECK(state == TSC_READY && !diag.abnormal && diag.degraded);
    monitor(&bad); CHECK(state == TSC_READY && diag.abnormal == 1);
    reject(REJECT_READ); monitor(&bad); CHECK(state == TSC_READY && diag.abnormal == 1);
    bad.cost = 401; monitor(&bad); CHECK(!diag.abnormal && state == TSC_READY);
    /* Calibration drops isolated missing periods, then obtains replacements. */
    reset(); pc98_tsc_calibrate_work();
    advance(200000, 100000); /* apparent +1P */
    for (u32 i = 0; i < 5; ++i) advance(200000, 0);
    CHECK(state == TSC_READY && diag.rejected[REJECT_MISSING]);
    /* Identical missing rates are indistinguishable: accepted, not "detected". */
    reset(); pc98_tsc_calibrate_work();
    for (u32 i = 0; i < 5; ++i) advance(200000, 100000);
    CHECK(state == TSC_READY);
    reset(); pc98_tsc_calibrate_work(); advance(221000, 0);
    CHECK(state == TSC_CALIBRATING && !collected && diag.rejected[REJECT_LENGTH]);
    reset(); pc98_tsc_calibrate_work(); pit_rc = -1;
    for (u32 i = 0; i < 10; ++i) advance(200000, 0);
    CHECK(state == TSC_OFF && diag.rejected[REJECT_LIMIT]);
    reset(); pc98_tsc_calibrate_work();
    pit_us += 170000; fake_ms += 200; counter += 20000000;
    pc98_tsc_calibrate_work(); CHECK(diag.rejected[REJECT_LENGTH] && !collected);
    reset(); endpoint_cycles = 501; pc98_tsc_calibrate_work();
    for (u32 i = 0; i < 5; ++i) advance(200000, 0);
    CHECK(state != TSC_READY && diag.rejected[REJECT_COST]);
    reset(); pc98_tsc_calibrate_work(); pit_rc = -1; advance(200000, 0);
    CHECK(!have_start && diag.rejected[REJECT_READ]);
    reset(); pc98_tsc_calibrate_work();
    for (u32 i = 0; i < 5; ++i) advance(200000, (i & 1) ? 50000 : 0);
    CHECK(state != TSC_READY && diag.rejected[REJECT_SPREAD]);
    ready(); advance(1000000, 0); counter = start.tsc;
    advance(200000, -20000000); CHECK(state == TSC_FAILED);
    CHECK(enabled && saves == restores);
}
void _start(void) { tests(); finish(0); }
