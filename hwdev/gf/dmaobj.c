#include "command.h"

/* Descriptor encoding: nvkm/engine/dma/usernv04.c:41-69,104-130. */
enum {
    DMA_VRAM_FLAGS = 0x00003000U, DMA_PCI_FLAGS = 0x00023000U,
    DMA_RO_FLAGS = 0x00004000U, DMA_WO_FLAGS = 0x00008000U,
    DMA_WRITE_FLAGS = 2U, DMA_PAGE_MASK = 0xfffff000U,
    DMA_ADJUST_MASK = 0xfffU, DMA_ADJUST_SHIFT = 20
};

int gf2_dma_new(struct gf2_chan *c, u32 handle, u16 class_id,
                u8 target, u8 access, u32 start, u32 limit,
                struct gf2_object *obj)
{
    u32 flags0 = class_id, flags2 = 0, base;
    int rc;

    if (!handle || (class_id != GF2_DMA_FROM_MEMORY &&
        class_id != GF2_DMA_TO_MEMORY && class_id != GF2_DMA_IN_MEMORY) ||
        start > limit) return GF2_EINVAL;
    if (target == GF2_DMA_TARGET_VRAM) flags0 |= DMA_VRAM_FLAGS;
    else if (target == GF2_DMA_TARGET_PCI) flags0 |= DMA_PCI_FLAGS;
    else return GF2_EINVAL;
    if (access == GF2_DMA_ACCESS_RO) flags0 |= DMA_RO_FLAGS;
    else if (access == GF2_DMA_ACCESS_WO) {
        flags0 |= DMA_WO_FLAGS;
        flags2 = DMA_WRITE_FLAGS;
    } else if (access == GF2_DMA_ACCESS_RW) flags2 = DMA_WRITE_FLAGS;
    else return GF2_EINVAL;

    rc = gf_inst_alloc(c->f, GF2_DMA_OBJECT_BYTES, GF2_OBJECT_ALIGN, false,
                       &obj->inst);
    if (rc) return rc == GF1_ENOSPC ? GF2_ENOSPC : GF2_EIO;
    obj->handle = handle;
    obj->class_id = class_id;
    obj->cookie = 0;
    base = (start & DMA_PAGE_MASK) | flags2;
    rc = gf_inst_write32(c->f, &obj->inst, 0, flags0 |
                         ((start & DMA_ADJUST_MASK) << DMA_ADJUST_SHIFT));
    if (!rc) rc = gf_inst_write32(c->f, &obj->inst, 4, limit - start);
    if (!rc) rc = gf_inst_write32(c->f, &obj->inst, 8, base);
    if (!rc) rc = gf_inst_write32(c->f, &obj->inst, 12, base);
    if (rc) {
        gf_inst_free(c->f, &obj->inst);
        obj->handle = 0;
        return GF2_EIO;
    }
    if (handle != GF2_HANDLE_PUSH_DMA) {
        rc = gf2_ramht_add(c, handle, GF2_RAMHT_ENGINE_DMA, obj->inst.off,
                           &obj->cookie);
        if (rc) {
            gf_inst_free(c->f, &obj->inst);
            obj->handle = 0;
            return rc;
        }
    }
    return GF2_OK;
}

int gf2_object_del(struct gf2_chan *c, struct gf2_object *obj)
{
    int rc;
    if (obj->cookie) {
        rc = gf2_ramht_del(c, obj->cookie);
        if (rc) return rc;
        obj->cookie = 0;
    }
    gf_inst_free(c->f, &obj->inst);
    obj->handle = 0;
    return GF2_OK;
}
