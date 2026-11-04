#ifndef PC98_SPLASH_RASTER_PLAY_H
#define PC98_SPLASH_RASTER_PLAY_H

#include "gfx_osri.h"
#include "splash_raster.h"

struct PlV2Row {
    u8 first, count;
    u8 port[OSRI_DEFAULT_OUT_BUDGET], value[OSRI_DEFAULT_OUT_BUDGET];
};
struct PlV2Plan { struct PlV2Row row[GFX_HEIGHT]; u32 max_outs, total_outs; };
STATIC_ASSERT(sizeof(struct PlV2Row) == 64u, pl_v2_row_size);
STATIC_ASSERT(sizeof(struct PlV2Plan) == 25608u, pl_v2_plan_size);
enum { PL_FRAME_OK = 0, PL_FRAME_DROP = 1, PL_FRAME_NO_SYNC = -1 };
/* Borrowed image/plan; the owner allocates both on the heap. */
int pl_v2_prepare(const struct OsriImage *image, struct PlV2Plan *plan);
int pl_v2_frame(struct SplashRaster *state, const struct OsriImage *image,
                const struct PlV2Plan *plan);
#endif
