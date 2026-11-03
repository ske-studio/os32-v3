/*
 * Copyright 2009 Ben Skeggs
 * Copyright 2008 Stuart Bennett
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
/* GF_FEAT3_DESIGN section 6: nv04_fbcon.c:105-124 (MIT, v6.1).
 * OS32 adds checked spans, arbitrary RAM pitch and bounded scratch packing. */
#include "accel.h"

/* nv04_fbcon.c:105-124,246-252: GDI mono methods, SRCCOPY and CGA6.
 * Field masks: xf86-video-nouveau/src/hwdefs/nv01_2d.xml.h:1278-1314. */
#define MONO_OPERATION 0x02fcU
#define MONO_FORMAT 0x0304U
#define MONO_SETUP 0x0be4U
#define MONO_DATA 0x0c00U
#define MONO_SRCCOPY 3U
#define MONO_CGA6 1U
#define MONO_BYTE_BITS 8U
#define MONO_FIELD_MAX 0xffffU

int gf3_mono(struct gf3_ctx *c, const struct gf3_surface *s,
             struct gf3_rect r, const struct gf3_ram *bits, u32 fg, u32 bg,
             struct gf3_rect clip, u32 dl)
{
    struct gf3_format fmt;
    struct gf3_span dst, ram;
    struct gf2_fence fence;
    u32 left, top, right, bottom, rowbytes, padded, words;
    u32 v[7], row = 0, byte = 0;
    int rc = GF3_EINVAL;

    if (!c) return rc;
    if (!c->ch || !s || !bits) goto done;
    rc = gf2_fault_check(c->ch);
    if (rc) goto done;
    if (!c->ready || c->epoch != c->ch->epoch || c->ch->state != GF2_RUN) {
        rc = GF3_ESTATE;
        goto done;
    }
    rc = GF3_OK;
    if (!r.w || !r.h) goto done;
    rc = gf3_surface_check(c, s, r, GF3_USE_GDI, &fmt, &dst);
    if (rc) goto done;

    /* Intersect without computing a possibly overflowing clip endpoint. */
    left = r.x > clip.x ? r.x : clip.x;
    top = r.y > clip.y ? r.y : clip.y;
    right = r.x + r.w;
    bottom = r.y + r.h;
    rc = GF3_OK;
    if (!clip.w || !clip.h || left >= right || top >= bottom) goto done;
    if (clip.w < right - clip.x) right = clip.x + clip.w;
    if (clip.h < bottom - clip.y) bottom = clip.y + clip.h;
    if (left >= right || top >= bottom) goto done;

    rowbytes = (r.w + MONO_BYTE_BITS - 1) / MONO_BYTE_BITS;
    padded = rowbytes * MONO_BYTE_BITS;
    if (padded > MONO_FIELD_MAX || right > MONO_FIELD_MAX ||
        bottom > MONO_FIELD_MAX) {
        rc = GF3_ERANGE;
        goto done;
    }
    rc = gf3_ram_check(bits, rowbytes, r.h, &ram);
    if (rc) goto done;
    rc = gf3_surface_set(c, s, s, true, dl);
    if (rc) goto done;
    v[0] = MONO_SRCCOPY;
    rc = gf2_emit(c->ch, GF3_SUBC_GDI, MONO_OPERATION, false, v, 1, dl);
    if (rc) goto done;
    v[0] = MONO_CGA6;
    rc = gf2_emit(c->ch, GF3_SUBC_GDI, MONO_FORMAT, false, v, 1, dl);
    if (rc) goto done;
    v[0] = (top << 16) | left;
    v[1] = (bottom << 16) | right;
    v[2] = bg;
    v[3] = fg;
    v[4] = (r.h << 16) | padded;
    v[5] = (r.h << 16) | r.w;
    v[6] = (r.y << 16) | r.x;
    rc = gf2_emit(c->ch, GF3_SUBC_GDI, MONO_SETUP, false, v, 7, dl);
    if (rc) goto done;

    /* surface_check bounds w/h to 16 bits, so this product fits u32.
     * Rows are byte-aligned, not dword-aligned (nv04_fbcon.c:114). */
    words = (rowbytes * r.h + sizeof(u32) - 1) / sizeof(u32);
    while (words) {
        u32 count = words > GF3_MONO_BATCH_WORDS ? GF3_MONO_BATCH_WORDS : words;
        for (u32 i = 0; i < count; ++i) {
            u32 word = 0;
            for (u32 b = 0; b < sizeof(u32) && row < r.h; ++b) {
                word |= (u32)((const u8 *)bits->ptr)[row * bits->pitch + byte] << (b * MONO_BYTE_BITS);
                if (++byte == rowbytes) { byte = 0; ++row; }
            }
            c->scratch[i] = word;
        }
        rc = gf2_emit(c->ch, GF3_SUBC_GDI, MONO_DATA, false, c->scratch, count, dl);
        if (rc) goto done;
        words -= count;
    }
    rc = gf2_fence_emit(c->ch, &fence, dl);
    if (!rc) rc = gf2_fence_wait(c->ch, fence, dl);
done:
    c->last_rc = rc;
    return rc;
}
