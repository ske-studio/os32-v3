#include "tsc_clock.h"
#include "pit.h"
#include "cpu.h"
#include "io.h"
#include "irq.h"
#include "idle_work.h"
#include "tick_clock.h"
#include "os32_kapi_shared.h"

#define TSC_WINDOW_US 200000u
#define TSC_WINDOW_MIN_US 180000u
#define TSC_WINDOW_MAX_US 220000u
#define TSC_BOOT_LIMIT_US 2000000u
#define TSC_MONITOR_US 1000000u
#define TSC_ENDPOINT_US 4u
#define TSC_EPOCH_CYCLES 0x40000000u
#define TSC_WINDOWS 5u
#define TSC_ATTEMPTS 10u

enum tsc_state { TSC_OFF, TSC_CALIBRATING, TSC_READY, TSC_FAILED };
enum tsc_reject { REJECT_READ, REJECT_ORDER, REJECT_LENGTH, REJECT_COST,
                  REJECT_MISSING, REJECT_SPREAD, REJECT_SLOTS, REJECT_LIMIT };
static enum tsc_state state;
static struct tsc_epoch epoch;
static u32 epoch_fraction;
static unsigned long long last_us;
static volatile struct { u32 rejected[8], degraded, abnormal, missing; } diag;
struct endpoint { unsigned long long tsc, pit, cost; };
struct window { unsigned long long cycles, cost; u32 us; };
static struct endpoint start;
static struct window windows[TSC_WINDOWS];
static unsigned long long boot_start, next_monitor;
static u32 collected, attempts, next_work_ms;
static int registered, have_start, work_started;
static u32 boot_ms;

static unsigned long long rdtsc(void)
{
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
    return ((unsigned long long)hi << 32) | lo;
}
static unsigned long long join(u32 lo, u32 hi)
{ return ((unsigned long long)hi << 32) | lo; }
static void reject(enum tsc_reject reason)
{
    diag.rejected[reason]++;
    diag.degraded = 1;
    diag.abnormal = 0;
    if (reason != REJECT_MISSING) diag.missing = 0;
}
int pc98_tsc_ready(void) { return state == TSC_READY; }

int pc98_tsc_read_us(u32 *lo, u32 *hi)
{
    unsigned int flags = irq_save();
    unsigned long long now, origin, delta, us;
    u32 fraction;
    if (state != TSC_READY) { irq_restore(flags); return OS32_ERR_NOSYS; }
    now = rdtsc(); origin = join(epoch.lo, epoch.hi);
    if (now < origin) { irq_restore(flags); return OS32_ERR_AGAIN; }
    delta = now - origin;
    us = join(epoch.us_lo, epoch.us_hi) +
         tsc_scale(delta, epoch.mult, epoch.shift, epoch_fraction, &fraction);
    if (us < last_us) us = last_us;
    last_us = us;
    if (delta >= TSC_EPOCH_CYCLES) {
        epoch.lo = (u32)now; epoch.hi = (u32)(now >> 32);
        epoch.us_lo = (u32)us; epoch.us_hi = (u32)(us >> 32);
        epoch_fraction = fraction;
    }
    irq_restore(flags);
    *lo = (u32)us; *hi = (u32)(us >> 32);
    return 0;
}

static int endpoint(struct endpoint *p)
{
    u32 lo, hi;
    unsigned int flags = irq_save();
    arch_cpu_serialize();
    p->tsc = rdtsc();
    int rc = pc98_pit_read_us(&lo, &hi);
    arch_cpu_serialize();
    unsigned long long end = rdtsc();
    irq_restore(flags);
    if (rc) { reject(REJECT_READ); return 0; }
    if (end < p->tsc) { reject(REJECT_ORDER); return 0; }
    p->pit = join(lo, hi); p->cost = end - p->tsc;
    return 1;
}
static unsigned long long scaled(unsigned long long cycles, u32 m, u32 s)
{ u32 remainder; return tsc_scale(cycles, m, s, 0, &remainder); }

/* Cross multiplication retains the actual rational PIT period. */
static int missing_window(unsigned long long delta, u32 us)
{
    const struct pit_setup *ps = pit_get_setup();
    unsigned long long period = (unsigned long long)ps->reload * 1000000u;
    unsigned long long deviation = delta * ps->clk_hz;
    unsigned long long k = (deviation + period / 2) / period;
    unsigned long long limit = (8u + (550ULL * us + 999999u) / 1000000u) * ps->clk_hz;
    unsigned long long distance = deviation > k * period ? deviation - k * period : k * period - deviation;
    return k >= 1 && k <= 2 && distance <= limit;
}
static int valid_cost(const struct window *w, u32 m, u32 s)
{
    /* Compare cycles directly: truncation must not admit 4.9us endpoints. */
    unsigned long long allowance = ((unsigned long long)TSC_ENDPOINT_US << s) / m;
    return w->cost <= allowance;
}
static int calibrate(void)
{
    u32 m, s;
    unsigned int order[TSC_WINDOWS];
    for (u32 i = 0; i < collected; ++i) order[i] = i;
    for (u32 i = 1; i < collected; ++i)
        for (u32 j = i; j && windows[order[j]].cycles * windows[order[j-1]].us <
                                windows[order[j-1]].cycles * windows[order[j]].us; --j) {
            u32 t = order[j]; order[j] = order[j-1]; order[j-1] = t;
        }
    struct window *min = &windows[order[0]];
    if (!tsc_coefficient(min->cycles, min->us, &m, &s)) return 0;
    u32 keep = 0;
    for (u32 i = 0; i < collected; ++i) {
        unsigned long long t = scaled(windows[i].cycles, m, s);
        if (!valid_cost(&windows[i], m, s)) { reject(REJECT_COST); continue; }
        if (t > windows[i].us && missing_window(t - windows[i].us, windows[i].us)) {
            reject(REJECT_MISSING); continue;
        }
        windows[keep++] = windows[i];
    }
    if (keep != collected) { collected = keep; return 0; }
    struct window *max = &windows[order[collected-1]];
    unsigned long long low = min->cycles * max->us;
    unsigned long long high = max->cycles * min->us;
    if (high - low > low / 2000u) { reject(REJECT_SPREAD); return 0; }
    struct window *median = &windows[order[collected/2]];
    if (!tsc_coefficient(median->cycles, median->us, &m, &s)) return 0;
    for (u32 i = 0; i < collected; ++i) {
        unsigned long long t = scaled(windows[i].cycles, m, s);
        if (!valid_cost(&windows[i], m, s)) return 0;
        if (t > windows[i].us && missing_window(t - windows[i].us, windows[i].us)) return 0;
    }
    unsigned int flags = irq_save();
    epoch = (struct tsc_epoch){(u32)start.tsc, (u32)(start.tsc >> 32),
                               (u32)start.pit, (u32)(start.pit >> 32), m, s};
    epoch_fraction = 0; last_us = start.pit;
    state = TSC_READY;
    irq_restore(flags);
    next_monitor = start.pit + TSC_MONITOR_US;
    have_start = 0;
    return 1;
}
static void monitor(const struct window *w)
{
    unsigned long long t = scaled(w->cycles, epoch.mult, epoch.shift);
    if (!valid_cost(w, epoch.mult, epoch.shift)) { reject(REJECT_COST); return; }
    unsigned long long d = t > w->us ? t - w->us : w->us - t;
    if (t > w->us && missing_window(d, w->us)) {
        /* N3: a second adjacent suspect counts both windows as abnormal. */
        if (++diag.missing == 1) { reject(REJECT_MISSING); return; }
        diag.abnormal = 2;
    } else {
        diag.missing = 0;
        if (d * 1000u > w->us) ++diag.abnormal;
        else diag.abnormal = 0;
    }
    if (diag.abnormal >= 2) { diag.degraded = 1; state = TSC_FAILED; }
}
void pc98_tsc_calibrate_work(void)
{
    struct endpoint end;
    if (!_irq_enabled() || kctx_irq_depth) return;
    if (state != TSC_CALIBRATING && state != TSC_READY) return;
    u32 ms = tick_clock_ms();
    if (!work_started) { boot_ms = ms; work_started = 1; }
    if (state == TSC_CALIBRATING && ms - boot_ms >= TSC_BOOT_LIMIT_US / 1000u) {
        reject(REJECT_LIMIT); state = TSC_OFF; return;
    }
    if ((int)(ms - next_work_ms) < 0) return;
    if (!endpoint(&end)) { have_start = 0; next_work_ms = ms + TSC_WINDOW_US / 1000u; return; }
    next_work_ms = ms + TSC_WINDOW_US / 1000u;
    if (!boot_start) boot_start = end.pit;
    if (state == TSC_CALIBRATING && end.pit - boot_start >= TSC_BOOT_LIMIT_US) {
        reject(REJECT_LIMIT); state = TSC_OFF; return;
    }
    if (!have_start) {
        if (state == TSC_READY && end.pit < next_monitor) return;
        start = end; have_start = 1; return;
    }
    if (end.tsc <= start.tsc) {
        reject(REJECT_ORDER); have_start = 0;
        if (state == TSC_READY) state = TSC_FAILED;
        return;
    }
    if (end.pit < start.pit) { reject(REJECT_ORDER); have_start = 0; return; }
    unsigned long long elapsed = end.pit - start.pit;
    if (elapsed < TSC_WINDOW_MIN_US || elapsed > TSC_WINDOW_MAX_US) {
        reject(REJECT_LENGTH); start = end; return;
    }
    struct window w = {end.tsc - start.tsc,
                       end.cost > start.cost ? end.cost : start.cost, (u32)elapsed};
    start = end;
    if (state == TSC_READY) {
        monitor(&w); have_start = 0;
        next_monitor = end.pit + TSC_MONITOR_US - elapsed;
        next_work_ms = ms + (TSC_MONITOR_US - (u32)elapsed) / 1000u;
        return;
    }
    ++attempts;
    windows[collected++] = w;
    if (collected == TSC_WINDOWS && calibrate()) { next_work_ms = ms + 1000u; return; }
    if (collected == TSC_WINDOWS) collected = 0;
    if (attempts >= TSC_ATTEMPTS) { reject(REJECT_LIMIT); state = TSC_OFF; }
}
int pc98_tsc_init(void)
{
    state = TSC_OFF;
    collected = attempts = have_start = work_started = 0; boot_start = 0;
    diag.abnormal = diag.missing = 0;
    next_work_ms = tick_clock_ms();
    if (!arch_cpu_has_tsc()) return OS32_ERR_NOSYS;
    const struct pit_setup *ps = pit_get_setup();
    /* Maximum-window L must remain below half the rational PIT period. */
    if (!ps->valid || !ps->clk_hz ||
        2ULL * 129u * ps->clk_hz >= (unsigned long long)ps->reload * 1000000u)
        return OS32_ERR_NOSYS;
    if (!registered) {
        if (idle_work_register(pc98_tsc_calibrate_work) != 0) {
            reject(REJECT_SLOTS); return OS32_ERR_FULL;
        }
        registered = 1;
    }
    state = TSC_CALIBRATING;
    return 0;
}
