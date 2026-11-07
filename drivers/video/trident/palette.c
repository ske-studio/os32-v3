/*
 * Copyright 1992-2003 by Alan Hourihane, North Wales, UK.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Alan Hourihane not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Alan Hourihane makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * ALAN HOURIHANE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ALAN HOURIHANE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Alan Hourihane, alanh@fairlite.demon.co.uk
 */
/*
 * Copyright (c) 1998-2001 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */
/* Source notices retained from trident_mode.hwb:3–48.
 * Copyright 1991-1999 by The XFree86 Project, Inc.
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * vgaHW.c license: xorg-server-21.1.13 COPYING:54-76
 * Copyright (C) 1994-2003 The XFree86 Project, Inc.  All Rights Reserved.
 * Copyright (C) Colin Harrison 2005-2008
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall not
 * be used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from the XFree86 Project.
 */
/* TridentLoadPalette/SetOverscan, trident_dac.c:1245-1282.
 * Default TrueColor values: xf86cmap.c:254-275,525-665,855-904,
 * gamma=1.0; OS32 F8 addendum 2026-11-07 defines unused RGB565 R/B as 0.
 * DACDelay is two ST01 reads after each OUT, using the current VGA base. */
#include "trident.h"

/* DAC/visual component widths from xf86HandleColormaps(256,6), D:3705-3719,
 * and RGB555/RGB565/RGB888 masks in the F8 addendum. */
#define PALETTE_DAC_MAX ((1u << 6) - 1u)
#define PALETTE_RGB5_MAX ((1u << 5) - 1u)
#define PALETTE_RGB8_MAX (TRI_DAC_COLORS - 1u)
/* vgaHW.c:474-487 and VGA OVERSCAN attribute index (C:1255). */
#define PALETTE_AR_ENABLE 0x00u
#define PALETTE_AR_DISABLE 0x20u
#define PALETTE_AR_OVERSCAN 0x11u

static int palette_range(struct trident_device *d, int first, int count,
                         const u8 *rgb)
{
    if (!d || !rgb || first < 0 || first >= (int)TRI_DAC_COLORS ||
        count <= 0 || count > (int)TRI_DAC_COLORS - first)
        return TRI_E_INVAL;
    return TRI_OK;
}

static int palette_write(struct trident_device *d, u32 reg, u8 value)
{
    u8 status;
    int rc = trident_write8(d, TRI_SPACE_REGS, reg, value);
    if (rc) return rc;
    rc = trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6, &status);
    if (rc) return rc;
    return trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6, &status);
}

int tri_palette_record(struct trident_device *d, int first, int count,
                       const u8 *dac6)
{
    int rc = palette_range(d, first, count, dac6);
    if (rc) return rc;
    for (int i = 0; i < count; ++i)
        for (u32 c = 0; c < GFX_RGB_COMPONENTS; ++c)
            d->dac[first + i][c] = dac6[i * GFX_RGB_COMPONENTS + c];
    return TRI_OK;
}

static int palette_write_dac6(struct trident_device *d,
                             int first, int count, const u8 *dac6)
{
    int rc = palette_range(d, first, count, dac6);
    if (rc) return rc;
    for (int i = 0; i < count; ++i) {
        rc = palette_write(d, TRI_DAC_MASK, 0xFF);
        if (rc) return rc;
        rc = palette_write(d, TRI_DAC_WRITE_INDEX, (u8)(first + i));
        if (rc) return rc;
        const u8 *color = dac6 + i * GFX_RGB_COMPONENTS;
        for (u32 c = 0; c < GFX_RGB_COMPONENTS; ++c) {
            rc = palette_write(d, TRI_DAC_DATA, color[c]);
            if (rc) return rc;
        }
        /* Only completed RGB entries become visible to cursor color lookup. */
        rc = tri_palette_record(d, first + i, 1, color);
        if (rc) return rc;
    }
    return TRI_OK;
}

int trident_palette(struct trident_device *d, int first, int count, const u8 *rgb)
{
    int rc = palette_range(d, first, count, rgb);
    if (rc) return rc;
    /* X:373-375: StoreColors does not change TrueColor colormaps. */
    switch (d->mode.selected.depth) {
    case 15: case 16: case 24: return TRI_OK;
    case 8: break;
    default: return TRI_E_INVAL;
    }
    u8 dac6[TRI_DAC_BYTES];
    for (int i = 0; i < count * GFX_RGB_COMPONENTS; ++i)
        dac6[i] = trident_palette_dac6(*rgb++, GFX_PALETTE_COMPONENT_BITS);
    return palette_write_dac6(d, first, count, dac6);
}

int tri_palette_load_default(struct trident_device *d)
{
    if (!d) return TRI_E_INVAL;
    u8 rgb[TRI_DAC_BYTES];
    u32 count;
    switch (d->mode.selected.depth) {
    case 8: {
        /* Moved from lifecycle.c:53-74: system/cube/gray, already DAC units. */
        const PaletteEntry *sys = palette_get_all();
        u32 i = 0;
        for (; i < GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP); ++i) {
            rgb[i * 3] = trident_palette_dac6(sys[i].r, GFX_PALETTE_COMPONENT_BITS);
            rgb[i * 3 + 1] = trident_palette_dac6(sys[i].g, GFX_PALETTE_COMPONENT_BITS);
            rgb[i * 3 + 2] = trident_palette_dac6(sys[i].b, GFX_PALETTE_COMPONENT_BITS);
        }
        for (u32 r = 0; r < GFX_PALETTE_CUBE_LEVELS; ++r)
            for (u32 g = 0; g < GFX_PALETTE_CUBE_LEVELS; ++g)
                for (u32 b = 0; b < GFX_PALETTE_CUBE_LEVELS; ++b, ++i) {
                    rgb[i * 3] = r * PALETTE_DAC_MAX / (GFX_PALETTE_CUBE_LEVELS - 1u);
                    rgb[i * 3 + 1] = g * PALETTE_DAC_MAX / (GFX_PALETTE_CUBE_LEVELS - 1u);
                    rgb[i * 3 + 2] = b * PALETTE_DAC_MAX / (GFX_PALETTE_CUBE_LEVELS - 1u);
                }
        for (u32 gray = 0; i < TRI_DAC_COLORS; ++i, ++gray) {
            u32 gray8 = GFX_PALETTE_GRAY_FIRST + gray * GFX_PALETTE_GRAY_STEP;
            /* Round 8bit gray to 6bit, preserving lifecycle's initial table. */
            u8 v = (gray8 * PALETTE_DAC_MAX + PALETTE_RGB8_MAX / 2u) /
                   PALETTE_RGB8_MAX;
            for (u32 c = 0; c < GFX_RGB_COMPONENTS; ++c) rgb[i * 3 + c] = v;
        }
        count = TRI_DAC_COLORS;
        break;
    }
    case 15:
        count = PALETTE_RGB5_MAX + 1u;
        for (u32 i = 0; i < count; ++i)
            for (u32 c = 0; c < GFX_RGB_COMPONENTS; ++c)
                rgb[i * 3 + c] = i * PALETTE_DAC_MAX / PALETTE_RGB5_MAX;
        break;
    case 16:
        count = PALETTE_DAC_MAX + 1u;
        for (u32 i = 0; i < count; ++i) {
            u8 rb = i <= PALETTE_RGB5_MAX ?
                    i * PALETTE_DAC_MAX / PALETTE_RGB5_MAX : 0;
            rgb[i * 3] = rb;
            rgb[i * 3 + 1] = i;
            rgb[i * 3 + 2] = rb;
        }
        break;
    case 24: /* Both packed24 and 32bpp RGB888 have depth24. */
        count = TRI_DAC_COLORS;
        for (u32 i = 0; i < count; ++i)
            for (u32 c = 0; c < GFX_RGB_COMPONENTS; ++c)
                rgb[i * 3 + c] = i * PALETTE_DAC_MAX / PALETTE_RGB8_MAX;
        break;
    default:
        return TRI_E_INVAL;
    }
    return palette_write_dac6(d, 0, count, rgb);
}

int tri_palette_reload(struct trident_device *d)
{
    if (!d) return TRI_E_INVAL;
    if (d->mode.selected.depth == 8)
        return palette_write_dac6(d, 0, TRI_DAC_COLORS, &d->dac[0][0]);
    return tri_palette_load_default(d);
}

int tri_overscan(struct trident_device *d, u8 index)
{
    if (!d) return TRI_E_INVAL;
    u8 status;
    int rc = trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6u, &status);
    if (rc) return rc;
    rc = trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, PALETTE_AR_ENABLE);
    if (rc) return rc;
    rc = trident_ar_write(d, d->cur_vga_base, PALETTE_AR_OVERSCAN, index);
    if (rc) return rc;
    rc = trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6u, &status);
    if (rc) return rc;
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, PALETTE_AR_DISABLE);
}
