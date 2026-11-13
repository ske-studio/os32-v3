#include "clock_source.h"
#include "kernel/irq/tick_budget.h"
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
        unsigned int count;
        u32 raw_lo, raw_hi;
        unsigned long long raw;
        struct platform_timer_sample hw;
        int p1, p2;

        platform_timer_sample(&hw);
        p1 = hw.p1;
        count = hw.count;
        p2 = hw.p2;

        /* 4. 物理 IRQ 数 (IF=0 の中なので sample の間は不変) */
        tick_clock_raw(&raw_lo, &raw_hi);
        raw = ((unsigned long long)raw_hi << BITS_PER_U32) | raw_lo;

        /* 5. 判定 */
        if (time_decide(p1, p2, 0, count, &snap) == TIME_DECIDE_OK) {
            /* t=0 の判定結果は pending の 0/1。下位語の周回も繰り上げる。 */
            raw += snap.tick;
            time_us_from64((unsigned int)raw, (unsigned int)(raw >> BITS_PER_U32),
                           snap.count, ps->reload, (unsigned int)ps->clk_hz,
                           &out_lo, &out_hi);
            rc = 0;
            break;
        }
    }
    if (rc == 0) { *lo = out_lo; *hi = out_hi; }
    return rc;
}
