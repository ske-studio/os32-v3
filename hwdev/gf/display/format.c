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
 * see docs/design/hwdev/gf/display/format.md for the assignment ownership. */
#include "calc.h"
void gf_mode_format(struct gfinit_mode *o)
{
    o->misc = GF_MODE_MISC_NEG_HV;
    o->sr[0] = 0;
    o->sr[1] = GF_MODE_SR_BLANKED;
    o->sr[2] = GF_MODE_PLANES;
    o->sr[3] = 0;
    o->sr[4] = GF_MODE_SR_LINEAR;
    o->gr[5] = GF_MODE_GR_INDEXED;
    o->gr[6] = GF_MODE_GR_MAP;
    o->gr[7] = GF_MODE_PLANES;
    o->gr[8] = GF_MODE_BYTE_MASK;
    for (u32 i = 0; i < 16; i++) o->ar[i] = i;
    o->ar[0x10] = 1;
    o->ar[0x12] = GF_MODE_PLANES;
    o->cr[GF_MODE_CR_PIXEL] = (GF_MODE_BPP + 1) / 8;
}
