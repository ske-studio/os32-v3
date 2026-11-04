/* Default-palette preview retained for boot and observation owners. */
#include "splash_raster_image.h"
#include "palette.h"
#include "pc98.h"
#include "io.h"

#define PL_PREVIEW_POLL_LIMIT 1000000u

static u32 preview_rounds(const struct SplashRaster *s)
{
    unsigned long long period = (unsigned long long)s->total_lines * s->line_num_us;
    unsigned long long duration = (unsigned long long)SPLASH_RASTER_PREVIEW_MS *
                                 SPLASH_RASTER_US_PER_MS * s->line_den;
    u32 rounds = 0;
    while ((unsigned long long)rounds * period < duration) rounds++;
    return rounds;
}

static int preview_vsync(int high)
{
    for (u32 i = 0; i < PL_PREVIEW_POLL_LIMIT; i++)
        if (!!(inp(GDC_GFX_STAT) & GDC_STAT_VSYNC) == high) return 0;
    return -1;
}

static int preview(struct SplashRaster *s)
{
    if (!s || !s->checked || !s->dynamic_ok || !_irq_enabled() ||
        !s->line_num_us || !s->line_den ||
        s->total_lines <= SPLASH_RASTER_VISIBLE_LINES) return -1;
    palette_init();
    /* Align once, then count full periods: a partially elapsed first
     * VBLANK must not shorten the requested one-second preview. */
    if (preview_vsync(0) || preview_vsync(1)) return -1;
    u32 rounds = preview_rounds(s);
    for (u32 round = 0; round < rounds; round++)
        if (preview_vsync(0) || preview_vsync(1)) return -1;
    return 0;
}

int pl_preview(struct SplashRaster *s)
{
    return preview(s);
}
