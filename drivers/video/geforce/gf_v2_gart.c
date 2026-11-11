/* HAL_V2_DESIGN §11-6 G5: NV44A uses nv04 paged PCI ctxdma.
 * MIT nouveau: nvkm/subdev/mmu/vmmnv04.c:45-77,135-139;
 * nvkm/engine/dma/usernv04.c:51-55. No MMIO or TLB flush. */
#include "gf_v2.h"
#include "include/memmap.h"

enum {
    GART_HEADER_BYTES = 8, GART_PTE_BYTES = 4, GART_PAGE_SHIFT = 12,
    GART_DMA_HEADER = 0x0002103d, GART_DMA_LIMIT = 0x07ffffff,
    GART_PTE_FLAGS = 3
};

static bool live(const struct gf_v2 *g)
{
    return g->gart.epoch != 0 && g->gart.epoch == g->ch->epoch &&
           g->ch->state == GF2_RUN;
}

static int pte_write(struct gf_v2 *g, u32 page, u32 value)
{
    return gf_inst_write32(g->ch->f, &g->gart.obj.inst,
                          GART_HEADER_BYTES + GART_PTE_BYTES * page, value);
}

static int resume(struct gf2_chan *ch)
{
    ch->last_rc = 0;
    gf2_fifo_resume(ch);
    if (ch->last_rc) return ch->last_rc;
    return ch->state == GF2_RUN ? 0 : GF2_ESTATE;
}

int gf_v2_gart_start(struct gf_v2 *g, u32 dl)
{
    struct gf2_chan *ch = g->ch;
    struct gf2_object *obj = &g->gart.obj;
    struct gf2_fence fence;
    int rc, restarted;
    if (live(g)) return 0;
    if (ch->state != GF2_RUN) return GF3_ESTATE;
    /* The previous epoch's instance was already freed by F2 stop. */
    *obj = (struct gf2_object){0};
    g->gart.epoch = 0;
    rc = gf2_fence_emit(ch, &fence, dl);
    if (!rc) rc = gf2_fence_wait(ch, fence, dl);
    if (rc) return rc;
    rc = gf2_fifo_pause(ch);
    if (rc) return rc;
    rc = gf_inst_alloc(ch->f, GF_V2_GART_PT_BYTES, MEM_PAGE_SIZE, true,
                       &obj->inst);
    if (rc) {
        rc = rc == GF1_ENOSPC ? GF2_ENOSPC : rc;
        goto fail;
    }
    rc = gf_inst_write32(ch->f, &obj->inst, 0, GART_DMA_HEADER);
    if (!rc) rc = gf_inst_write32(ch->f, &obj->inst, 4, GART_DMA_LIMIT);
    if (rc) goto fail;
    for (u32 i = 0; i < g->gart.count; i++) {
        const struct gf_v2_gart_bind *b = &g->gart.bind[i];
        for (u32 j = 0; j < b->n; j++) {
            rc = pte_write(g, b->first + j,
                           (b->pfn[j] << GART_PAGE_SHIFT) | GART_PTE_FLAGS);
            if (rc) goto fail;
        }
    }
    rc = gf2_ramht_add(ch, GF3_HANDLE_GART_DMA, GF2_RAMHT_ENGINE_DMA,
                       obj->inst.off, &obj->cookie);
    if (rc) goto fail;
    obj->handle = GF3_HANDLE_GART_DMA;
    obj->class_id = GF2_DMA_IN_MEMORY;
    rc = resume(ch);
    if (rc) return rc; /* F2 stop owns the published instance. */
    g->gart.epoch = ch->epoch;
    return 0;
fail:
    if (obj->inst.size) gf_inst_free(ch->f, &obj->inst);
    *obj = (struct gf2_object){0};
    restarted = resume(ch);
    return restarted ? restarted : rc;
}

int gf_v2_gart_bind(struct gf_v2 *g, const u32 *pfn, u32 n, u32 *gpu_off)
{
    struct gf_v2_gart *t = &g->gart;
    u32 first = 0, best = GF_V2_GART_PAGES + 1u, chosen = 0, insert = 0;
    if (!pfn || !n || !gpu_off) return OS32_ERR_INVAL;
    if (!live(g)) return OS32_ERR_IO;
    if (n > GF_V2_GART_PAGES || t->count == GF_V2_GART_BINDS)
        return OS32_ERR_NOSPC;
    for (u32 i = 0; i <= t->count; i++) {
        u32 end = i < t->count ? t->bind[i].first : GF_V2_GART_PAGES;
        u32 gap = end - first;
        if (gap >= n && gap < best) {
            best = gap; chosen = first; insert = i;
        }
        if (i < t->count) first = end + t->bind[i].n;
    }
    if (best > GF_V2_GART_PAGES) return OS32_ERR_NOSPC;
    for (u32 j = 0; j < n; j++) {
        if (pte_write(g, chosen + j,
                       (pfn[j] << GART_PAGE_SHIFT) | GART_PTE_FLAGS)) {
            for (u32 k = 0; k < j; k++)
                (void)pte_write(g, chosen + k, 0);
            return OS32_ERR_IO;
        }
    }
    for (u32 i = t->count; i > insert; i--) t->bind[i] = t->bind[i - 1];
    t->bind[insert] = (struct gf_v2_gart_bind){chosen, n, pfn};
    t->count++;
    *gpu_off = chosen << GART_PAGE_SHIFT;
    return 0;
}

int gf_v2_gart_unbind(struct gf_v2 *g, u32 gpu_off, u32 n)
{
    struct gf_v2_gart *t = &g->gart;
    if (gpu_off & (MEM_PAGE_SIZE - 1u)) return OS32_ERR_INVAL;
    for (u32 i = 0; i < t->count; i++) {
        const struct gf_v2_gart_bind *b = &t->bind[i];
        if (b->first != (gpu_off >> GART_PAGE_SHIFT) || b->n != n) continue;
        if (live(g))
            for (u32 j = 0; j < n; j++) (void)pte_write(g, b->first + j, 0);
        for (u32 j = i + 1; j < t->count; j++) t->bind[j - 1] = t->bind[j];
        t->bind[--t->count] = (struct gf_v2_gart_bind){0};
        return 0;
    }
    return OS32_ERR_INVAL;
}

int gf_v2_gart_range(void *opaque, u32 handle, u32 off, u32 bytes)
{
    const struct gf_v2 *g = opaque;
    if (handle != GF3_HANDLE_GART_DMA) return GF3_EINVAL;
    if (!live(g)) return GF3_ERANGE;
    for (u32 i = 0; i < g->gart.count; i++) {
        const struct gf_v2_gart_bind *b = &g->gart.bind[i];
        u32 end = (b->first + b->n) << GART_PAGE_SHIFT;
        if (off >= (b->first << GART_PAGE_SHIFT) && off <= end &&
            bytes <= end - off) return 0;
    }
    return GF3_ERANGE;
}

void gf_v2_gart_reset(struct gf_v2 *g)
{
    g->gart = (struct gf_v2_gart){0};
}
