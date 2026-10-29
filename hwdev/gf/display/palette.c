/* Palette load: MIT nouveau c03c1a1 dispnv04/hw.c:795-810,
 * dispnv04/crtc.c:778. Partial updates select their own starting index. */
#include "access.h"
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

    /* Shared access record keeps the failing target and bus rc. */
    if (gf_display_write(ctx, GF_MODE_DAC_MASK, 1, GF_MODE_BYTE_MASK) ||
        gf_display_write(ctx, GF_MODE_DAC_INDEX, 1, first))
        return -1;
    for (u32 i = 0; i < bytes; i++)
        if (gf_display_write(ctx, GF_MODE_DAC_DATA, 1, ctx->palette[start + i]))
            return -1;
    return 0;
}
