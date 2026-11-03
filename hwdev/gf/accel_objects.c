/* Layer 3 object setup, adapted from xf86-video-nouveau/src/
 * nv_accel_common.c:185-251,313-578 (MIT). OS32 lifecycle: F3 sections 2/3.
 *
 * Copyright 2007 Ben Skeggs
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
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "accel.h"

static const u16 classes[GF3_OBJECT_COUNT] = {
    GF3_CLASS_NULL, GF3_CLASS_SURFACE2D, GF3_CLASS_BETA1, GF3_CLASS_BETA4,
    GF3_CLASS_PATTERN, GF3_CLASS_ROP, GF3_CLASS_GDI, GF3_CLASS_BLIT,
    GF3_CLASS_SIFM, GF3_CLASS_CLIP, GF3_CLASS_M2MF, GF3_CLASS_IFC
};

/* Methods/values: nv_accel_common.c ranges below; nv01_2d.xml.h:
 * 96-142,199-223,657-689,757-810,890-934,1169-1198; nv_m2mf.xml.h:96-100.
 * Each row is an increment packet; bind is method 0000, count 1.
 * OS32 uses NULL's handle for notify except M2MF's separate ctxdma. */
static const struct {
    u8 subc;
    u16 method, count;
    u32 data[7]; /* SIFM's seven context handles, common.c:472-480. */
} setup[] = {
    /* common.c:185-209,213-251. */
    {GF3_SUBC_SURFACE2D, 0x0000, 1, {GF3_HANDLE_SURFACE2D}},
    {GF3_SUBC_SURFACE2D, 0x0180, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_SURFACE2D, 0x0184, 2, {GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA}},
    {GF3_SUBC_MISC, 0x0000, 1, {GF3_HANDLE_BETA1}},
    {GF3_SUBC_MISC, 0x0300, 1, {0x7f800000}},
    {GF3_SUBC_MISC, 0x0000, 1, {GF3_HANDLE_BETA4}},
    {GF3_SUBC_MISC, 0x0300, 1, {0xffff0000}},
    /* common.c:313-360: CGA6, 8x8, MONO. */
    {GF3_SUBC_MISC, 0x0000, 1, {GF3_HANDLE_PATTERN}},
    {GF3_SUBC_MISC, 0x0180, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_MISC, 0x0304, 3, {1, 0, 1}},
    {GF3_SUBC_MISC, 0x0000, 1, {GF3_HANDLE_ROP}},
    {GF3_SUBC_MISC, 0x0180, 1, {GF3_HANDLE_NULL}},
    /* common.c:364-398; CGA6=1 from nouveau-v6.1/nv04_fbcon.c:250. */
    {GF3_SUBC_GDI, 0x0000, 1, {GF3_HANDLE_GDI}},
    {GF3_SUBC_GDI, 0x0180, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_GDI, 0x0184, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_GDI, 0x0198, 1, {GF3_HANDLE_SURFACE2D}},
    {GF3_SUBC_GDI, 0x018c, 1, {GF3_HANDLE_ROP}},
    {GF3_SUBC_GDI, 0x0188, 1, {GF3_HANDLE_PATTERN}},
    {GF3_SUBC_GDI, 0x02fc, 1, {1}},
    {GF3_SUBC_GDI, 0x0304, 1, {1}},
    /* common.c:402-438: ROP_AND and NV15 flip contexts. */
    {GF3_SUBC_BLIT, 0x0000, 1, {GF3_HANDLE_BLIT}},
    {GF3_SUBC_BLIT, 0x0180, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_BLIT, 0x0184, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_BLIT, 0x019c, 1, {GF3_HANDLE_SURFACE2D}},
    {GF3_SUBC_BLIT, 0x0188, 3, {GF3_HANDLE_NULL, GF3_HANDLE_PATTERN, GF3_HANDLE_ROP}},
    {GF3_SUBC_BLIT, 0x02fc, 1, {1}},
    {GF3_SUBC_BLIT, 0x0120, 3, {0, 1, 2}},
    /* common.c:442-508: DITHER, SRCCOPY, then CLIP. */
    {GF3_SUBC_MISC, 0x0000, 1, {GF3_HANDLE_SIFM}},
    {GF3_SUBC_MISC, 0x0180, 7, {GF3_HANDLE_NULL, GF3_HANDLE_VRAM_DMA,
        GF3_HANDLE_NULL, GF3_HANDLE_NULL, GF3_HANDLE_BETA1, GF3_HANDLE_BETA4,
        GF3_HANDLE_SURFACE2D}},
    {GF3_SUBC_MISC, 0x02fc, 1, {0}},
    {GF3_SUBC_MISC, 0x0304, 1, {3}},
    {GF3_SUBC_MISC, 0x0000, 1, {GF3_HANDLE_CLIP}},
    {GF3_SUBC_MISC, 0x0180, 1, {GF3_HANDLE_NULL}},
    /* common.c:512-528; F3 section 3 moves M2MF to subc1. */
    {GF3_SUBC_M2MF, 0x0000, 1, {GF3_HANDLE_M2MF}},
    {GF3_SUBC_M2MF, 0x0180, 1, {GF3_HANDLE_NOTIFY}},
    /* common.c:532-578: SRCCOPY. */
    {GF3_SUBC_IFC, 0x0000, 1, {GF3_HANDLE_IFC}},
    {GF3_SUBC_IFC, 0x0180, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_IFC, 0x0188, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_IFC, 0x018c, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_IFC, 0x0190, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_IFC, 0x0194, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_IFC, 0x0198, 1, {GF3_HANDLE_NULL}},
    {GF3_SUBC_IFC, 0x019c, 1, {GF3_HANDLE_SURFACE2D}},
    {GF3_SUBC_IFC, 0x02fc, 1, {3}}
};

static bool owns_objects(const struct gf3_ctx *c)
{
    if (c->notify_storage.size || c->notify.handle || c->notify.cookie)
        return true;
    for (unsigned i = 0; i < GF3_OBJECT_COUNT; ++i)
        if (c->obj[i].handle || c->obj[i].cookie) return true;
    return false;
}

static int drain(struct gf3_ctx *c, u32 dl)
{
    struct gf2_fence fence;
    int rc = gf2_fence_emit(c->ch, &fence, dl);
    return rc ? rc : gf2_fence_wait(c->ch, fence, dl);
}

static int resume(struct gf2_chan *ch)
{
    ch->last_rc = GF2_OK;
    gf2_fifo_resume(ch);
    if (ch->last_rc) return ch->last_rc;
    return ch->state == GF2_RUN ? GF2_OK : GF2_ESTATE;
}

/* Only after drain/pause; stop immediately on failed deletion. */
static int release(struct gf3_ctx *c)
{
    int rc;
    for (unsigned i = GF3_OBJECT_COUNT; i; --i)
        if (c->obj[i-1].handle || c->obj[i-1].cookie) {
            rc = gf2_object_del(c->ch, &c->obj[i-1]);
            if (rc) return rc;
            c->obj[i-1] = (struct gf2_object){0};
        }
    if (c->notify.handle || c->notify.cookie) {
        rc = gf2_object_del(c->ch, &c->notify);
        if (rc) return rc;
    }
    /* F2 deletion/F1 free leave stale inst sizes; clear only dead nodes. */
    c->notify = (struct gf2_object){0};
    if (c->notify_storage.size) gf_inst_free(c->ch->f, &c->notify_storage);
    c->notify_storage = (struct gf_inst){0};
    c->notify_off = 0;
    return GF3_OK;
}

/* Partial initialization cannot release GPU-owned objects before F2 stop.
 * F2 releases all variable inst nodes itself; clear only after confirmed stop. */
static int failed_init(struct gf3_ctx *c, int rc)
{
    int stopped = gf2_stop(c->ch);
    if (stopped == GF2_ESTOP) return c->last_rc = stopped;
    for (unsigned i = 0; i < GF3_OBJECT_COUNT; ++i)
        c->obj[i] = (struct gf2_object){0};
    c->notify = (struct gf2_object){0};
    c->notify_storage = (struct gf_inst){0};
    c->notify_off = 0;
    return c->last_rc = rc;
}

int gf3_init(struct gf3_ctx *c, struct gf2_chan *ch, u32 dl)
{
    int rc;
    if (!c) return GF3_EINVAL;
    if (!ch || !ch->f) return c->last_rc = GF3_EINVAL;
    if (c->ready || owns_objects(c) || ch->state != GF2_RUN)
        return c->last_rc = GF3_ESTATE;
    c->ch = ch;
    c->epoch = ch->epoch;
    rc = gf2_fault_check(ch);
    if (rc) return c->last_rc = rc;
    rc = drain(c, dl);
    if (rc) return failed_init(c, rc);
    rc = gf2_fifo_pause(ch);
    if (rc) return failed_init(c, rc);
    rc = gf_inst_alloc(ch->f, GF3_NOTIFY_BYTES, GF2_OBJECT_ALIGN, false,
                       &c->notify_storage);
    if (!rc) {
        /* nvkm/subdev/instmem/nv40.c:86-89, F3 notifier addendum. */
        c->notify_off = ch->f->imem.vram_bytes - ch->f->imem.reserved +
                        c->notify_storage.off;
        rc = gf2_dma_new(ch, GF3_HANDLE_NOTIFY, GF3_CLASS_NOTIFY,
                         GF3_DMA_TARGET_VRAM, GF3_DMA_ACCESS_RW, c->notify_off,
                         c->notify_off + GF3_NOTIFY_BYTES - 1U, &c->notify);
    }
    for (unsigned i = 0; !rc && i < GF3_OBJECT_COUNT; ++i)
        rc = gf2_gr_object_new(ch, GF2_HANDLE_LAYER3_FIRST + i, classes[i],
                               &c->obj[i]);
    if (rc) {
        int cleanup = release(c);
        if (cleanup) return failed_init(c, cleanup);
        int restarted = resume(ch);
        if (restarted) return failed_init(c, restarted);
        return c->last_rc = rc;
    }
    rc = resume(ch);
    if (rc) return failed_init(c, rc);
    for (unsigned i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        rc = gf2_emit(ch, setup[i].subc, setup[i].method, false,
                       setup[i].data, setup[i].count, dl);
        if (rc) return failed_init(c, rc);
    }
    rc = drain(c, dl);
    if (rc) return failed_init(c, rc);
    c->ready = true;
    return c->last_rc = GF3_OK;
}

int gf3_fini(struct gf3_ctx *c, u32 dl)
{
    int rc;
    if (!c) return GF3_EINVAL;
    c->ready = false;
    if (!owns_objects(c)) return c->last_rc = GF3_OK;
    if (!c->ch || c->epoch != c->ch->epoch)
        return c->last_rc = GF3_ESTATE;
    rc = gf2_fault_check(c->ch);
    if (rc) return c->last_rc = rc; /* Caller hands FAULT to F2 stop. */
    if (c->ch->state != GF2_RUN) return c->last_rc = GF3_ESTATE;
    rc = drain(c, dl);
    if (!rc) rc = gf2_fifo_pause(c->ch);
    if (!rc) rc = release(c);
    if (!rc) rc = resume(c->ch);
    return c->last_rc = rc;
}
