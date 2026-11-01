/* Layer 1 lifecycle. MIT nouveau references below are relative to
 * drivers/gpu/drm/nouveau/; the composed stop order is OS32 policy. */
#include "foundation.h"
#include "../../include/io.h"
#include "../../drivers/video/geforce/geforce_tick.h"

#define GF1_PMC_IRQ_ENABLE 0x000140U
#define GF1_IRQ_ALL 0xffffffffU
#define GF1_HEAD_A 1U

/* nv04_mc_intr_unarm, nvkm/subdev/mc/nv04.c:82-92;
 * nv04_display_fini, dispnv04/disp.c:57-69. Close the aggregate before
 * closing/ACKing sources. PMC status itself is never ACKed. */
static int mask_sources(struct gf_foundation *f)
{
    static const struct { u32 enable, status, ack; } sources[] = {
        {0x002140U, 0x002100U, GF1_IRQ_ALL},
        {0x40013cU, 0x400100U, GF1_IRQ_ALL},
        {0x600140U, 0x600100U, 1U},
        {0x602140U, 0x602100U, 1U},
        {0x008140U, 0x008100U, GF1_IRQ_ALL},
        {0x009140U, 0x009100U, GF1_IRQ_ALL},
        {0x001140U, 0x001100U, GF1_IRQ_ALL},
        {0x00b140U, 0x00b100U, GF1_IRQ_ALL},
    };
    u32 value;
    if (f->io.write(f->io.opaque, GF_MODE_REGISTER_BAR, GF1_PMC_IRQ_ENABLE,
                    sizeof(value), 0) ||
        f->io.read(f->io.opaque, GF_MODE_REGISTER_BAR, GF1_PMC_IRQ_ENABLE,
                   sizeof(value), &value)) return GF1_EIO;
    for (unsigned i = 0; i < sizeof(sources) / sizeof(sources[0]); i++)
        if (f->io.write(f->io.opaque, GF_MODE_REGISTER_BAR, sources[i].enable,
                        sizeof(value), 0) ||
            f->io.write(f->io.opaque, GF_MODE_REGISTER_BAR, sources[i].status,
                        sizeof(value), sources[i].ack)) return GF1_EIO;
    return GF1_OK;
}

/* nv40_instmem_oneinit, nvkm/subdev/instmem/nv40.c:183-206 supplies
 * the fixed regions. Variable handles precede their release; fixed handles
 * are private to foundation and cannot be released via gf_inst_free. */
static void release_instances(struct gf_foundation *f)
{
    struct gf_inst *obj = f->imem.used;
    while (obj) {
        struct gf_inst *next = obj->next;
        gf_inst_free(f, obj);
        obj = next;
    }
    f->imem.used = 0;
    f->imem.ramfc = (struct gf_inst){0};
    f->imem.ramro = (struct gf_inst){0};
    f->imem.ramht = (struct gf_inst){0};
    f->imem.vbios = (struct gf_inst){0};
}

/* nouveau_drm_remove, nouveau_drm.c:933-941: device teardown precedes
 * PCI disable. OS32 also unbinds its resident tick before IRQ/map release.
 * Run the hardware stop even without bm: a failed master readback can leave
 * Command changed before master.c has published the successful BM state. */
int gf_foundation_stop(struct gf_foundation *f)
{
    unsigned flags = irq_save();
    int rc;
    f->running = false;
    rc = geforce_tick_bind(0, 0) ? GF1_EIRQ : mask_sources(f);
    irq_restore(flags);
    if (rc) return f->last_rc = rc;
    rc = gf_engine_stop(f);
    if (rc) return f->last_rc = rc;
    /* OS32 stage validation: reset may have changed PMC even before MC init.
     * nouveau has no expected_pmc; publish only the successful readback. */
    u32 stopped_pmc;
    if (f->io.read(f->io.opaque, GF_MODE_REGISTER_BAR, RECOVER_PMC_ENABLE,
                   sizeof(stopped_pmc), &stopped_pmc))
        return f->last_rc = GF1_EIO;
    f->expected_pmc = stopped_pmc;
    rc = gf_irq_detach(f);
    if (rc) return f->last_rc = rc;
    rc = gf_master_disable(f);
    if (rc) return f->last_rc = rc;
    release_instances(f);
    if (f->mapped) {
        rc = gf_instmem_unmap(f);
        if (rc) return f->last_rc = rc;
    }
    return f->last_rc = GF1_OK;
}

/* nouveau_drm_probe, nouveau_drm.c:858-896: enable master before device
 * initialization. B0 completes Command/expected_command publication without
 * an intervening MMIO operation. MC must see all IRQ sources closed. */
int gf_foundation_start(struct gf_foundation *f)
{
    int rc = gf_master_enable(f);
    if (!rc) rc = mask_sources(f);
    if (!rc) rc = gf_mc_init(f);
    if (!rc) rc = gf_instmem_init(f);
    if (!rc && geforce_tick_bind(gf_tick_poll, f)) rc = GF1_EIRQ;
    if (!rc) rc = gf_irq_attach(f);
    if (!rc) {
        unsigned flags = irq_save();
        f->running = true;
        rc = gf_irq_enable(f, true, true, GF1_HEAD_A);
        irq_restore(flags);
    }
    if (rc) {
        int stop_rc = gf_foundation_stop(f);
        if (stop_rc) rc = stop_rc;
    }
    return f->last_rc = rc;
}

/* Publish loss of the hardware lease without touching the old device. */
void gf_foundation_revoke(struct gf_foundation *f)
{
    unsigned flags = irq_save();
    f->revoked = true;
    f->running = false; /* revoke publication */
    irq_restore(flags);
}
