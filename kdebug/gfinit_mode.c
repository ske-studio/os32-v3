/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/* NV44A analog head A adaptation of nouveau c03c1a1 MIT dispnv04/crtc.c.
 * Fixed progressive 8bpp state only. Register references below are to that
 * file unless otherwise specified; order of future writes is in the C table.
 * No POST timing, FP/TV timing, cursor allocation or hardware I/O here. */
#include "../hwdev/gfinit_mode.h"
void gfinit_mode_palette(u32 index, u8 rgb[GF_MODE_RGB])
{
    /* DESIGN §3 L3a: diagnostic colors, not nouveau's saved palette.
     * hw.c:794-807 supplies the mask/index/data write order. */
    static const u8 colors[GF_MODE_PALETTE_COLORS][GF_MODE_RGB] = {
        {0,0,0}, {255,0,0}, {0,255,0}, {0,0,255},
        {255,255,255}, {0,255,255}, {255,0,255}, {255,255,0}
    };
    for (u32 c = 0; c < GF_MODE_RGB; c++)
        rgb[c] = index < GF_MODE_PALETTE_COLORS ? colors[index][c] : 0;
}

int gfinit_mode_row(u32 y, u8 row[GF_MODE_WIDTH])
{
    if (!row || y >= GF_MODE_HEIGHT) return -1;
    for (u32 x = 0; x < GF_MODE_WIDTH; x++) {
        u32 color = x / GF_MODE_BAND_WIDTH;
        if (x >= GF_MODE_FRAME_WIDTH && x < GF_MODE_FRAME_WIDTH + GF_MODE_CHECK_SIZE &&
            y >= GF_MODE_FRAME_WIDTH && y < GF_MODE_FRAME_WIDTH + GF_MODE_CHECK_SIZE) {
            u32 cx = (x - GF_MODE_FRAME_WIDTH) / GF_MODE_CHECK_CELL;
            u32 cy = (y - GF_MODE_FRAME_WIDTH) / GF_MODE_CHECK_CELL;
            color = ((cx + cy) & 1U) ? GF_MODE_COLOR_BLACK : GF_MODE_COLOR_WHITE;
        }
        if (x < GF_MODE_FRAME_WIDTH || x >= GF_MODE_WIDTH - GF_MODE_FRAME_WIDTH ||
            y < GF_MODE_FRAME_WIDTH || y >= GF_MODE_HEIGHT - GF_MODE_FRAME_WIDTH)
            color = GF_MODE_COLOR_WHITE;
        row[x] = (u8)color;
    }
    return 0;
}
