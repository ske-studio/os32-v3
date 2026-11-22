#include "backend_of_fb.h"
#include "kstring.h"
#include "gfx/palette.h"

static struct of_fb_desc fb;
static GfxMode mode;
static int live;
static u8 scan_index[256];
static int remapped;

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
    for (int i = 0; i < 256; i++) scan_index[i] = i;
    remapped = 0;
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
        const u8 *source = src + row * pitch + first;
        volatile u32 *dest = (volatile u32 *)(fb.display + row * fb.pitch + first);
        u32 words = (end - first) / sizeof(u32);
        /* Include adjacent shadow pixels at the two edge words. Native loads
         * and stores preserve the byte order on both endian CPUs. bind ensures
         * display and its pitch are word aligned; source rows need not be. */
        if (remapped) {
            for (u32 i = 0; i < words; i++) {
                u32 word = 0;
                for (unsigned j = 0; j < sizeof(word); j++) {
                    unsigned shift = __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ ? 3 - j : j;
                    word |= (u32)scan_index[*source++] << (8 * shift);
                }
                dest[i] = word;
            }
        } else if (!((u32)source & 3)) {
            const u32 *aligned = (const u32 *)source;
            for (u32 i = 0; i < words; i++) dest[i] = aligned[i];
        } else {
            for (u32 i = 0; i < words; i++) {
                u32 word;
                kmemcpy(&word, source + i * sizeof(word), sizeof(word));
                dest[i] = word;
            }
        }
    }
    if (bottom > y) gfx_counters.present_bytes += (end - first) * ((u32)bottom - (u32)y);
    __sync_synchronize();
    return 0;
}
static int query(GFX_ScreenInfo *out)
{
    if (!out) return OS32_ERR_INVAL;
    if (!live) {
        *out = (GFX_ScreenInfo){0};
        return OS32_ERR_STALE;
    }
    *out = (GFX_ScreenInfo){.width = mode.width, .height = mode.height,
                            .lease_first = fb.clut ? 16 : 0, .lease_count = fb.clut ? 240 : 0,
                            .bpp = 8, .format = GFX_FMT_PACKED8};
    return 0;
}
int of_fb_present(int x, int y, int w, int h)
{
    if (!live) return OS32_ERR_STALE;
    if (w <= 0 || h <= 0) return OS32_ERR_INVAL;
    int rc = of_fb_copy(fb.shadow, fb.shadow_pitch, fb.shadow_bytes, x, y, w, h);
    if (!rc) gfx_counters.commits++;
    return rc;
}
static void present(int x, int y, int w, int h)
{
    (void)of_fb_present(x, y, w, h);
}
static int set_mode(u32 id) { return !live ? OS32_ERR_STALE : id == 1 ? 0 : OS32_ERR_NOSYS; }
static void noop(void) { }
int of_fb_palette(int first, int count, const u8 *rgb)
{
    if (!live) return OS32_ERR_STALE;
    if (!fb.clut) return OS32_ERR_NOSYS;
    if (!rgb || first < 0 || first >= 256 ||
        count <= 0 || count > 256 - first) return OS32_ERR_INVAL;
    /* Changing the mapping repaints the shadow, so use the same ownership
     * and generation check as a public present. */
    if (!fb.palette_allowed || !fb.palette_allowed()) return OS32_ERR_BUSY;
    int changed = 0;
    for (int i = first; i < first + count; i++, rgb += 3) {
        int best = 0, distance = 3 * 255 * 255 + 1;
        for (int j = 0; j < 256; j++) {
            int r = (rgb[0] & 15) * 17 - fb.clut[j][0];
            int g = (rgb[1] & 15) * 17 - fb.clut[j][1];
            int b = (rgb[2] & 15) * 17 - fb.clut[j][2];
            int d = r*r + g*g + b*b;
            if (d < distance) { best = j; distance = d; }
        }
        palette_shadow_set(i, rgb[0], rgb[1], rgb[2]);
        if (scan_index[i] == best) continue;
        remapped += (best != i) - (scan_index[i] != i);
        scan_index[i] = best;
        changed = 1;
    }
    return changed ? of_fb_present(0, 0, fb.width, fb.height) : 0;
}
static void palette(int first, int count, const u8 *rgb)
{
    (void)of_fb_palette(first, count, rgb);
}
GfxBackend gfx_backend_of_fb = {
    .name = "of_fb", .probe = of_fb_ready, .prepare = noop, .init = noop,
    .query = query, .shutdown = of_fb_unbind, .enter = noop, .leave = noop,
    .present_rect = present, .set_palette = palette, .set_mode = set_mode,
    .bb_format = GFX_BB_PACKED8, .modes = &mode, .v2 = &of_fb_v2,
};
