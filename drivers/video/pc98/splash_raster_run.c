/* Rational deadline control shared by boot and the synchronous viewer. */
#include "config.h"
#include "splash_raster_run.h"
#include "pegc.h"
#include "io.h"

int pl_run_timing(const struct SplashRaster *s)
{
    static const u8 sync24[] = PEGC_GDC_MSYNC_400;
    static const u8 sync31[] = PEGC_GDC_MSYNC_400_31K;
    if (!s || !s->checked || !s->dynamic_ok || !_irq_enabled() ||
        s->mode > SPLASH_RASTER_31K ||
        s->line_num_us != SPLASH_RASTER_LINE_NUM_US) return -1;
    const u8 *sync = s->mode == SPLASH_RASTER_31K ? sync31 : sync24;
    u32 den = s->mode == SPLASH_RASTER_31K ? SPLASH_RASTER_31K_DEN : SPLASH_RASTER_24K_DEN;
    u32 vs = (sync[2] >> 5) | ((sync[3] & 3) << 3);
    u32 total = sync[6] + ((sync[7] & 3) << BITS_PER_BYTE) + sync[5] + vs + (sync[7] >> 2);
    return s->line_den == den && s->total_lines == total ? 0 : -1;
}

void pl_run_init(struct PlRun *r, const struct SplashRaster *s, u32 start, u32 budget_us)
{
    *r = (struct PlRun){ .start = start, .budget_us = budget_us,
        .divisor = s ? s->line_den : 1,
        .period = s ? (unsigned long long)s->total_lines * s->line_num_us : 0 };
}

unsigned long long pl_run_used(const struct PlRun *r, u32 now)
{
    unsigned long long elapsed = (unsigned long long)(now - r->start) *
                                  PL_RUN_TICK_US * r->divisor;
    return elapsed > r->consumed ? elapsed : r->consumed;
}

u32 pl_run_remaining(const struct PlRun *r, u32 now, u32 unit_us, u32 cap)
{
    unsigned long long used = pl_run_used(r, now);
    unsigned long long limit = (unsigned long long)r->budget_us * r->divisor;
    u32 lo = 0, hi = cap;
    /* Round down; logarithmic bounded search also supports the 120s viewer. */
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2 + 1;
        if (used + (unsigned long long)mid * unit_us * r->divisor <= limit) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

void pl_run_extend(struct PlRun *r, u32 now, u32 extension_us)
{
    u32 remaining = pl_run_remaining(r, now, 1, r->budget_us);
    r->budget_us = remaining > extension_us ? remaining : extension_us;
    r->start = now;
    r->consumed = 0;
    r->frames = 0;
}

int pl_run_more(const struct PlRun *r, u32 now)
{
    return pl_run_used(r, now) + r->period <=
           (unsigned long long)r->budget_us * r->divisor;
}

void pl_run_charge(struct PlRun *r, int frame)
{
    r->consumed += r->period; /* Both OK and DROP consume one period. */
    r->frames++;
    if (frame == PL_FRAME_OK) r->good++;
}
