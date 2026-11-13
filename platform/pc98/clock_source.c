#include "clock_source.h"
#include "kernel/irq/tick_budget.h"
#include "kernel_tick.h"
#include "pit.h"
#include "pit_time.h"
#include "platform_timer.h"
#include "os32_kapi_shared.h"
#include "../../kernel/time/ktime.h"
#include "tick_clock.h"

STATIC_ASSERT(OS32_TICK_HZ == PIT_HZ, logical_tick_hz_match);

int platform_clock_init(u32 tick_hz)
{
    int rc = pit_init((unsigned int)tick_hz);
    const struct pit_setup *ps = pit_get_setup();
    if (ps->valid) {
        (void)tick_clock_config(ps->reload, ps->clk_hz);
        tick_budget_config();
    }
    return rc;
}
void platform_clock_info(struct clock_info *out)
{
    const struct pit_setup *ps = pit_get_setup();
    out->tick_hz = ps->hz;
    out->counter_hz = ps->clk_hz;
    out->ready = ps->valid;
}
int platform_clock_read_us(u32 *lo, u32 *hi)
{
    const struct pit_setup *ps = pit_get_setup();
    struct time_snapshot snap;
    unsigned int out_lo = 0, out_hi = 0;
    int attempt, rc = OS32_ERR_AGAIN;
    /* PIT を積んでいない / mode 2 でない機械では補間の意味が無い。 */
    if (!ps->valid || ps->mode != (u8)PIT_MODE_TIMER0 || ps->reload == 0) {
        return OS32_ERR_NOSYS;
    }

    for (attempt = 0; attempt < TIME_RETRY_MAX; attempt++) {
        unsigned int count, t;
        struct platform_timer_sample hw;
        int p1, p2;

        platform_timer_sample(&hw);
        p1 = hw.p1;
        count = hw.count;
        p2 = hw.p2;

        /* 4. tick (IF=0 の中なので 1〜3 の間は不変) */
        t = (unsigned int)tick_count;

        /* 5. 判定 */
        if (time_decide(p1, p2, t, count, &snap) == TIME_DECIDE_OK) {
            time_us_from(snap.tick, snap.count, ps->reload, ps->period_us,
                         &out_lo, &out_hi);
            rc = 0;
            break;
        }
    }
    if (rc == 0) { *lo = out_lo; *hi = out_hi; }
    return rc;
}
