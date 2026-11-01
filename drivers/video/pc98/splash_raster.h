#ifndef PC98_SPLASH_RASTER_H
#define PC98_SPLASH_RASTER_H

#include "types.h"
#include "os32_kapi_shared.h"

/* Subsystem timing, from pegc.h's 400-line SYNC and nominal horizontal Hz.
 * SYNC: AL=P7|((P8&3)<<8), VS=(P3>>5)|((P4&3)<<3), VFP=P6, VBP=P8>>2.
 * NEC Technical Databook Hardware (1993), p.351 confirms the 24k profile. */
#define SPLASH_RASTER_LINE_NUM_US 100000u
#define SPLASH_RASTER_24K_DEN 2483u
#define SPLASH_RASTER_31K_DEN 3147u
#define SPLASH_RASTER_VISIBLE_LINES 400u
#define SPLASH_RASTER_MIN_GAP 2u
#define SPLASH_RASTER_PER_LINE 1u /* stage 0: single-color logo only */
#define SPLASH_RASTER_SYNC_TIMEOUT_US 100000u

enum { SPLASH_RASTER_OK = 0, SPLASH_RASTER_NO_SYNC = -1 };
enum { SPLASH_RASTER_24K = 0, SPLASH_RASTER_31K = 1 };

struct OsriImage;
struct SplashRaster {
    u32 frame, late, mode, dynamic_ok;
    int last_result;
    u32 line_num_us, line_den, active0_lines;
    u32 total_lines, vs_lines, vbp_lines, high_us;
    u32 checked, dropped, unapplied;
};

/* Owned here; reset at gfx_init/shutdown (connected by L0), lazy on first use.
 * begin/frame/table return OK or NO_SYNC and also set last_result.
 * A timing mismatch is a completed begin with dynamic_ok=0; fixed color works.
 * Stage 0 frame accepts count=0; OSRI differences are added in stage 2. */
struct SplashRaster *pc98_splash_raster_state(void);
void pc98_splash_raster_reset(void);
int pc98_splash_raster_begin(struct SplashRaster *s);
int pc98_splash_raster_frame(struct SplashRaster *s, const struct OsriImage *image);
int pc98_splash_raster_table(struct SplashRaster *s, const GFX_RasterPalTable *t);

#ifdef SPLASH_RASTER_DIAG
struct SplashRasterEventDiag {
    unsigned long long target_us, begin_us, end_us;
    u32 line, index, applied;
};
struct SplashRasterDiag {
    unsigned long long if0_max_us, if0_total_us;
    u32 count, capacity, overflow, clock_errors, late, dropped, unapplied;
    const struct SplashRasterEventDiag *events;
};
/* Last frame, borrowed until the next frame/reset; diagnostics build only. */
const struct SplashRasterDiag *pc98_splash_raster_diag(void);
#endif

#endif
