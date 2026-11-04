/* Production startup: docs/design/hwdev/gf/devinit.md. A2 order is the existing
 * recover_gf_prepare_plan (recover_gf.c:410), without plan/approval machinery.
 * main0..4, baseline, calc, load_state and required CHECK remain in service.c. */
#include "production.h"
#include "../../drivers/pci/pci.h"
#include "../../lib/serial_diag.h"
#include "../../mm/pgalloc.h"
#include "../../lib/kstring.h"
#include "../../lib/crc32_core.inc"
#include "rom.inc"
static int same_resources(struct gf_production *p)
{
    struct gf_production_resources current;
    if (gf_production_identity(p)) return -1;
    if (gf_production_identify(&current)) {
        /* Recorded only: the 98 decode is not proven safe here (§4-3). */
        if (current.reject.reason) {
            p->reject=current.reject;
            p->display.target=GF_PRODUCTION_REJECT_TARGET(&current.reject);
            p->display.last_rc=-1;
        }
        return -1;
    }
    for (u32 i=0;i<RECOVER_BAR_COUNT;i++) {
        const struct recover_bar_extent *b=&p->display.bars[i];
        if (current.bars[i].raw!=b->raw || current.bars[i].base!=b->base ||
            current.bars[i].length!=b->length || !b->valid) return -1;
    }
    return gf_production_mapped(p);
}
/* IRQ/tick use the permanent BAR0 map: no PCI scan, waits or VGA windows.
 * nouveau nvkm/subdev/mc/nv04.c:94 nv04_mc_intr_pending uses direct MMIO. */
static int foundation_read(void *opaque, u32 bar, u32 off, u32 width, u32 *value)
{
    struct gf_foundation *f=opaque;
    (void)bar;(void)width;
    if (f->revoked) return -1;
    *value=*(volatile u32 *)(f->bar0+off);
    return 0;
}
static int foundation_write(void *opaque, u32 bar, u32 off, u32 width, u32 value)
{
    struct gf_foundation *f=opaque;
    (void)bar;(void)width;
    if (f->revoked) return -1;
    *(volatile u32 *)(f->bar0+off)=value;
    return 0;
}
static int foundation_result(struct gf_display_context *c, int rc)
{
    if (rc && !serial_gate_active()) {
        serial_puts_polled("[gf1] lifecycle rc=");serial_diag_i32(rc);
        serial_puts_polled("\r\n");
    }
    struct gf_production *p=c->io.opaque;
    p->foundation.last_rc=rc;
    c->last_rc=rc;
    return rc ? -1 : 0;
}
static int foundation_start(struct gf_display_context *c)
{
    struct gf_production *p=c->io.opaque;
    if (p->foundation.revoked || same_resources(p))
        return foundation_result(c,GF1_EPCI);
    p->chan.map_ops=p->map_ops;
    p->chan.map_owner=p->map_owner;
    p->chan.bar1=c->bars[GF_MODE_VRAM_BAR];
    p->chan.dma_ops=geforce_dma_ops();
    p->foundation.instmem_bar=c->bars[2];
    p->foundation.map_ops=p->map_ops;
    p->foundation.map_owner=p->map_owner;
    return foundation_result(c,gf_foundation_start(&p->foundation));
}
static int foundation_stop(struct gf_display_context *c)
{
    struct gf_production *p=c->io.opaque;
    /* Stop must still unbind tick/unmap after an identity or BAR revoke.
     * nouveau_drm.c:933-941 tears down before disabling PCI. */
    (void)gf_production_identity(p);
    int rc=gf_foundation_stop(&p->foundation);
    /* Revoked display cannot be retried; quiesce still attempts every rejected
     * cleanup row (design P2-b). Ordinary stop failures keep engaged. */
    if (p->foundation.revoked) c->engaged=false;
    return foundation_result(c,rc);
}
static struct gf2_chan *command_get(struct gf_display_context *c,
                                    struct gf_foundation **out)
{
    struct gf_production *p=c->io.opaque;
    *out=&p->foundation;
    return &p->chan;
}
int gf_production_prepare(struct gf_production *p,
                          const struct gf_production_resources *boot,
                          int (*route98)(void *), void *opaque)
{
    if (!p || !boot || !route98 || p->prepared || p->attempted) return -1;
    p->display.identity=boot->identity;
    for (u32 i=0;i<RECOVER_BAR_COUNT;i++) p->display.bars[i]=boot->bars[i];
    p->route98=route98;p->route_opaque=opaque;
    p->display.io=(struct gf_io){p,gf_production_read,gf_production_write,
        {gf_production_clock,gf_production_wait},gf_production_point,gf_production_restore,0};
    p->display.owner=&gf_production_owner;
    p->foundation.io=(struct gf_io){.opaque=&p->foundation,
        .read=foundation_read,.write=foundation_write};
    p->foundation.expected_command=RECOVER_COMMAND_STOP;
    p->map_ops=&gf_production_instmem_map_ops;p->map_owner=p;
    p->video=(struct video_device){&p->display,&gf_video_ops};
    if (same_resources(p)) return -1;
    p->display.bar0_map=(void *)P2V_IO(p->display.bars[0].base);
    p->foundation.bar0=p->display.bar0_map;
    p->display.bar1_map=(void *)P2V_IO(p->display.bars[GF_MODE_VRAM_BAR].base);
    p->prepared=true;p->display.reusable=true;
    return 0;
}
static int read_rom(struct gf_production *p)
{
    u8 *rom=p->scratch->rom;
    for (u32 pass=0;pass<2;pass++)
        for (u32 off=0;off<GF_ROM_MAX;off++) {
            u32 value;
            if (!(off%PAGE_SIZE) && gf_production_point(p)) return -1;
            if (gf_production_read(p,0,KPEEK_NV_PROM_OFF+off,1,&value)) return -1;
            if (pass && rom[off]!=(u8)value) return -1;
            rom[off]=(u8)value;
        }
    u32 chip;
    struct gf_display_context *c=&p->display;
    if (gf_rom_validate(rom,&c->rom_size,&c->rom_crc,&chip)) return -1;
    c->init->rom=rom;c->init->rom_size=c->rom_size;
    return gfinit_pll_parse(rom,c->rom_size,GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&c->limits) ||
           gf_init_prepare(c->init,c->program) ? -1 : 0;
}
static int a2_decode_safe(void *opaque)
{ return gf_production_decode_safe(opaque); }
static int validate(struct gf_display_context *c);
static int acquire(struct gf_display_context *c)
{
    struct gf_production *p=c->io.opaque;
    if (c->initialized) {
        /* C2-links-init: runtime ROM loan, not a second boot attempt.
         * GF_FEAT4_DESIGN TMDS addendum N1: no PMC readback comparison.
         * decode_safe checks PBUS1850/1854 before any PROM byte. */
        if (!p->prepared || validate(c)) return -1;
    } else {
        if (p->attempted || !p->prepared) return -1;
        p->attempted=true;
        if (p->route98(p->route_opaque) || same_resources(p)) return -1;
        /* nouveau_drm.c:858,881 nouveau_drm_probe: master before devinit. */
        if (foundation_result(c,gf_master_enable(&p->foundation))) return -1;
    }
    u32 started;
    if (gf_production_clock(p,&started)) return -1;
    u32 phys=pgalloc_alloc_phys(LEDGER_OWNER_KERNEL,PAGE_ALIGN_UP(sizeof(*p->scratch))/PAGE_SIZE);
    if (!phys) return -1;
    p->scratch=P2V(phys);
    kmemset(p->scratch,0,sizeof(*p->scratch));
    c->init=&p->scratch->init;c->program=&p->scratch->program;
    *c->init=(struct gf_init_context){.io=c->io,.started_us=started};
    p->acquiring=true;
    if (read_rom(p)) return -1;
    return c->initialized ? 0 : gf_init_a2(c->init,a2_decode_safe);
}
static void release(struct gf_display_context *c)
{
    struct gf_production *p=c->io.opaque;
    p->acquiring=false;
    if (!c->initialized && c->baseline_valid)
        p->foundation.expected_pmc=c->baseline_pmc;
    if (p->scratch)
        (void)pgalloc_free_n_owner(LEDGER_OWNER_KERNEL,V2P(p->scratch)/PAGE_SIZE,
                                   PAGE_ALIGN_UP(sizeof(*p->scratch))/PAGE_SIZE);
    p->scratch=0;c->init=0;c->program=0;
}
static int validate(struct gf_display_context *c)
{
    struct gf_production *p=c->io.opaque;
    u32 amount;
    if (gf_production_decode_safe(p) ||
        gf_production_read(p,0,GF_PRODUCTION_PFB_AMOUNT,4,&amount)) return -1;
    amount&=GF_PRODUCTION_PFB_MASK;
    return amount<GF_PRODUCTION_PFB_MIN || (amount&(amount-1)) ||
        amount<GF_MODE_VRAM_BYTES || gf_production_point(p) ? -1 : 0;
}
const struct gf_display_owner gf_production_owner={
    .acquire=acquire, .release=release, .validate=validate,
    .foundation_start=foundation_start, .foundation_stop=foundation_stop,
    .command_get=command_get
};
