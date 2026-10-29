/* Palette load: MIT nouveau c03c1a1 dispnv04/hw.c:795-810,
 * dispnv04/crtc.c:778. Partial updates select their own starting index. */
#include "../internal.h"
#include "../../gfinit_mode.h"

int gf_display_palette(struct gf_display_context *ctx, u32 first, u32 count,
                       const u8 *rgb)
{
    if (!ctx || !rgb || first >= VIDEO_PALETTE_ENTRIES || !count ||
        count > VIDEO_PALETTE_ENTRIES - first)
        return OS32_ERR_INVAL;

    u32 start = first * VIDEO_RGB_COMPONENTS;
    u32 bytes = count * VIDEO_RGB_COMPONENTS;
    for (u32 i = 0; i < bytes; i++)
        ctx->palette[start + i] = rgb[i];

    struct gf_io *io = &ctx->io;
    if (io->write(io->opaque, GF_MODE_REGISTER_BAR, GF_MODE_DAC_MASK, 1,
                  GF_MODE_BYTE_MASK) ||
        io->write(io->opaque, GF_MODE_REGISTER_BAR, GF_MODE_DAC_INDEX, 1,
                  first)) {
        ctx->last_rc = -1;
        return -1;
    }
    for (u32 i = 0; i < bytes; i++) {
        if (io->write(io->opaque, GF_MODE_REGISTER_BAR, GF_MODE_DAC_DATA, 1,
                      ctx->palette[start + i])) {
            ctx->last_rc = -1;
            return -1;
        }
    }
    return 0;
}
