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
#include "api.h"
/* GF_FEAT4_DESIGN §3 pitch alignment; nouveau dispnv04/crtc.c:308-312,
 * 868-888 and nvreg.h:230,281,341-347 register fields. */
enum {
    FORMAT_PITCH_ALIGN = 64, FORMAT_START_ALIGN = 4,
    FORMAT_PITCH_UNIT = 8, FORMAT_PITCH_MAX = 0xfff,
    FORMAT_MISC_BASE = 0x23, FORMAT_MISC_NEG_H = 0x40,
    FORMAT_MISC_NEG_V = 0x80, FORMAT_PIXEL_MASK = 3,
    FORMAT_DAC_ALT_MODE = 0x1000, FORMAT_PITCH_HI_SHIFT = 8,
    FORMAT_PITCH_HI_MASK = 7, FORMAT_PITCH_HI_DEST = 5,
    FORMAT_PITCH_LOW_MASK = 0xff, FORMAT_PITCH_11_SHIFT = 11,
    FORMAT_PITCH_11_DEST = 6
};
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

/* nouveau dispnv04/crtc.c:308-340, 838-903; nvreg.h:230,281,341-347. */
i32 gf4_format_calc(const struct gf4_mode *mode,const struct gf3_surface *front,
                    struct gfinit_mode *o)
{
    if (!mode || !front || !o) return GF4_EINVAL;
    u32 cpp, pixel;
    switch (mode->fmt) {
    case GF3_INDEX8: cpp=1; pixel=1; break;
    case GF3_RGB565: cpp=2; pixel=2; break;
    case GF3_XRGB8888: cpp=4; pixel=3; break;
    default: return GF4_EFORMAT;
    }
    if (front->fmt != mode->fmt) return GF4_EFORMAT;
    u32 pitch=((u32)mode->t.hd * cpp + FORMAT_PITCH_ALIGN - 1U) & ~(FORMAT_PITCH_ALIGN - 1U);
    if (!pitch || mode->pitch != pitch || front->pitch != pitch ||
        pitch / FORMAT_PITCH_UNIT > FORMAT_PITCH_MAX || (front->off & (FORMAT_START_ALIGN - 1U))) return GF4_ERANGE;
    gf_mode_format(o);
    o->misc=FORMAT_MISC_BASE | ((mode->t.flags & GF4_NEG_H) ? FORMAT_MISC_NEG_H : 0) |
        ((mode->t.flags & GF4_NEG_V) ? FORMAT_MISC_NEG_V : 0);
    o->cr[GF_MODE_CR_PIXEL]=(o->cr[GF_MODE_CR_PIXEL] & ~FORMAT_PIXEL_MASK) | pixel;
    o->general=GF_MODE_GENERAL | (mode->fmt == GF3_RGB565 ? FORMAT_DAC_ALT_MODE : 0);
    u32 q=pitch / FORMAT_PITCH_UNIT;
    o->cr[GF_MODE_CR_PITCH_HI]=((q >> FORMAT_PITCH_HI_SHIFT) & FORMAT_PITCH_HI_MASK) << FORMAT_PITCH_HI_DEST;
    o->cr[GF_MODE_CR_PITCH]=q & FORMAT_PITCH_LOW_MASK;
    o->cr[GF_MODE_CR_PITCH11]=((q >> FORMAT_PITCH_11_SHIFT) & 1U) << FORMAT_PITCH_11_DEST;
    o->start=front->off;
    return GF4_OK;
}
