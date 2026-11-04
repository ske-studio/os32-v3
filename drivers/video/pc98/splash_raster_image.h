#ifndef PC98_SPLASH_RASTER_IMAGE_H
#define PC98_SPLASH_RASTER_IMAGE_H

#include "types.h"
#include "splash_raster.h"

struct OsriImage;
#define SPLASH_RASTER_PREVIEW_MS 1000u
#define SPLASH_RASTER_US_PER_MS 1000u
/* VRAM is already committed. Keep IRQs open and count complete VBLANK
 * periods with the default PC98 palette before any per-line replay. */
int pl_preview(struct SplashRaster *state);
#define PL_MAX_OUTS 6u
struct PlRow { u8 first, count, port[PL_MAX_OUTS], value[PL_MAX_OUTS]; };
struct PlPlan { struct PlRow row[SPLASH_RASTER_VISIBLE_LINES]; };
STATIC_ASSERT(sizeof(struct PlRow) == 14, pl_row_size);

/* All rows are prepared before commit; row 0 has count zero. */
int pl_prepare(const struct OsriImage *image, u8 n, struct PlPlan *plan);
enum { PL_FRAME_OK = 0, PL_FRAME_DROP = 1, PL_FRAME_NO_SYNC = -1,
       PL_FRAME_DEADLINE = -2 };
int pl_frame(struct SplashRaster *state, const struct OsriImage *image,
             const struct PlPlan *plan, unsigned long long deadline_us);
#ifdef SPLASH_RASTER_OBS
#define PL_OBS_MAX_OUTS 64u
#define PL_OBS_OUT_LIMIT 47u
struct PlObsRow { u8 first, count, port[PL_OBS_MAX_OUTS], value[PL_OBS_MAX_OUTS]; };
struct PlObsPlan { struct PlObsRow row[SPLASH_RASTER_VISIBLE_LINES];
                   u32 max_outs, total_outs; };
STATIC_ASSERT(sizeof(struct PlObsRow) == 130, pl_obs_row_size);
STATIC_ASSERT(sizeof(struct PlObsPlan) == 52008, pl_obs_plan_size);
int pl_obs_prepare(const struct OsriImage *image, struct PlObsPlan *plan);
int pl_obs_frame(struct SplashRaster *state, const struct OsriImage *image,
                 const struct PlObsPlan *plan);
#endif
#endif
