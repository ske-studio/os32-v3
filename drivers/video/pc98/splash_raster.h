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
#define SPLASH_RASTER_MEASURE_PERIODS 8u
#define SPLASH_RASTER_PERIOD_PERCENT 100u
#define SPLASH_RASTER_PERIOD_TOLERANCE 1u
/* B is a reference, not a measured/adopted bit6 width. 24k: NEC p.351;
 * 31k: H * (HFP+HS+HBP)/100 from the existing 400-line SYNC. */
#define SPLASH_RASTER_24K_BLANK_NUM_US 988u
#define SPLASH_RASTER_24K_BLANK_DEN 100u
#define SPLASH_RASTER_31K_BLANK_NUM_US 20000u
#define SPLASH_RASTER_CLOCK_QUANTUM_US 1u

/* Image-only adopted OUT budget; 6 is for explicit diagnostic trials. */
#define SPLASH_RASTER_IMAGE_OUTS 5u
#define SPLASH_RASTER_IMAGE_DIAG_OUTS 6u

/* Measured bounds are required before adopting an image path. Q includes
 * sampling and row preparation, D is successful IN return to column start,
 * W is the entire prepared column, and B is bit6 high width. All ranges use
 * 1/den us. Require Q+D+W+E < B_min and Q < min(B_min,H_min-B_max).
 * Phase: F is a new bit5 high-to-low transition; the first subsequent new
 * bit6 low-to-high edge is k=1 and E(y) is k=active0+y. One IN samples both.
 * The existing public table's IF and spacing budgets remain unchanged. */
/* Internal LC contract. Signed numerators preserve negative margins. Bounds
 * in a budget share den (units = 1/den us), never rounded to whole lines.
 * Q includes IN/loop/compare/count; branch is successful IN return -> OUT,
 * so it must not include Q again. C is clock cost, W is all four OUTs.
 * Q+branch+m*W+E < B_min; Q < min(B_min,H_min-B_max).
 * These measured bounds do not certify arbitrary interrupt workloads. */
struct SplashRasterTimeRange { long long min, max; };
struct SplashRasterImageBudget {
    u32 den, outs;
    struct SplashRasterTimeRange q, d, w, h, b;
    long long error;
};
struct SplashRasterBudget {
    u32 den;
    struct SplashRasterTimeRange q, c, w, h, b;
    long long branch, error, retry;
};
struct SplashRasterEntryBudget {
    struct SplashRasterTimeRange s[2]; /* inward-rounded S_j and S_(j+1) */
    long long planned, retry, j_max;
    u32 usable; /* bit0/bit1: nonempty S, including >=1H open / <=2H shut */
};
/* J_max(y)=U_(j+1)-planned-retry for two windows; U_j-planned
 * for one window. Frame J_max=min(all entries/relays, F entry margin,
 * initial palette completion margin). Negative or unknown => unqualified.
 * J is cumulative IRQ/return delay from coarse-wait tail to entry, not the
 * longest single ISR. Failed post-save checks also count toward IF=0. */
enum SplashRasterWave {
    SPLASH_RASTER_WAVE_UNKNOWN,
    SPLASH_RASTER_WAVE_24K_BLANK, SPLASH_RASTER_WAVE_24K_VS,
    SPLASH_RASTER_WAVE_31K_BLANK, SPLASH_RASTER_WAVE_31K_VS
};

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
struct SplashRasterProbeCost {
    /* IF=0 batch means; upper_num_us/upper_samples retains sub-us precision.
     * upper_us is its ceiling for coarse waits only. IF=1 is kept separate. */
    unsigned long long total_us, upper_us;
    unsigned long long upper_num_us, open_total_us, open_max_us;
    u32 samples, upper_samples, batch;
};
struct SplashRasterProbeEntry {
    struct SplashRasterEntryBudget budget;
    unsigned long long wait_begin_us, planned_us, entered_us, end_us;
    struct SplashRasterTimeRange irq_us;
    long long initial_margin, capture_margin, frame_j_max;
    u32 line, rx_words, qualified;
};
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
