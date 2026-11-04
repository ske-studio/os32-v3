/* Module-only HAL adapter, GF_FEAT3_DESIGN section 10. Drawing targets the
 * borrowed scanout, while the existing CPU backbuffer/present path persists.
 * Clipping follows backend_cirrus.c:403-406,492-505 (G5); hardware operations
 * remain in B3/B4 (nv04_exa.c:118-157,170-200,255-258). */
#include "gfx_hal.h"
#include "../../../gfx/gfx_internal.h"
#include "hwdev/gf/accel.h"

/* GXcopy from X11 GX, nv04_exa.c:46-84; all INDEX8 planes are selected. */
#define GF_HOOK_GXCOPY 3U
#define GF_HOOK_FULL_MASK 0xffU

static struct gf3_ctx *hook_ctx;
static struct gf3_surface hook_scan;
static bool hook_active;

void gf_hook_ctx_bind(struct gf3_ctx *ctx)
{
    hook_ctx = ctx;
}

void gf_hook_scan_bind(bool active, const struct gf3_surface *scan)
{
    if (scan) hook_scan = *scan;
    hook_active = active && scan;
}

static int hook_deadline(u32 *dl)
{
    struct gf_io *io = &hook_ctx->ch->f->io;
    u32 now;
    if (io->timing.clock_us(io->opaque, &now)) {
        hook_ctx->last_rc = GF2_EIO;
        return OS32_ERR_IO;
    }
    *dl = now + GF3_TIMEOUT_US;
    return 0;
}

int geforce_fill_rect(int x, int y, int w, int h, u8 color)
{
    u32 dl;
    if (!hook_active || !hook_ctx) return OS32_ERR_NOSYS;
    if (!gfx_clip_screen(&x, &y, &w, &h, hook_scan.width, hook_scan.height))
        return 0;
    if (hook_deadline(&dl)) return OS32_ERR_IO;
    struct gf3_rect r = {(u32)x, (u32)y, (u32)w, (u32)h};
    int rc = gf3_fill(hook_ctx, &hook_scan, r, color,
                      GF_HOOK_GXCOPY, GF_HOOK_FULL_MASK, dl);
    hook_ctx->last_rc = rc;
    return rc ? OS32_ERR_IO : 0;
}

int geforce_blit(int dx, int dy, int sx, int sy, int w, int h)
{
    u32 dl;
    if (!hook_active || !hook_ctx) return OS32_ERR_NOSYS;
    if (dx < 0 || dy < 0 || sx < 0 || sy < 0) return OS32_ERR_INVAL;
    if ((u32)dx >= hook_scan.width || (u32)sx >= hook_scan.width ||
        (u32)dy >= hook_scan.height || (u32)sy >= hook_scan.height) return 0;
    if (w <= 0 || h <= 0) return 0;
    if ((u32)w > hook_scan.width - (u32)dx) w = hook_scan.width - (u32)dx;
    if ((u32)w > hook_scan.width - (u32)sx) w = hook_scan.width - (u32)sx;
    if ((u32)h > hook_scan.height - (u32)dy) h = hook_scan.height - (u32)dy;
    if ((u32)h > hook_scan.height - (u32)sy) h = hook_scan.height - (u32)sy;
    if (hook_deadline(&dl)) return OS32_ERR_IO;
    struct gf3_rect sr = {(u32)sx, (u32)sy, (u32)w, (u32)h};
    int rc = gf3_copy(hook_ctx, &hook_scan, &hook_scan, sr, (u32)dx, (u32)dy,
                      GF_HOOK_GXCOPY, GF_HOOK_FULL_MASK, dl);
    hook_ctx->last_rc = rc;
    return rc ? OS32_ERR_IO : 0;
}
