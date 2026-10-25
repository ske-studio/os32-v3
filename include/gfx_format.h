/* Pixel format properties for kernel graphics (not part of the SDK ABI). */
#ifndef GFX_FORMAT_H
#define GFX_FORMAT_H

#include "types.h"
#include "../sdk/include/os32/os32_kapi_shared.h"

/* Correspond to the SDK's GFX_FMT_PLANAR4 / GFX_FMT_PACKED8 format IDs. */
#define GFX_FMT_PLANAR4_BPP 4
#define GFX_FMT_PACKED8_BPP 8

/* Indexed formats only: bpp is an index width, not a DAC component width.
 * Keep the int type of the palette bounds used by the backends. */
#define GFX_PAL_ENTRIES(bpp) (1 << (bpp))

typedef struct GfxFormatDesc {
    u8 bpp, indexed, planes, bits_per_plane;
    u16 palette_count;
} GfxFormatDesc;

static inline int gfx_format_desc(u8 fmt, GfxFormatDesc *out)
{
    switch (fmt) {
    case GFX_FMT_PLANAR4:
        *out = (GfxFormatDesc){GFX_FMT_PLANAR4_BPP, 1,
            GFX_FMT_PLANAR4_BPP, 1, GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP)};
        return 1;
    case GFX_FMT_PACKED8:
        *out = (GfxFormatDesc){GFX_FMT_PACKED8_BPP, 1,
            1, GFX_FMT_PACKED8_BPP, GFX_PAL_ENTRIES(GFX_FMT_PACKED8_BPP)};
        return 1;
    default:
        return 0;
    }
}

/* Minimum bytes per row of ONE plane, without overflowing width + 7. */
static inline int gfx_format_row(u8 fmt, u32 width, u32 *row_bytes)
{
    if (!width) return 0;
    switch (fmt) {
    case GFX_FMT_PLANAR4:
        *row_bytes = width / BITS_PER_BYTE + (width % BITS_PER_BYTE != 0);
        return 1;
    case GFX_FMT_PACKED8:
        *row_bytes = width;
        return 1;
    default:
        return 0;
    }
}

#endif /* GFX_FORMAT_H */
