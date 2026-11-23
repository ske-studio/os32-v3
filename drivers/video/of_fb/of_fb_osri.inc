#include "gfx/gfx_osri.h"
#include "gfx/gfx_osri_view.h"
#include "gfx/v2/gfx2_int.h"
#include "kernel/idle_work.h"
#include "kernel_tick.h"
#include "kmalloc.h"

static u8 nearest_osri(u16 rgb)
{
    return nearest_clut((rgb >> 8) * 17, ((rgb >> 4) & 15) * 17, (rgb & 15) * 17);
}

static void osri_convert(const struct OsriImage *image, const struct gfx_kernel_fb *src, u8 *out)
{
    u8 colors[16];
    unsigned event = 0;
    for (unsigned i = 0; i < 16; ++i) colors[i] = nearest_osri(image->pal[i]);
    for (unsigned y = 0; y < GFX_HEIGHT; ++y) {
        while (event < image->count && image->event[event].line == y) {
            const struct OsriEvent *e = &image->event[event++];
            colors[e->index] = nearest_osri(e->rgb444);
        }
        for (unsigned x = 0; x < GFX_WIDTH; ++x) {
            unsigned off = y * GFX_BPL + x / 8, index = 0;
            for (unsigned p = 0; p < 4; ++p)
                if (src->planes[p][off] & (128u >> (x % 8))) index |= 1u << p;
            *out++ = colors[index];
        }
    }
}

static int osri_transfer(unsigned long long bound, const u32 *pixels, const struct OsriViewRun *timed)
{
    if (of_fb_osri_generation() != bound) return OS32_ERR_IO;
    int rc = v1_bridge_begin(GFX2_ROLE_CLIENT, GFX2_CPU_R);
    if (rc) return rc;
    rc = v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_W);
    if (!rc) {
        if (timed && (unsigned long long)(tick_count - timed->start) * (1000000 / OS32_TICK_HZ) >=
                     (unsigned long long)timed->duration_ms * 1000) {
            rc = OS32_ERR_AGAIN;
            goto end_display;
        }
        osri_visible = pixels != 0;
        for (unsigned y = 0; y < GFX_HEIGHT; ++y) {
            unsigned row = fb.protected_rows + y;
            if (pixels) {
                volatile u32 *dst = (volatile u32 *)(fb.display + row * fb.pitch);
                for (unsigned x = 0; x < GFX_WIDTH / 4; ++x) dst[x] = *pixels++;
                gfx_counters.present_bytes += GFX_WIDTH;
            } else {
                const u8 *src = external_scanout ? 0 : fb.shadow;
                transfer(src, fb.shadow_pitch, 0, (row + origin) % fb.height, GFX_WIDTH, 1);
            }
        }
        __sync_synchronize();
end_display:
        v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_W, 0);
    }
    if (bound != of_fb_osri_generation()) return OS32_ERR_IO;
    v1_bridge_end(GFX2_ROLE_CLIENT, GFX2_CPU_R, 0);
    return rc;
}

/* All tick differences are modular. Accumulate them during a long fixed image
 * or repeated extensions rather than treating wraparound as a new deadline. */
static unsigned long long elapsed_us(u32 *previous, unsigned long long elapsed)
{
    u32 now = tick_count;
    elapsed += (unsigned long long)(now - *previous) * (1000000 / OS32_TICK_HZ);
    *previous = now;
    return elapsed;
}

static int osri_wait(const struct OsriImage *image, const struct OsriViewRun *ctl,
                     unsigned long long bound, unsigned long long deadline,
                     unsigned long long elapsed, u32 previous)
{
    elapsed = elapsed_us(&previous, elapsed);
    if (image->count && elapsed >= deadline) return OS32_ERR_AGAIN;
    for (;;) {
        if (bound != of_fb_osri_generation()) return OS32_ERR_IO;
        if (gfx_osri_poll_abort(ctl->caller)) return GFX_OSRI_SHOW_CANCELLED;
        elapsed = elapsed_us(&previous, elapsed);
        for (int i = 0; i < 32; ++i) {
            int key = pm_osri_key();
            if (key < 0) break;
            if (!key) return GFX_OSRI_SHOW_CANCELLED;
            if (key == 1 && image->count) {
                unsigned long long extension = elapsed + GFX_OSRI_SHOW_EXTEND_MS * 1000u;
                if (deadline < extension) deadline = extension;
            }
        }
        if (image->count && elapsed >= deadline) return GFX_OSRI_SHOW_DONE;
        idle_wait_step();
    }
}

static int of_osri_show(const struct OsriImage *image, struct OsriViewRun *ctl)
{
    unsigned long long bound;
    const struct gfx_kernel_fb *src = pm_osri_decoded(&bound);
    if (!src || !bound || bound != of_fb_osri_generation()) return OS32_ERR_IO;
    u8 *packed = kmalloc(GFX_WIDTH * GFX_HEIGHT);
    if (!packed) return OS32_ERR_NOSPC;
    osri_convert(image, src, packed);
    unsigned long long deadline = (unsigned long long)ctl->duration_ms * 1000;
    u32 previous = ctl->start;
    unsigned long long elapsed = elapsed_us(&previous, 0);
    int rc = OS32_ERR_AGAIN;
    if (!image->count || elapsed < deadline) {
        rc = osri_transfer(bound, (u32 *)packed, image->count ? ctl : 0);
        if (!rc) {
            rc = osri_wait(image, ctl, bound, deadline, elapsed, previous);
            /* A competing lease can be acquired while the image is visible.
             * Release our successful begins before waiting for that lease. */
            int restore;
            while ((restore = osri_transfer(bound, 0, 0)) == OS32_ERR_BUSY)
                idle_wait_step();
            if (restore) {
                if (bound == of_fb_osri_generation()) osri_visible = 0;
                rc = restore;
            }
        }
    }
    kfree(packed);
    return rc;
}
