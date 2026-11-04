/* Boot foreground image owner. Pixels borrow the existing planar BB. */
#include "platform_splash.h"
#include "gfx_osri.h"
#include "splash_raster_image.h"
#include "splash_raster_play.h"
#include "kmalloc.h"
#include "palette.h"
#include "kprintf.h"
#include "vfs.h"
#include "io.h"
#include "pegc.h"

#define SPLASH_IMAGE_TICK_US (TICK_MS * SPLASH_RASTER_US_PER_MS)
extern volatile u32 tick_count;

static void image_black(void)
{
    for (u32 i = 0; i < OSRI_COLORS; i++) palette_set(i, 0, 0, 0);
}

/* Only the two existing 400-line profiles enter the rational deadline math.
 * With <=3s, <=300 frames and these profiles, every product fits in u64. */
static int image_timing(const struct SplashRaster *s)
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

static u32 image_elapsed(u32 start, u32 budget)
{
    u32 elapsed = tick_count - start; /* unsigned subtraction handles wrap */
    return elapsed < budget ? elapsed : budget;
}

int platform_splash_try_image(const char *path, u32 *end_tick)
{
    u32 start = tick_count;
    i32 left = (i32)(*end_tick - start);
    u32 budget = left > 0 ? (u32)left : 0;
    if (budget > PLATFORM_SPLASH_TICKS) budget = PLATFORM_SPLASH_TICKS;
    struct gfx_kernel_fb fb;
    struct SplashRaster *s = pc98_splash_raster_state();
    struct OsriImage *image = 0;
    struct PlV2Plan *plan = 0;
    unsigned long long model = 0;
    u32 divisor = 1;
    int transferred = 0;
    OS32_Stat stat;
    const char *reason = "framebuffer";
    int rc = PLATFORM_SPLASH_FALLBACK;
    if (gfx_kernel_framebuffer(&fb)) goto done;
    reason = "missing";
    if (vfs_stat(path, &stat)) goto done;
    reason = "memory";
    image = kmalloc(sizeof(*image));
    if (!image) goto done;
    reason = "invalid";
    if (osri_load(path, &fb, image)) goto done;
    if (image_elapsed(start, budget) == budget) {
        rc = PLATFORM_SPLASH_DONE;
        goto done;
    }
    reason = "no sync";
    if (!_irq_enabled()) { rc = PLATFORM_SPLASH_NO_SYNC; goto done; }
    if (image->count) {
        if (image_timing(s)) { rc = PLATFORM_SPLASH_NO_SYNC; goto done; }
        reason = "memory";
        plan = kmalloc(sizeof(*plan));
        if (!plan) goto done;
        reason = "prepare";
        if (pl_v2_prepare(image, plan)) goto done;
        reason = "preview time";
        if ((budget - image_elapsed(start, budget)) * SPLASH_IMAGE_TICK_US <
            SPLASH_RASTER_PREVIEW_MS * SPLASH_RASTER_US_PER_MS) goto done;
    }
    image_black();
    gfx_add_dirty_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);
    gfx_present_nosync();
    transferred = 1;
    reason = "no sync";
    rc = PLATFORM_SPLASH_NO_SYNC;
    if (plan) {
        if (pl_preview(s)) goto done;
        /* Charge transfer and preview alignment using actual elapsed ticks. */
        u32 play_start = tick_count;
        u32 prefix = play_start - start;
        if (prefix > budget) prefix = budget;
        u32 remaining_us = (budget - prefix) * SPLASH_IMAGE_TICK_US;
        divisor = s->line_den;
        unsigned long long period = (unsigned long long)s->total_lines * s->line_num_us;
        unsigned long long consumed = 0;
        u32 frames = 0, good = 0;
        while (frames < PLATFORM_SPLASH_TICKS) {
            unsigned long long elapsed = (unsigned long long)image_elapsed(play_start, budget) *
                                         SPLASH_IMAGE_TICK_US * divisor;
            unsigned long long used = elapsed > consumed ? elapsed : consumed;
            if (used + period > (unsigned long long)remaining_us * divisor) break;
            int frame = pl_v2_frame(s, image, plan);
            if (frame == PL_FRAME_NO_SYNC) goto play_done;
            consumed += period; /* Both OK and DROP consume one period. */
            frames++;
            if (frame == PL_FRAME_OK) good++;
        }
        if (good || !frames) rc = PLATFORM_SPLASH_DONE;
play_done:
        model = (unsigned long long)prefix * SPLASH_IMAGE_TICK_US * divisor + consumed;
    } else {
        /* Fixed images keep the existing IRQ-open, clock-paced path. */
        if (pc98_splash_raster_frame(s, image)) goto done;
        while ((i32)(tick_count - *end_tick) < 0)
            if (platform_splash_wait_frame(*end_tick)) break;
        rc = PLATFORM_SPLASH_DONE;
    }
done:
    if (transferred) image_black();
    if (plan) kfree(plan);
    if (image) kfree(image);
    /* Round down by bounded multiplication comparison. Never restart the
     * three-second window, double-charge model+tick, or compensate PIT. */
    u32 now = tick_count;
    u32 ticks = now - start;
    if (ticks > budget) ticks = budget;
    unsigned long long elapsed = (unsigned long long)ticks * SPLASH_IMAGE_TICK_US * divisor;
    if (model > elapsed) elapsed = model;
    unsigned long long limit = (unsigned long long)budget * SPLASH_IMAGE_TICK_US * divisor;
    u32 rest = 0;
    while (rest < budget && elapsed + (unsigned long long)(rest + 1u) *
           SPLASH_IMAGE_TICK_US * divisor <= limit) rest++;
    *end_tick = now + rest;
    if (rc != PLATFORM_SPLASH_DONE)
        kprintf(0x07, "[splash] %s: builtin logo\n", reason);
    return rc;
}
