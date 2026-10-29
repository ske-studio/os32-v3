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
#include "calc.h"
int gf_mode_pll_program(const struct gfinit_pll_limits *limits,
                        const struct gfinit_mode_saved *saved, struct gfinit_mode *o)
{
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
