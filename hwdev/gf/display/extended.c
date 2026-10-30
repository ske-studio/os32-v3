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
/* NV44A analog: MIT nouveau c03c1a1 dispnv04/crtc.c:245-606;
 * see docs/design/hwdev/gf/display/extended.md for the assignment ownership. */
#include "calc.h"
void gf_mode_extended(const struct gfinit_mode_saved *saved, struct gfinit_mode *o)
{
    o->cr[GF_MODE_CR_ENH] = saved->cr1c & ~GF_MODE_ENH_CLEAR;
    o->cr[GF_MODE_CR_LCD] = saved->cr33 & ~GF_MODE_LCD_CLEAR;
    o->cr[GF_MODE_CR_SCRATCH3] = GF_MODE_ANALOG_SCRATCH;
    o->cr[GF_MODE_CR_SCRATCH4] = saved->cr3c;
    o->cr[GF_MODE_CR_SCRATCH4B] = saved->cr4b | GF_MODE_HEAD_A_SCRATCH;
    o->cr[GF_MODE_CR_LATENCY] = (saved->cr52 + GF_MODE_LATENCY_ADD) & GF_MODE_BYTE_MASK;
    o->cr[GF_MODE_CR_9F] = 1;
    o->cr[GF_MODE_CR_85] = GF_MODE_BYTE_MASK;
    o->cr[GF_MODE_CR_86] = 1;
    o->engine_ctrl = GF_MODE_ENGINE_I2C;
    o->crtc830 = GF_MODE_HEIGHT - 3;
    o->crtc834 = GF_MODE_HEIGHT - 1;
    o->gpio_ext = saved->gpio_ext;
    o->crtc850 = saved->crtc850;
}
