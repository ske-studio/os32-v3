/*
 * Copyright 2009 Nouveau Project
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/* GF_FEAT3_DESIGN section 7, G2/G3 and N1. Adapted from MIT
 * xf86-video-nouveau/src/nouveau_exa.c:242-288,305-371. OS32 borrows F2's
 * one-page staging instead of allocating BOs, and waits for each fragment
 * in gf3_m2mf before reusing staging. No CPU fallback after submission. */
#include "accel.h"

/* nouveau_exa.c:320: hostdata upload is preferred below 16 KiB. */
#define TRANSFER_IFC_BYTES (16U * 1024U)

static int transfer_result(struct gf3_ctx *c, int rc)
{
    if (c) c->last_rc = rc;
    return rc;
}

static bool transfer_ifc(const struct gf3_format *fmt,
                         const struct gf3_span *span)
{
    /* nv04_exa.c:296-319; use division to avoid rowbytes*rows overflow. */
    return fmt->ifc != GF3_FMT_NONE &&
        span->rowbytes >= GF3_IFC_MIN_ROW_BYTES &&
        span->rows <= GF3_IFC_MAX_HEIGHT &&
        span->rowbytes <= GF3_SCRATCH_WORDS * sizeof(u32) &&
        ((span->rowbytes + GF3_IFC_ROW_ALIGN - 1U) &
         ~(GF3_IFC_ROW_ALIGN - 1U)) / sizeof(u32) <= GF3_SCRATCH_WORDS &&
        span->rows <= (TRANSFER_IFC_BYTES - 1U) / span->rowbytes;
}

static int transfer(struct gf3_ctx *c, const struct gf3_surface *s,
                     struct gf3_rect r, const struct gf3_ram *ram,
                     bool upload, bool force_m2mf, u32 dl)
{
    struct gf3_format fmt;
    struct gf3_span vr, cpu;
    if (!c || !c->ch || !s || !ram)
        return transfer_result(c, GF3_EINVAL);
    if (!c->ready || c->epoch != c->ch->epoch)
        return transfer_result(c, GF3_ESTATE);
    int rc = gf2_fault_check(c->ch);
    if (rc) return transfer_result(c, rc);
    if (c->ch->state != GF2_RUN)
        return transfer_result(c, GF3_ESTATE);
    if (!r.w || !r.h) return transfer_result(c, GF3_OK);

    /* Validate the entire request before IFC or the first staging write.
     * B1 performs reservation identity and overflow-safe byte validation. */
    rc = gf3_surface_check(c, s, r, GF3_USE_BYTES, &fmt, &vr);
    if (rc) return transfer_result(c, rc);
    rc = gf3_ram_check(ram, vr.rowbytes, vr.rows, &cpu);
    if (rc) return transfer_result(c, rc);
    if (upload && !force_m2mf && transfer_ifc(&fmt, &vr)) {
        rc = gf3_ifc(c, s, r, ram, dl);
        if (rc != GF3_EUNSUPPORTED) return transfer_result(c, rc);
    }
    if (!c->ch->staging_live) return transfer_result(c, GF2_ENOSPC);

    /* nouveau_exa.c:210-237 never reuses outstanding scratch. With one
     * borrowed page, retire all earlier work before touching it again.
     * A failed emit/wait proves no completion, even if RUN is unchanged. */
    if (c->ch->staging_pending) {
        struct gf2_fence fence;
        rc = gf2_fence_emit(c->ch, &fence, dl);
        if (!rc) rc = gf2_fence_wait(c->ch, fence, dl);
        if (rc) return transfer_result(c, rc);
        c->ch->staging_pending = false;
    }

    u8 *staging = c->ch->staging.va;
    u8 *memory = ram->ptr;
    for (u32 y = 0; y < vr.rows;) {
        u32 rows = 1;
        if (vr.rowbytes <= GF3_STAGING_BYTES) {
            rows = GF3_STAGING_BYTES / vr.rowbytes;
            if (rows > GF3_M2MF_MAX_ROWS) rows = GF3_M2MF_MAX_ROWS;
            if (rows > vr.rows - y) rows = vr.rows - y;
        }
        for (u32 x = 0; x < vr.rowbytes;) {
            u32 bytes = vr.rowbytes - x;
            if (bytes > GF3_STAGING_BYTES)
                bytes = (GF3_STAGING_BYTES / fmt.cpp) * fmt.cpp;
            if (upload) {
                /* nouveau_exa.c:350-357: pack valid bytes, skip RAM gaps. */
                for (u32 j = 0; j < rows; ++j)
                    for (u32 b = 0; b < bytes; ++b)
                        staging[j * bytes + b] = memory[(y + j) * cpu.pitch + x + b];
                __sync_synchronize();
            }
            u32 off = vr.off + y * vr.pitch + x;
            c->ch->staging_pending = true;
            if (upload)
                rc = gf3_m2mf(c, GF3_HANDLE_PCI_DMA, GF3_HANDLE_VRAM_DMA,
                              0, off, bytes, vr.pitch, bytes, rows, dl);
            else
                rc = gf3_m2mf(c, GF3_HANDLE_VRAM_DMA, GF3_HANDLE_PCI_DMA,
                              off, 0, vr.pitch, bytes, bytes, rows, dl);
            if (rc) return transfer_result(c, rc);
            c->ch->staging_pending = false;
            if (!upload) {
                /* nouveau_exa.c:268-280: completion precedes CPU unpack. */
                __sync_synchronize();
                for (u32 j = 0; j < rows; ++j)
                    for (u32 b = 0; b < bytes; ++b)
                        memory[(y + j) * cpu.pitch + x + b] = staging[j * bytes + b];
            }
            x += bytes;
        }
        y += rows;
    }
    return transfer_result(c, GF3_OK);
}

int gf3_upload(struct gf3_ctx *c, const struct gf3_surface *s,
               struct gf3_rect r, const struct gf3_ram *ram,
               bool force_m2mf, u32 dl)
{
    return transfer(c, s, r, ram, true, force_m2mf, dl);
}

int gf3_download(struct gf3_ctx *c, const struct gf3_surface *s,
                 struct gf3_rect r, struct gf3_ram *ram, u32 dl)
{
    return transfer(c, s, r, ram, false, true, dl);
}
