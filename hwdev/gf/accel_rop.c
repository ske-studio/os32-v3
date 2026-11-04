/*
 * Copyright 2003 NVIDIA, Corporation
 * Copyright 2008 Maarten Maathuis
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "accel.h"

/* xf86-video-nouveau/src/nv_rop.h:303-318, copy and copy_planemask. */
static const u8 rop_table[16][2] = {
    {0x00, 0x00}, {0x88, 0x8a}, {0x44, 0x45}, {0xcc, 0xca},
    {0x22, 0x2a}, {0xaa, 0xaa}, {0x66, 0x6a}, {0xee, 0xea},
    {0x11, 0x15}, {0x99, 0x95}, {0x55, 0x55}, {0xdd, 0xd5},
    {0x33, 0x2e}, {0xbb, 0xba}, {0x77, 0x75}, {0xff, 0xff}
};

/* xf86-video-nouveau/src/nv04_exa.c:35-84 and hwdefs/nv01_2d.xml.h:
 * bind, pattern format/colors, ROP, and drawing operation methods. */
#define GF3_MTHD_OBJECT       0x0000U
#define GF3_MTHD_OPERATION    0x02fcU
#define GF3_MTHD_PATTERN_FMT  0x0300U
#define GF3_MTHD_PATTERN_DATA 0x0310U
#define GF3_MTHD_ROP          0x0300U
#define GF3_OPERATION_ROP     1U
#define GF3_OPERATION_COPY    3U
#define GF3_GX_COPY           3U
#define GF3_FULL_MASK         0xffffffffU

int gf3_rop_set(struct gf3_ctx *c, u8 subc, u8 alu, u32 mask,
                enum gf3_fmt fmt, u32 dl)
{
    u32 value, pattern[4];
    struct gf3_format format;
    int rc;

    if (!c || !c->ch || alu >= 16U)
        return GF3_EINVAL;
    rc = gf3_format_get(fmt, &format);
    if (rc) return rc;
    if (format.pat == GF3_FMT_NONE)
        return GF3_EFORMAT;
    if (format.cpp == 1)
        mask |= 0xffffff00U;
    else if (format.cpp == 2)
        mask |= 0xffff0000U;
    else if (format.cpp == 4 &&
             (alu != GF3_GX_COPY || mask != GF3_FULL_MASK))
        return GF3_EUNSUPPORTED;
    value = format.pat;

    if (alu == GF3_GX_COPY && mask == GF3_FULL_MASK) {
        value = GF3_OPERATION_COPY;
        rc = gf2_emit(c->ch, subc, GF3_MTHD_OPERATION, false, &value, 1, dl);
        if (rc) c->last_rc = rc;
        return rc;
    }

    pattern[0] = c->obj[GF3_OBJ_PATTERN].handle;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, GF3_MTHD_OBJECT, false, pattern, 1, dl);
    if (rc) goto failed;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, GF3_MTHD_PATTERN_FMT, false, &value, 1, dl);
    if (rc) goto failed;
    pattern[0] = mask == GF3_FULL_MASK ? GF3_FULL_MASK : 0;
    pattern[1] = mask;
    pattern[2] = pattern[3] = GF3_FULL_MASK;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, GF3_MTHD_PATTERN_DATA, false, pattern, 4, dl);
    if (rc) goto failed;
    value = c->obj[GF3_OBJ_ROP].handle;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, GF3_MTHD_OBJECT, false, &value, 1, dl);
    if (rc) goto failed;
    value = rop_table[alu][mask != GF3_FULL_MASK];
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, GF3_MTHD_ROP, false, &value, 1, dl);
    if (rc) goto failed;
    value = GF3_OPERATION_ROP;
    rc = gf2_emit(c->ch, subc, GF3_MTHD_OPERATION, false, &value, 1, dl);
failed:
    if (rc) c->last_rc = rc;
    return rc;
}
