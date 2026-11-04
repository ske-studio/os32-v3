/* M2MF packet sequence adapted from xf86-video-nouveau/src/nv04_exa.c:
 * 399-430 (RectM2MF), under the following license.
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
#include "accel.h"

/* xf86-video-nouveau/src/hwdefs/nv_m2mf.xml.h:98-100,143-171;
 * nv04_exa.c:419-426. FORMAT increments input and output by one byte.
 * NOP is NV04_GRAPH(M2MF,NOP), nv_object.xml.h:239. */
#define M2MF_DMA_BUFFER_IN 0x0184U
#define M2MF_OFFSET_IN 0x030cU
#define M2MF_OFFSET_OUT 0x0310U
#define M2MF_NOP 0x0100U
#define M2MF_BYTE_INCREMENTS 0x00000101U
#define M2MF_WRITE_ONLY 0U

static int m2mf_result(struct gf3_ctx *c, int rc)
{
    if (c) c->last_rc = rc;
    return rc;
}

/* G3: reservation identity and lifetime belong to the caller. Only the
 * selected DMA object's byte envelope is checked here, without lookup. */
static int m2mf_span(struct gf2_chan *ch, u32 handle, u32 off,
                     u32 pitch, u32 rowbytes, u32 rows, struct gf3_span *span)
{
    u32 capacity;
    if (handle == GF3_HANDLE_VRAM_DMA) {
        if (ch->f->imem.reserved > ch->f->imem.vram_bytes)
            return GF3_ERANGE;
        capacity = ch->f->imem.vram_bytes - ch->f->imem.reserved;
        if (capacity > ch->B) capacity = ch->B;
    } else {
        if (!ch->staging_live || !ch->staging.va || !ch->staging.size)
            return GF2_ENOSPC;
        if (ch->staging.size > GF3_STAGING_BYTES) return GF3_ERANGE;
        capacity = ch->staging.size;
    }
    return gf3_span_check(off, capacity, pitch, rowbytes, rows, span);
}

int gf3_m2mf(struct gf3_ctx *c, u32 in_handle, u32 out_handle, u32 in_off,
             u32 out_off, u32 in_pitch, u32 out_pitch,
             u32 rowbytes, u32 rows, u32 dl)
{
    if (!c || !c->ch || !c->ch->f) return m2mf_result(c, GF3_EINVAL);
    struct gf2_chan *ch = c->ch;
    int rc = gf2_fault_check(ch);
    if (rc) return m2mf_result(c, rc);
    if (!c->ready || c->epoch != ch->epoch || ch->state != GF2_RUN)
        return m2mf_result(c, GF3_ESTATE);
    if (!rowbytes || !rows) return m2mf_result(c, GF3_OK);
    if ((in_handle != GF3_HANDLE_VRAM_DMA && in_handle != GF3_HANDLE_PCI_DMA) ||
        (out_handle != GF3_HANDLE_VRAM_DMA && out_handle != GF3_HANDLE_PCI_DMA))
        return m2mf_result(c, GF3_EINVAL);

    struct gf3_span in, out;
    rc = m2mf_span(ch, in_handle, in_off, in_pitch, rowbytes, rows, &in);
    if (rc) return m2mf_result(c, rc);
    rc = m2mf_span(ch, out_handle, out_off, out_pitch, rowbytes, rows, &out);
    if (rc) return m2mf_result(c, rc);
    if (in_handle == GF3_HANDLE_VRAM_DMA && out_handle == GF3_HANDLE_VRAM_DMA &&
        (in_off <= out_off ? out_off - in_off < in.bytes :
                            in_off - out_off < out.bytes))
        return m2mf_result(c, GF3_EUNSUPPORTED);

    while (rows) {
        u32 n = rows > GF3_M2MF_MAX_ROWS ? GF3_M2MF_MAX_ROWS : rows;
        u32 handles[2] = {in_handle, out_handle};
        u32 v[8] = {in_off, out_off, in_pitch, out_pitch, rowbytes, n,
                    M2MF_BYTE_INCREMENTS, M2MF_WRITE_ONLY};
        u32 zero = 0;
        rc = gf2_emit(ch, GF3_SUBC_M2MF, M2MF_DMA_BUFFER_IN, false, handles, 2, dl);
        if (rc) return m2mf_result(c, rc);
        rc = gf2_emit(ch, GF3_SUBC_M2MF, M2MF_OFFSET_IN, false, v, 8, dl);
        if (rc) return m2mf_result(c, rc);
        rc = gf2_emit(ch, GF3_SUBC_M2MF, M2MF_NOP, false, &zero, 1, dl);
        if (rc) return m2mf_result(c, rc);
        rc = gf2_emit(ch, GF3_SUBC_M2MF, M2MF_OFFSET_OUT, false, &zero, 1, dl);
        if (rc) return m2mf_result(c, rc);
        rows -= n;
        /* The final row need not have trailing pitch padding. Advance only
         * when another chunk exists; full spans already proved it fits. */
        if (rows) {
            in_off += in_pitch * n;
            out_off += out_pitch * n;
        }
    }
    struct gf2_fence fence;
    rc = gf2_fence_emit(ch, &fence, dl);
    if (!rc) rc = gf2_fence_wait(ch, fence, dl);
    return m2mf_result(c, rc);
}
