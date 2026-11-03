#ifndef PC98_SPLASH_RASTER_IMAGE_H
#define PC98_SPLASH_RASTER_IMAGE_H

#include "types.h"
#include "splash_raster.h"

struct OsriImage;
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
#endif
