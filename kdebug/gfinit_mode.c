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
/* Move one timing bit to the named register bit (hw.h:34 XLATE). */
STATIC_ASSERT(GF_MODE_DCB_OR == 0 && GF_MODE_DCB_CONNECTOR == 0 &&
              (GF_MODE_DCB_HEADS & (1U << GF_MODE_HEAD)), mode_dcb_head_a_or0);
static u32 mode_bit(u32 value, u32 from, u32 to)
{
    return ((value >> from) & 1U) << to;
}
int gfinit_mode_calc(const struct gfinit_pll_limits *limits,
                     const struct gfinit_mode_saved *saved, struct gfinit_mode *o)
{
    *o = (struct gfinit_mode){0};
    /* crtc.c:245-256, analog corrections. */
    struct gfinit_mode_timing *t = &o->timing;
    t->hd = t->hbs = (GF_MODE_WIDTH >> GF_MODE_CHAR_SHIFT) - 1;
    t->hs = (GF_MODE_HSYNC_START >> GF_MODE_CHAR_SHIFT) + 1;
    t->he = (GF_MODE_HSYNC_END >> GF_MODE_CHAR_SHIFT) + 1;
    t->ht = (GF_MODE_HTOTAL >> GF_MODE_CHAR_SHIFT) - 5;
    t->hbe = (GF_MODE_HTOTAL >> GF_MODE_CHAR_SHIFT) - 1;
    t->vd = t->vbs = GF_MODE_HEIGHT - 1;
    t->vs = GF_MODE_VSYNC_START - 1; t->ve = GF_MODE_VSYNC_END - 1;
    t->vt = GF_MODE_VTOTAL - 2; t->vbe = GF_MODE_VTOTAL - 1;
    /* crtc.c:305-381, negative H/V, blanked sequencer, pitch in units of 8. */
    o->misc = GF_MODE_MISC_NEG_HV;
    o->sr[0] = 0; o->sr[1] = GF_MODE_SR_BLANKED; o->sr[2] = GF_MODE_PLANES; o->sr[3] = 0; o->sr[4] = GF_MODE_SR_LINEAR;
    o->cr[GF_MODE_CR_HT] = t->ht; o->cr[GF_MODE_CR_HD] = t->hd; o->cr[GF_MODE_CR_HBS] = t->hbs;
    o->cr[GF_MODE_CR_HBE] = GF_MODE_HBE_ENABLE | (t->hbe & GF_MODE_END_MASK);
    o->cr[GF_MODE_CR_HS] = t->hs;
    o->cr[GF_MODE_CR_HE] = mode_bit(t->hbe, 5, 7) | (t->he & GF_MODE_END_MASK);
    o->cr[GF_MODE_CR_VT] = t->vt;
    o->cr[GF_MODE_CR_OVERFLOW] = mode_bit(t->vs, 9, 7) | mode_bit(t->vd, 9, 6) |
        mode_bit(t->vt, 9, 5) | GF_MODE_OVL_LINE_COMPARE | mode_bit(t->vbs, 8, 3) |
        mode_bit(t->vs, 8, 2) | mode_bit(t->vd, 8, 1) | mode_bit(t->vt, 8, 0);
    o->cr[GF_MODE_CR_CELL] = GF_MODE_CELL_LINE_COMPARE | mode_bit(t->vbs, 9, 5);
    o->cr[GF_MODE_CR_VS] = t->vs; o->cr[GF_MODE_CR_VE] = GF_MODE_VRE_ENABLE | (t->ve & GF_MODE_VRE_MASK);
    o->cr[GF_MODE_CR_VD] = t->vd; o->cr[GF_MODE_CR_PITCH] = GF_MODE_PITCH / 8;
    o->cr[GF_MODE_CR_VBS] = t->vbs; o->cr[GF_MODE_CR_VBE] = t->vbe;
    o->cr[GF_MODE_CR_MODE] = GF_MODE_CRTC_MODE; o->cr[GF_MODE_CR_LINE_COMPARE] = GF_MODE_BYTE_MASK;
    /* crtc.c:388-413, pitch extension, large mode, timing overflow. */
    o->cr[GF_MODE_CR_PITCH_HI] = (GF_MODE_PITCH / 8 >> 8) & 7;
    o->cr[GF_MODE_CR_PITCH11] = (GF_MODE_PITCH / 8 >> 11) & 1;
    o->cr[GF_MODE_CR_RPC1] = GF_MODE_RPC1_LARGE;
    o->cr[GF_MODE_CR_LSR] = mode_bit(t->hbe, 6, 4) | mode_bit(t->vbs, 10, 3) |
        mode_bit(t->vs, 10, 2) | mode_bit(t->vd, 10, 1) | mode_bit(t->vt, 10, 0);
    o->cr[GF_MODE_CR_HEB] = mode_bit(t->hs, 8, 3) | mode_bit(t->hbs, 8, 2) |
        mode_bit(t->hd, 8, 1) | mode_bit(t->ht, 8, 0);
    o->cr[GF_MODE_CR_EBR] = mode_bit(t->vbs, 11, 6) | mode_bit(t->vs, 11, 4) |
        mode_bit(t->vd, 11, 2) | mode_bit(t->vt, 11, 0);
    o->cr[GF_MODE_CR_INTERLACE] = GF_MODE_BYTE_MASK;
    /* crtc.c:418-449. Zero fields come from zero state, not saved POST. */
    o->gr[5] = GF_MODE_GR_INDEXED; o->gr[6] = GF_MODE_GR_MAP; o->gr[7] = GF_MODE_PLANES; o->gr[8] = GF_MODE_BYTE_MASK;
    for (u32 i = 0; i < 16; i++) o->ar[i] = i;
    o->ar[0x10] = 1; o->ar[0x12] = GF_MODE_PLANES;
    /* crtc.c:494-578; cursor.c:38-59 offset=0, hidden.
     * arb.c:239-264: NV44A fixed burst/lwm. CR49/58/5b are zero-state
     * (disp.c:223), analog stopping paths per DESIGN §3. */
    o->cr[GF_MODE_CR_ENH] = saved->cr1c & ~GF_MODE_ENH_CLEAR;
    o->cr[GF_MODE_CR_LCD] = saved->cr33 & ~GF_MODE_LCD_CLEAR;
    o->cr[GF_MODE_CR_SCRATCH3] = GF_MODE_ANALOG_SCRATCH; o->cr[GF_MODE_CR_SCRATCH4] = saved->cr3c;
    o->cr[GF_MODE_CR_SCRATCH4B] = saved->cr4b | GF_MODE_HEAD_A_SCRATCH;
    o->cr[GF_MODE_CR_LATENCY] = (saved->cr52 + GF_MODE_LATENCY_ADD) & GF_MODE_BYTE_MASK;
    o->cr[GF_MODE_CR_9F] = 1; o->cr[GF_MODE_CR_85] = GF_MODE_BYTE_MASK; o->cr[GF_MODE_CR_86] = 1;
    o->cr[GF_MODE_CR_PIXEL] = (GF_MODE_BPP + 1) / 8;
    o->cr[GF_MODE_CR_CURSOR] = GF_MODE_CURSOR_HIDDEN; o->cr[GF_MODE_CR_BURST] = GF_MODE_FIFO_BURST; o->cr[GF_MODE_CR_LWM] = GF_MODE_FIFO_LWM;
    o->engine_ctrl = GF_MODE_ENGINE_I2C; o->cursor_cfg = GF_MODE_CURSOR_CONFIG;
    o->crtc830 = GF_MODE_HEIGHT - 3; o->crtc834 = GF_MODE_HEIGHT - 1;
    o->gpio_ext = saved->gpio_ext; o->crtc850 = saved->crtc850;
    o->config = GF_MODE_CONFIG_HSYNC; o->start = 0;
    /* crtc.c:589-606; dfp.c:93-110; dac.c:375-409 head A/OR0. */
    o->cursync = GF_MODE_CURSYNC; o->general = GF_MODE_GENERAL;
    o->fp_control = GF_MODE_FP_OFF; o->ramdac8c0 = GF_MODE_RAMDAC8C0;
    o->ramdaca24 = GF_MODE_RAMDACA24; o->ramdaca34 = 1;
    o->dacclk = (GF_MODE_HEAD << 8) | GF_MODE_DAC_ENABLE; o->test_control = GF_MODE_TEST_CONTROL;
    /* crtc.c:129-162,685-689. Reuse the integer solver/5-write sequence;
     * 50c includes head A bit28 as well as Curie bit2 and source bit9. */
    o->sel_clk = saved->sel_clk & ~GF_MODE_SEL_CLK_CLEAR;
    o->pllsel = GF_MODE_PLL_SELECT;
    o->limits = *limits;
    o->single = GF_MODE_CLOCK_KHZ <= limits->vco1.max_freq / 2;
    if (o->single) o->limits.vco2 = (struct gfinit_pll_vco){0};
    if (gfinit_pll_solve(&o->limits, GF_MODE_CLOCK_KHZ, &o->pll)) return -1;
    return gfinit_pll_sequence(GF_PLL_VIDEO, &o->limits, &o->pll, &o->sequence);
}
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
