/* The palette shadow exists from I1; hardware palette operations belong to C13. */
#include "gfx2_int.h"
#include "palette.h"

u32 gfx2_pal[256];

void gfx2_pal_reset16(void)
{
    const PaletteEntry *p = palette_get_all();
    for (u32 i = 0; i < 256; i++) gfx2_pal[i] = 0;
    for (u32 i = 0; i < 16; i++)
        gfx2_pal[i] = ((u32)p[i].r * 17 << 16) |
                      ((u32)p[i].g * 17 << 8) | (u32)p[i].b * 17;
}

void gfx2_pal_set(u32 first, u32 count, const u32 *rgb)
{
    if (first >= 256 || count > 256 - first) return;
    for (u32 i = 0; i < count; i++) gfx2_pal[first + i] = rgb[i] & 0xffffffUL;
}

void gfx2_pal_set_v1(int first, int count, const u8 *rgb)
{
    if (first < 0 || count < 0 || first >= 256 || count > 256 - first) return;
    for (int i = 0; i < count; i++)
        gfx2_pal[first + i] = ((u32)(rgb[3*i] & 15) * 17 << 16) |
                              ((u32)(rgb[3*i+1] & 15) * 17 << 8) |
                               (u32)(rgb[3*i+2] & 15) * 17;
}
