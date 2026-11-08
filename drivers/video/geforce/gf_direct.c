/* INDEX8 DIRECT CPU transfers. The backend retains scanout ownership; read
 * its existing value-only framebuffer loan on each call. BAR1 bounds match
 * geforce_platform.c's permanent boot display map, not the larger WC range.
 * Dword stores and BAR0 drain follow gf_present.c (GF6200_4C_DESIGN §4). */
#include "gfx_hal.h"
#include "memmap.h"
#include "geforce_module.h"
#include "hwdev/gf/accel.h"
#include "hwdev/gfinit_mode.h"

#define GF_DIRECT_PFB_AMOUNT 0x10020cU

/* G2 supplies the GPU adapters; G3 registers this immutable table. */
extern int geforce_fill_rect(int, int, int, int, u8);
extern int geforce_blit(int, int, int, int, int, int);
extern int geforce_fill_rop(int, int, int, int, u8, u8);

static int gf_front_in_cpu_map(const struct video_fb *fb)
{
    const struct geforce_platform *p = geforce_context->platform;
    if (!p || fb->phys < p->display_phys) return 0;
    u32 off = fb->phys - p->display_phys;
    return fb->bytes && off <= GF_MODE_VRAM_BYTES &&
        fb->bytes <= GF_MODE_VRAM_BYTES - off;
}

int gf_direct_ok(void)
{
    struct video_fb fb;
    u32 width, height;
    geforce_backend_framebuffer_info(&fb, &width, &height);
    return fb.format == GFX_FMT_PACKED8 && width == GF_MODE_WIDTH &&
        height == GF_MODE_HEIGHT && gf_front_in_cpu_map(&fb);
}

/* Submit/get validate screen rectangles before dispatch. Keep this boundary
 * checked too, since the table may also be used by kernel callers. */
static int direct_rect(struct video_fb *fb, int x, int y, int w, int h,
                       const void *data, u32 pitch)
{
    u32 width, height;
    if (!gf_direct_ok()) return OS32_ERR_NOSYS;
    geforce_backend_framebuffer_info(fb, &width, &height);
    if (!data || x < 0 || y < 0 || w <= 0 || h <= 0 ||
        (u32)x >= width || (u32)y >= height ||
        (u32)w > width - (u32)x || (u32)h > height - (u32)y ||
        pitch < (u32)w || (u32)(h - 1) > (U32_MAX - (u32)w) / pitch ||
        fb->pitch < width || !fb->pitch ||
        height > fb->bytes / fb->pitch) return OS32_ERR_INVAL;
    return 0;
}

int gf_direct_put(int x, int y, int w, int h, const u8 *src, u32 pitch, int key)
{
    struct video_fb fb;
    if (key < -1 || key > 255) return OS32_ERR_INVAL;
    int rc = direct_rect(&fb, x, y, w, h, src, pitch);
    if (rc) return rc;
    const struct geforce_platform *p = geforce_context->platform;
    volatile u8 *dst = P2V_IO(fb.phys);
    dst += (u32)y * fb.pitch + (u32)x;
    for (int row = 0; row < h; row++) {
        rc = p->point(p->opaque);
        if (rc) return rc;
        const u8 *s = src;
        volatile u8 *d = dst;
        int n = w;
        if (key >= 0) {
            for (; n; --n, ++s, ++d)
                if (*s != (u8)key) *d = *s;
        } else {
            while (n && ((uptr)d & 3U)) { *d++ = *s++; --n; }
            while (n >= 4) {
                u32 value;
                __builtin_memcpy(&value, s, sizeof(value));
                *(volatile u32 *)d = value;
                s += 4; d += 4; n -= 4;
            }
            while (n) { *d++ = *s++; --n; }
        }
        src += pitch;
        dst += fb.pitch;
    }
    /* Drain WC/posted BAR1 writes before the caller submits GPU commands. */
    volatile u8 *bar0 = P2V_IO(p->bar0_phys);
    (void)*(volatile u32 *)(bar0 + GF_DIRECT_PFB_AMOUNT);
    return 0;
}

int gf_direct_get(int x, int y, int w, int h, u8 *dst, u32 pitch)
{
    struct video_fb fb;
    int rc = direct_rect(&fb, x, y, w, h, dst, pitch);
    if (rc) return rc;
    const struct geforce_platform *p = geforce_context->platform;
    const volatile u8 *src = P2V_IO(fb.phys);
    src += (u32)y * fb.pitch + (u32)x;
    for (int row = 0; row < h; row++) {
        rc = p->point(p->opaque);
        if (rc) return rc;
        for (int col = 0; col < w; col++) dst[col] = src[col];
        src += fb.pitch;
        dst += pitch;
    }
    return 0;
}

const GfxDirectOps gf_direct_ops = {
    .fill_rect = geforce_fill_rect, .blit = geforce_blit,
    .fill_rop = geforce_fill_rop, .put = gf_direct_put, .get = gf_direct_get
};
