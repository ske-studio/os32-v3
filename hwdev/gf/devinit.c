/* Production startup: GF_DRIVER_DESIGN §4-2. A2 order is the existing
 * recover_gf_prepare_plan (recover_gf.c:410), without plan/approval machinery.
 * main0..4, baseline, calc, load_state and required CHECK remain in service.c. */
#include "production.h"
#include "../../drivers/pci/pci.h"
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
    p->video=(struct video_device){&p->display,&gf_video_ops};
    if (same_resources(p)) return -1;
    p->display.bar0_map=(void *)P2V_IO(p->display.bars[0].base);
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
static int acquire(struct gf_display_context *c)
{
    struct gf_production *p=c->io.opaque;
    if (p->attempted || !p->prepared) return -1;
    p->attempted=true;
    if (p->route98(p->route_opaque) || same_resources(p)) return -1;
    /* N1: one WORD write, then raw WORD compare. Never a DWORD RMW or BAR write. */
    if (pci_cfg_write16(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,
                        PCI_CFG_COMMAND,RECOVER_COMMAND_OVERRIDE) ||
        pci_cfg_read16(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,
                        PCI_CFG_COMMAND)!=RECOVER_COMMAND_OVERRIDE) return -1;
    u32 started;
    if (gf_production_clock(p,&started)) return -1;
    u32 phys=pgalloc_alloc_phys(LEDGER_OWNER_KERNEL,PAGE_ALIGN_UP(sizeof(*p->scratch))/PAGE_SIZE);
    if (!phys) return -1;
    p->scratch=P2V(phys);
    kmemset(p->scratch,0,sizeof(*p->scratch));
    c->init=&p->scratch->init;c->program=&p->scratch->program;
    *c->init=(struct gf_init_context){.io=c->io,.started_us=started};
    p->acquiring=true;
    return read_rom(p) || gf_init_a2(c->init,a2_decode_safe) ? -1 : 0;
}
static void release(struct gf_display_context *c)
{
    struct gf_production *p=c->io.opaque;
    p->acquiring=false;
    if (p->scratch)
        (void)pgalloc_free_n_owner(LEDGER_OWNER_KERNEL,V2P(p->scratch)/PAGE_SIZE,
                                   PAGE_ALIGN_UP(sizeof(*p->scratch))/PAGE_SIZE);
    p->scratch=0;c->init=0;c->program=0;
}
static int validate(struct gf_display_context *c)
{
    struct gf_production *p=c->io.opaque;
    u32 amount,pmc;
    if (gf_production_decode_safe(p) ||
        gf_production_read(p,0,RECOVER_PMC_ENABLE,4,&pmc) ||
        (c->initialized && pmc!=c->baseline_pmc) ||
        gf_production_read(p,0,GF_PRODUCTION_PFB_AMOUNT,4,&amount)) return -1;
    amount&=GF_PRODUCTION_PFB_MASK;
    return amount<GF_PRODUCTION_PFB_MIN || (amount&(amount-1)) ||
        amount<GF_MODE_VRAM_BYTES || gf_production_point(p) ? -1 : 0;
}
const struct gf_display_owner gf_production_owner={acquire,release,validate};
