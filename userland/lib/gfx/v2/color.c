/* HAL_V2_DESIGN U1: ARGB to the destination's raw pixel value. */
#include "os32gfx2u.h"
#include "gfx/v2/gfx2_fmt.h"

int gfx2_color_pack(u32 fmt, u32 argb, const u32 *pal256, u32 *out)
{
    u32 best = 0, best_distance = ~0u;
    int r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;

    if (fmt != GFX2_FMT_INDEX8) return gfx2_from_argb(fmt, argb, out);
    if (!out || !pal256) return OS32_ERR_INVAL;
    for (u32 i = 0; i < 256; i++) {
        int dr = r - (int)((pal256[i] >> 16) & 255);
        int dg = g - (int)((pal256[i] >> 8) & 255);
        int db = b - (int)(pal256[i] & 255);
        u32 distance = (u32)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    *out = best;
    return 0;
}
