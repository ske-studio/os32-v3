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
/* PlRow is retained only by the independent DIAG probe. */
#endif
