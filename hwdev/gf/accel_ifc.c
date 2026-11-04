/*
 * Copyright 2003 NVIDIA, Corporation
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/* IFC sequence: xf86-video-nouveau/src/nv04_exa.c:296-374 (MIT).
 * GF_FEAT3_DESIGN section 7: restart at each row, with scratch padding;
 * never read RAM row gaps or depend on IFC state surviving a push wrap. */
#include "accel.h"

/* nv04_exa.c:326-339,351-370; hwdefs/nv01_2d.xml.h:201-211,783-820. */
#define IFC_BIND 0x0000U
#define IFC_CLIP_POINT 0x0300U
#define IFC_OPERATION 0x02fcU
#define IFC_POINT 0x0304U
#define IFC_COLOR 0x0400U
#define IFC_SRCCOPY 3U
#define IFC_FIELD_SHIFT 16U

int gf3_ifc(struct gf3_ctx *c, const struct gf3_surface *dst,
            struct gf3_rect r, const struct gf3_ram *ram, u32 dl)
{
    struct gf3_format fmt;
    struct gf3_span part, full, input;
    struct gf2_fence fence;
    u32 v[3], padded, words, iw;
    int rc;

    if (!c) return GF3_EINVAL;
    if (!c->ch || !dst || !ram) { rc = GF3_EINVAL; goto done; }
    if (!c->ready || c->epoch != c->ch->epoch) {
        rc = GF3_ESTATE;
        goto done;
    }
    rc = gf2_fault_check(c->ch);
    if (rc) goto done;
    if (c->ch->state != GF2_RUN) { rc = GF3_ESTATE; goto done; }
    if (!r.w || !r.h) { rc = GF3_OK; goto done; }

    rc = gf3_surface_check(c, dst, r, GF3_USE_IFC, &fmt, &part);
    if (rc) goto done;
    rc = gf3_ram_check(ram, part.rowbytes, r.h, &input);
    if (rc) goto done;
    if (part.rowbytes < GF3_IFC_MIN_ROW_BYTES || r.h > GF3_IFC_MAX_HEIGHT ||
        part.rowbytes > GF3_SCRATCH_WORDS * sizeof(u32)) {
        rc = GF3_EUNSUPPORTED;
        goto done;
    }
    padded = (part.rowbytes + GF3_IFC_ROW_ALIGN - 1U) &
             ~(GF3_IFC_ROW_ALIGN - 1U);
    words = padded / sizeof(u32);
    iw = padded / fmt.cpp;
    /* surface_set validates the entire descriptor as 2D. Do that before
     * the first clip packet as well (G2: no validation after submission). */
    rc = gf3_surface_check(c, dst,
                           (struct gf3_rect){0, 0, dst->width, dst->height},
                           GF3_USE_2D, &fmt, &full);
    if (rc) goto done;

    v[0] = c->obj[GF3_OBJ_CLIP].handle;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, IFC_BIND, false, v, 1, dl);
    if (rc) goto done;
    v[0] = (r.y << IFC_FIELD_SHIFT) | r.x;
    v[1] = (r.h << IFC_FIELD_SHIFT) | r.w;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, IFC_CLIP_POINT, false, v, 2, dl);
    if (rc) goto done;
    rc = gf3_surface_set(c, dst, dst, false, dl);
    if (rc) goto done;
    v[0] = IFC_SRCCOPY;
    v[1] = fmt.ifc;
    rc = gf2_emit(c->ch, GF3_SUBC_IFC, IFC_OPERATION, false, v, 2, dl);
    if (rc) goto done;

    for (u32 y = 0; y < r.h; ++y) {
        const u8 *src = (const u8 *)ram->ptr + y * input.pitch;
        /* Explicit byte packing also accepts unaligned input and a final
         * row with exactly rowbytes available, without padding in RAM. */
        for (u32 i = 0; i < words; ++i) c->scratch[i] = 0;
        for (u32 b = 0; b < part.rowbytes; ++b)
            c->scratch[b / sizeof(u32)] |= (u32)src[b] << ((b % sizeof(u32)) * 8U);
        v[0] = ((r.y + y) << IFC_FIELD_SHIFT) | r.x;
        v[1] = (1U << IFC_FIELD_SHIFT) | r.w;
        v[2] = (1U << IFC_FIELD_SHIFT) | iw;
        rc = gf2_emit(c->ch, GF3_SUBC_IFC, IFC_POINT, false, v, 3, dl);
        if (rc) goto done;
        rc = gf2_emit(c->ch, GF3_SUBC_IFC, IFC_COLOR, false,
                      c->scratch, (u16)words, dl);
        if (rc) goto done;
    }
    rc = gf2_fence_emit(c->ch, &fence, dl);
    if (!rc) rc = gf2_fence_wait(c->ch, fence, dl);
done:
    c->last_rc = rc;
    return rc;
}
