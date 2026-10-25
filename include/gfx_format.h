/* Pixel format properties for kernel graphics (not part of the SDK ABI). */
#ifndef GFX_FORMAT_H
#define GFX_FORMAT_H

/* Correspond to the SDK's GFX_FMT_PLANAR4 / GFX_FMT_PACKED8 format IDs. */
#define GFX_FMT_PLANAR4_BPP 4
#define GFX_FMT_PACKED8_BPP 8

/* Indexed formats only: bpp is an index width, not a DAC component width.
 * Keep the int type of the palette bounds used by the backends. */
#define GFX_PAL_ENTRIES(bpp) (1 << (bpp))

#endif /* GFX_FORMAT_H */
