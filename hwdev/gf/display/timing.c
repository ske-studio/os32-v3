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
 * see docs/design/hwdev/gf/display/timing.md for the assignment ownership. */
#include "calc.h"
#include "api.h"
/* nouveau dispnv04/crtc.c:510-516; dispnv04/nvreg.h:160. */
enum { TIMING_CURSOR_DOUBLE_SCAN = 0x10 };
void gf_mode_timing(struct gfinit_mode *o)
{
    struct gfinit_mode_timing *t = &o->timing;
    t->hd = t->hbs = (GF_MODE_WIDTH >> GF_MODE_CHAR_SHIFT) - 1;
    t->hs = (GF_MODE_HSYNC_START >> GF_MODE_CHAR_SHIFT) + 1;
    t->he = (GF_MODE_HSYNC_END >> GF_MODE_CHAR_SHIFT) + 1;
    t->ht = (GF_MODE_HTOTAL >> GF_MODE_CHAR_SHIFT) - 5;
    t->hbe = (GF_MODE_HTOTAL >> GF_MODE_CHAR_SHIFT) - 1;
    t->vd = t->vbs = GF_MODE_HEIGHT - 1;
    t->vs = GF_MODE_VSYNC_START - 1;
    t->ve = GF_MODE_VSYNC_END - 1;
    t->vt = GF_MODE_VTOTAL - 2;
    t->vbe = GF_MODE_VTOTAL - 1;
    o->cr[GF_MODE_CR_HT] = t->ht;
    o->cr[GF_MODE_CR_HD] = t->hd;
    o->cr[GF_MODE_CR_HBS] = t->hbs;
    o->cr[GF_MODE_CR_HBE] = GF_MODE_HBE_ENABLE | (t->hbe & GF_MODE_END_MASK);
    o->cr[GF_MODE_CR_HS] = t->hs;
    o->cr[GF_MODE_CR_HE] = mode_bit(t->hbe, 5, 7) | (t->he & GF_MODE_END_MASK);
    o->cr[GF_MODE_CR_VT] = t->vt;
    o->cr[GF_MODE_CR_OVERFLOW] = mode_bit(t->vs, 9, 7) | mode_bit(t->vd, 9, 6) |
        mode_bit(t->vt, 9, 5) | GF_MODE_OVL_LINE_COMPARE | mode_bit(t->vbs, 8, 3) |
        mode_bit(t->vs, 8, 2) | mode_bit(t->vd, 8, 1) | mode_bit(t->vt, 8, 0);
    o->cr[GF_MODE_CR_CELL] = GF_MODE_CELL_LINE_COMPARE | mode_bit(t->vbs, 9, 5);
    o->cr[GF_MODE_CR_VS] = t->vs;
    o->cr[GF_MODE_CR_VE] = GF_MODE_VRE_ENABLE | (t->ve & GF_MODE_VRE_MASK);
    o->cr[GF_MODE_CR_VD] = t->vd;
    o->cr[GF_MODE_CR_VBS] = t->vbs;
    o->cr[GF_MODE_CR_VBE] = t->vbe;
    o->cr[GF_MODE_CR_MODE] = GF_MODE_CRTC_MODE;
    o->cr[GF_MODE_CR_LINE_COMPARE] = GF_MODE_BYTE_MASK;
    o->cr[GF_MODE_CR_RPC1] = GF_MODE_RPC1_LARGE;
    o->cr[GF_MODE_CR_LSR] = mode_bit(t->hbe, 6, 4) | mode_bit(t->vbs, 10, 3) |
        mode_bit(t->vs, 10, 2) | mode_bit(t->vd, 10, 1) | mode_bit(t->vt, 10, 0);
    o->cr[GF_MODE_CR_HEB] = mode_bit(t->hs, 8, 3) | mode_bit(t->hbs, 8, 2) |
        mode_bit(t->hd, 8, 1) | mode_bit(t->ht, 8, 0);
    o->cr[GF_MODE_CR_EBR] = mode_bit(t->vbs, 11, 6) | mode_bit(t->vs, 11, 4) |
        mode_bit(t->vd, 11, 2) | mode_bit(t->vt, 11, 0);
    o->cr[GF_MODE_CR_INTERLACE] = GF_MODE_BYTE_MASK;
    o->config = GF_MODE_CONFIG_HSYNC;
}

/* nouveau dispnv04/crtc.c:245-257, 301-302, 350-413. End
 * registers wrap relative to their start: HBE 7, HRE 5, VBE 8,
 * VRE 4 bits. The CRTC receives already-normalized vertical timings. */
i32 gf4_timing_calc(const struct gf4_mode *mode, struct gfinit_mode *o)
{
    if (!mode || !o) return GF4_EINVAL;
    const struct gf4_timing *v = &mode->t;
    if (!v->hd || v->hd >= v->hs || v->hs >= v->he || v->he > v->ht ||
        v->vd < 3 || v->vd >= v->vs || v->vs >= v->ve || v->ve > v->vt ||
        ((v->hd | v->hs | v->he | v->ht) & 7U) || v->ht < 40 || v->vt < 3)
        return GF4_ERANGE;
    u32 hd=(v->hd >> 3)-1, hs=(v->hs >> 3)+1, he=(v->he >> 3)+1;
    u32 ht=(v->ht >> 3)-5, hbe=(v->ht >> 3)-1;
    u32 vd=v->vd-1, vs=v->vs-1, ve=v->ve-1, vt=v->vt-2, vbe=v->vt-1;
    if (v->flags & GF4_INTERLACE) vt |= 1U;
    if (hd > 0x1ffU || hs > 0x1ffU || ht > 0x1ffU ||
        hbe-hd > 0x7fU || he-hs > 0x1fU || vd > 0xfffU ||
        vs > 0xfffU || vt > 0xfffU || vbe-vd > 0xffU || ve-vs > 0xfU)
        return GF4_ERANGE;
    struct gfinit_mode_timing *t=&o->timing;
    *t=(struct gfinit_mode_timing){hd,hs,he,ht,hd,hbe,vd,vs,ve,vt,vd,vbe};
    o->cr[GF_MODE_CR_HT]=ht; o->cr[GF_MODE_CR_HD]=hd;
    o->cr[GF_MODE_CR_HBS]=hd;
    o->cr[GF_MODE_CR_HBE]=GF_MODE_HBE_ENABLE | (hbe & GF_MODE_END_MASK);
    o->cr[GF_MODE_CR_HS]=hs;
    o->cr[GF_MODE_CR_HE]=mode_bit(hbe,5,7) | (he & GF_MODE_END_MASK);
    o->cr[GF_MODE_CR_VT]=vt;
    o->cr[GF_MODE_CR_OVERFLOW]=mode_bit(vs,9,7)|mode_bit(vd,9,6)|
        mode_bit(vt,9,5)|GF_MODE_OVL_LINE_COMPARE|mode_bit(vd,8,3)|
        mode_bit(vs,8,2)|mode_bit(vd,8,1)|mode_bit(vt,8,0);
    o->cr[GF_MODE_CR_CELL]=GF_MODE_CELL_LINE_COMPARE|mode_bit(vd,9,5)|
        ((v->flags & GF4_DBLSCAN) ? 0x80U : 0);
    o->cr[GF_MODE_CR_VS]=vs; o->cr[GF_MODE_CR_VE]=GF_MODE_VRE_ENABLE|(ve & GF_MODE_VRE_MASK);
    o->cr[GF_MODE_CR_VD]=vd; o->cr[GF_MODE_CR_VBS]=vd; o->cr[GF_MODE_CR_VBE]=vbe;
    o->cr[GF_MODE_CR_MODE]=GF_MODE_CRTC_MODE; o->cr[GF_MODE_CR_LINE_COMPARE]=GF_MODE_BYTE_MASK;
    o->cr[GF_MODE_CR_RPC1]=v->hd < 1280 ? GF_MODE_RPC1_LARGE : 0;
    o->cr[GF_MODE_CR_LSR]=mode_bit(hbe,6,4)|mode_bit(vd,10,3)|mode_bit(vs,10,2)|
        mode_bit(vd,10,1)|mode_bit(vt,10,0);
    o->cr[GF_MODE_CR_HEB]=mode_bit(hs,8,3)|mode_bit(hd,8,2)|mode_bit(hd,8,1)|mode_bit(ht,8,0);
    o->cr[GF_MODE_CR_EBR]=mode_bit(vd,11,6)|mode_bit(vs,11,4)|mode_bit(vd,11,2)|mode_bit(vt,11,0);
    u32 ilace=(ht >> 1) & ~1U;
    o->cr[GF_MODE_CR_INTERLACE]=(v->flags & GF4_INTERLACE) ? ilace : GF_MODE_BYTE_MASK;
    if (v->flags & GF4_INTERLACE) o->cr[GF_MODE_CR_HEB] |= mode_bit(ilace,8,4);
    o->config=GF_MODE_CONFIG_HSYNC;
    o->cursor_cfg=(o->cursor_cfg & ~TIMING_CURSOR_DOUBLE_SCAN) |
        ((v->flags & GF4_DBLSCAN) ? TIMING_CURSOR_DOUBLE_SCAN : 0);
    o->crtc830=v->vd-3; o->crtc834=v->vd-1;
    return GF4_OK;
}
