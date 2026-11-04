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
 * see docs/design/hwdev/gf/display/fifo.md for the assignment ownership. */
#include "calc.h"
#include "api.h"
void gf_mode_fifo(struct gfinit_mode *o)
{
    o->cr[GF_MODE_CR_BURST] = GF_MODE_FIFO_BURST;
    o->cr[GF_MODE_CR_LWM] = GF_MODE_FIFO_LWM;
}

/* nouveau dispnv04/arb.c:239-264 and crtc.c:893-903, NV44. */
#define GF4_CR_FIFO_LWM_HIGH 0x47U /* nouveau dispnv04/crtc.c:901-903 */
void gf4_fifo_calc(struct gfinit_mode *o)
{
    gf_mode_fifo(o);
    o->cr[GF4_CR_FIFO_LWM_HIGH]=0;
}
