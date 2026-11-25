#ifndef PC98_SPLASH_RASTER_RUN_H
#define PC98_SPLASH_RASTER_RUN_H
#include "config.h"
#include "splash_raster_image.h"
#include "splash_raster_play.h"

/* 描画の時刻は 100Hz のジフィで数える。 */
#define PL_RUN_TICK_US (TICK_MS * SPLASH_RASTER_US_PER_MS)
/* All rational times are microseconds * divisor. Rebase at every extension;
 * good is session-wide. No 64-bit division or adjustment of the PIT clock. */
struct PlRun {
    u32 start, budget_us, divisor;
    unsigned long long period, consumed;
    u32 frames, good;
};
int pl_run_timing(const struct SplashRaster *s);
void pl_run_init(struct PlRun *r, const struct SplashRaster *s, u32 start, u32 budget_us);
unsigned long long pl_run_used(const struct PlRun *r, u32 now);
u32 pl_run_remaining(const struct PlRun *r, u32 now, u32 unit_us, u32 cap);
void pl_run_extend(struct PlRun *r, u32 now, u32 extension_us);
int pl_run_more(const struct PlRun *r, u32 now);
void pl_run_charge(struct PlRun *r, int frame);
#endif
