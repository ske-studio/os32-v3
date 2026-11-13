#include "backend_of_fb.h"
#include "kstring.h"

static struct of_fb_desc fb;
static GfxMode mode;
static int live;

int of_fb_ready(void) { return live; }
void of_fb_unbind(void)
{
    live = 0;
    fb = (struct of_fb_desc){0};
    gfx_backend_of_fb.bb_base = 0;
    gfx_backend_of_fb.bb_size = 0;
    gfx_backend_of_fb.mode_count = 0;
    gfx_backend_of_fb.selected_mode = 0;
}
int of_fb_bind(const struct of_fb_desc *d)
{
    of_fb_unbind();
    /* Complete words only: an odd final word cannot preserve VRAM padding
     * without reading VRAM. Reject that layout rather than touch padding. */
    if (!d || !d->display || !d->shadow || d->display == d->shadow ||
        ((u32)d->display & 3) || !d->width || d->width > 65535 ||
        (d->width & 3) || d->protected_rows > d->height || !d->height || d->height > 65535 ||
        (d->pitch & 3) || d->pitch < d->width ||
        d->shadow_pitch < d->width || d->height > d->bytes / d->pitch ||
        d->height > d->shadow_bytes / d->shadow_pitch) return OS32_ERR_INVAL;
    fb = *d;
    mode = (GfxMode){.width = d->width, .height = d->height,
                    .format = GFX_FMT_PACKED8, .pitch = d->shadow_pitch};
    gfx_backend_of_fb.bb_base = d->shadow;
    gfx_backend_of_fb.bb_pitch = d->shadow_pitch;
    gfx_backend_of_fb.bb_size = d->shadow_bytes;
    gfx_backend_of_fb.mode_count = 1;
    live = 1;
    return 0;
}
int of_fb_copy(const u8 *src, u32 pitch, u32 bytes, int x, int y, int w, int h)
{
    if (!live) return OS32_ERR_STALE;
    if (!src || src == fb.display || pitch < fb.width ||
        fb.height > bytes / pitch) return OS32_ERR_INVAL;
    if (w <= 0 || h <= 0 || x >= (int)fb.width || y >= (int)fb.height) return 0;
    /* Clip without signed overflow for arbitrary v1 rectangles. */
    long long right = (long long)x + w, bottom = (long long)y + h;
    if (right <= 0 || bottom <= 0) return 0;
    if (x < 0) x = 0;
    if (y < (int)fb.protected_rows) y = fb.protected_rows;
    if (right > fb.width) right = fb.width;
    if (bottom > fb.height) bottom = fb.height;
    u32 first = (u32)x & ~3u, end = ((u32)right + 3) & ~3u;
    for (u32 row = y; row < (u32)bottom; row++) {
        for (u32 col = first; col < end; col += sizeof(u32)) {
            /* Include adjacent shadow pixels at the two edge words; byte
             * representation stays native on both big and little endian CPUs. */
            u32 word;
            kmemcpy(&word, src + row * pitch + col, sizeof(word));
            *(volatile u32 *)(fb.display + row * fb.pitch + col) = word;
        }
    }
    __sync_synchronize();
    return 0;
}
static int query(GFX_ScreenInfo *out)
{
    if (!live) return OS32_ERR_STALE;
    if (!out) return OS32_ERR_INVAL;
    *out = (GFX_ScreenInfo){.width = mode.width, .height = mode.height,
                            .bpp = 8, .format = GFX_FMT_PACKED8};
    return 0;
}
static void present(int x, int y, int w, int h)
{
    (void)of_fb_copy(fb.shadow, fb.shadow_pitch, fb.shadow_bytes, x, y, w, h);
}
static int set_mode(u32 id) { return !live ? OS32_ERR_STALE : id == 1 ? 0 : OS32_ERR_NOSYS; }
static void noop(void) { }
static void palette(int first, int count, const u8 *rgb)
{ (void)first; (void)count; (void)rgb; }
GfxBackend gfx_backend_of_fb = {
    .name = "of_fb", .probe = of_fb_ready, .prepare = noop, .init = noop,
    .query = query, .shutdown = of_fb_unbind, .enter = noop, .leave = noop,
    .present_rect = present, .set_palette = palette, .set_mode = set_mode,
    .bb_format = GFX_BB_PACKED8, .modes = &mode, .v2 = &of_fb_v2,
};
