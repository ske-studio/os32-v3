/* Layer 2 lifecycle: GF_FEAT2_DESIGN sections 2/3/4/6 and INIT/B0 addenda.
 * Gates follow MIT nouveau c03c1a1, nvkm/engine/fifo/nv04.c:53-60;
 * GR gate: nvkm/engine/gr/nv40.c:268. The composed release order, initial
 * reference test and explicit recovery are OS32's contracts. */
#include "command.h"
#include "../../include/io.h"
#include "../../include/kprintf.h"

enum {
    COMMAND_CACHES = 0x2500, COMMAND_DMA_PUSH = 0x3220,
    COMMAND_PUSH0 = 0x3200, COMMAND_PULL0 = 0x3250,
    COMMAND_GR_FIFO = 0x400720
};

static int close_gates(struct gf2_chan *ch)
{
    struct gf_io *io = &ch->f->io;
    static const struct { u32 reg; bool masked; } gates[] = {
        {COMMAND_CACHES, false}, {COMMAND_DMA_PUSH, true},
        {COMMAND_PUSH0, false}, {COMMAND_PULL0, true}, {COMMAND_GR_FIFO, true}
    };
    for (unsigned i = 0; i < sizeof(gates) / sizeof(gates[0]); i++) {
        u32 value = 0;
        if (gates[i].masked &&
            io->read(io->opaque, GF_MODE_REGISTER_BAR, gates[i].reg, 4, &value))
            return GF2_EIO;
        if (io->write(io->opaque, GF_MODE_REGISTER_BAR, gates[i].reg, 4, value & ~1U))
            return GF2_EIO;
    }
    return GF2_OK;
}

static int command_irq(struct gf2_chan *ch, bool enable)
{
    unsigned flags = irq_save();
    if (enable) gf_irq_snapshot(ch->f, &ch->seen);
    int rc = gf_irq_enable(ch->f, enable, enable, ch->heads);
    irq_restore(flags);
    return rc ? GF2_EIO : GF2_OK;
}

/* Remove every published entry, including layer 3 objects. Fixed F1
 * instances stay linked until foundation_stop; variable handles never move. */
static int release_instances(struct gf2_chan *ch)
{
    struct gf_foundation *f = ch->f;
    for (u32 slot = 0; slot < GF2_RAMHT_ENTRIES; slot++)
        if (ch->bitmap[slot / 8U] & (1U << (slot % 8U))) {
            int rc = gf2_ramht_del(ch, slot + 1U);
            if (rc) return rc;
        }
    for (struct gf_inst *inst = f->imem.used, *next; inst; inst = next) {
        next = inst->next;
        if (inst != &f->imem.vbios && inst != &f->imem.ramht &&
            inst != &f->imem.ramro && inst != &f->imem.ramfc)
            gf_inst_free(f, inst);
    }
    return GF2_OK;
}

static int command_stop(struct gf2_chan *ch, int result)
{
    ch->state = GF2_FAULT;
    if (close_gates(ch) || command_irq(ch, false) ||
        gf_engine_stop(ch->f) || release_instances(ch) ||
        gf_foundation_stop(ch->f))
        return ch->last_rc = GF2_ESTOP;
    gf2_dma_put(ch);
    if (ch->mapped && gf2_unmap_vram(ch)) return ch->last_rc = GF2_ESTOP;
    ch->state = GF2_OFF;
    return ch->last_rc = result;
}

static int reference_test(struct gf2_chan *ch)
{
    struct gf2_fence fence;
    struct gf_io *io = &ch->f->io;
    u32 now;
    if (io->timing.clock_us(io->opaque, &now)) return GF2_EIO;
    u32 deadline = now + GF2_TIMEOUT_US;
    int rc = gf2_fence_emit(ch, &fence, deadline);
    if (!rc) rc = gf2_fence_wait(ch, fence, deadline);
    return rc;
}

int gf2_start(struct gf2_chan *ch, struct gf_foundation *f)
{
    /* Caller lends BAR1 and mapping/DMA adapters through cleanup. */
    u32 available;
    int rc;
    const char *stage = "command_irq";
    if (ch->state != GF2_OFF || !f->running) return GF2_ESTATE;
    ch->f = f;
    ch->heads = f->wanted_heads; /* Preserve F1's VBLANK request. */
    ch->state = GF2_INIT;
    ch->fault = (struct gf2_fault){0};
    gf_irq_snapshot(f, &ch->seen);
    ch->seq = 0;
    ch->epoch++;
    ch->push_dma = (struct gf2_object){0};
    ch->vram_dma = (struct gf2_object){0};
    ch->pci_dma = (struct gf2_object){0};
    ch->grctx = (struct gf_inst){0};
    for (unsigned i = 0; i < sizeof(ch->bitmap); i++) ch->bitmap[i] = 0;
    rc = command_irq(ch, false);
    if (rc) goto fail;
    stage = "close_gates";
    rc = close_gates(ch);
    if (rc) goto fail;
    stage = "fb_init";
    rc = gf2_fb_init(ch);
    if (rc) goto fail;
    stage = "map_vram";
    available = f->imem.vram_bytes - f->imem.reserved;
    if (available < GF2_PUSH_BYTES) { rc = GF2_ENOSPC; goto fail; }
    ch->B = (available - GF2_PUSH_BYTES) & ~(GF2_PUSH_ALIGN - 1U);
    if (ch->B < GF2_PUSH_MIN_BASE) { rc = GF2_ENOSPC; goto fail; }
    rc = gf2_map_vram(ch, ch->B, GF2_PUSH_BYTES, &ch->va);
    if (rc) goto fail;
    /* Section 3 / nouveau_chan.c:439-478: initial free does not wait on GET. */
    ch->cur = ch->put = 0;
    ch->max = GF2_PUSH_MAX;
    while (ch->cur < GF2_PUSH_SKIPS) ch->va[ch->cur++] = 0;
    ch->free = ch->max - ch->cur;
    stage = "dma_new";
    rc = gf2_dma_new(ch, GF2_HANDLE_PUSH_DMA, GF2_DMA_FROM_MEMORY,
                     GF2_DMA_TARGET_VRAM, GF2_DMA_ACCESS_RW, 0, available - 1U,
                     &ch->push_dma);
    if (!rc) rc = gf2_dma_new(ch, GF2_HANDLE_VRAM_DMA, GF2_DMA_IN_MEMORY,
                              GF2_DMA_TARGET_VRAM, GF2_DMA_ACCESS_RW,
                              0, available - 1U, &ch->vram_dma);
    if (rc) goto fail;
    stage = "dma_get";
    rc = gf2_dma_get(ch);
    if (!rc) {
        stage = "dma_new";
        rc = gf2_dma_new(ch, GF2_HANDLE_PCI_DMA, GF2_DMA_IN_MEMORY,
                         GF2_DMA_TARGET_PCI, GF2_DMA_ACCESS_RW,
                         ch->staging.pa, ch->staging.pa + ch->staging.size - 1U,
                         &ch->pci_dma);
    } else if (rc == GF2_ENOSPC) rc = GF2_OK; /* VRAM remains available. */
    if (rc) goto fail;
    stage = "fifo_init";
    rc = gf2_fifo_init(ch);
    if (rc) goto fail;
    stage = "gr_init";
    rc = gf2_gr_init(ch); /* includes RAMFC+38 bind */
    if (rc) goto fail;
    stage = "fifo_start";
    rc = gf2_fifo_start(ch);
    if (rc) goto fail;
    stage = "command_irq";
    rc = command_irq(ch, true);
    if (rc) goto fail;
    stage = "reference_test";
    rc = reference_test(ch);
    if (rc) goto fail;
    ch->state = GF2_RUN;
    return ch->last_rc = GF2_OK;
fail:
    kprintf(KLOG_ATTR, "[gf2] start fail stage=%s rc=%d\n", stage, rc);
    return command_stop(ch, rc);
}

int gf2_stop(struct gf2_chan *ch)
{
    if (ch->state == GF2_OFF) return GF2_OK;
    int rc = gf2_fault_check(ch);
    if (!rc && ch->state == GF2_RUN) {
        ch->state = GF2_DRAIN;
        rc = reference_test(ch);
        if (!rc) {
            ch->last_rc = GF2_OK;
            gf2_fifo_stop(ch);
            rc = ch->last_rc;
        }
    }
    return command_stop(ch, rc);
}

int gf2_recover(struct gf2_chan *ch)
{
    int rc = gf2_stop(ch);
    if (rc == GF2_ESTOP) return rc;
    if (gf_foundation_start(ch->f)) {
        ch->state = GF2_FAULT; /* retain a retry path if F1 rollback failed */
        return ch->last_rc = GF2_ESTOP;
    }
    return gf2_start(ch, ch->f);
}
