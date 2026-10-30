/* BAR-relative bus, preserving local index/lock state. Register/cleanup order
 * follows kdebug/gfinit_bus.c; no diagnostic executor or real VGA port I/O. */
#include "production.h"
#include "../../drivers/pci/pci.h"
int gf_production_identity(struct gf_production *p)
{
    const struct recover_identity *id=&p->display.identity;
    if (p->revoked ||
        pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_VENDOR_ID)!=id->device ||
        pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_SUBSYS_VENDOR)!=id->subsys ||
        (pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_REVISION_ID)&PCI_REVISION_MASK)!=id->revision)
        goto bad;
    for (u32 i=0;i<RECOVER_BAR_COUNT;i++)
        if (pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_BAR0+i*PCI_CFG_BAR_STRIDE)!=p->display.bars[i].raw)
            goto bad;
    return 0;
bad:
    p->revoked=true;p->display.reusable=false;
    return -1;
}
static int raw(struct gf_production *p, u32 bar, u32 off, u32 width, bool write, u32 *value)
{
    if (!p->prepared || !value || bar>=RECOVER_BAR_COUNT ||
        (width!=1 && width!=2 && width!=4) || off%width ||
        off>p->display.bars[bar].length || width>p->display.bars[bar].length-off ||
        (bar!=GF_MODE_REGISTER_BAR && (bar!=GF_MODE_VRAM_BAR ||
         off>GF_MODE_VRAM_BYTES || width>GF_MODE_VRAM_BYTES-off)) ||
        gf_production_identity(p) ||
        pci_cfg_read16(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_COMMAND)!=RECOVER_COMMAND_OVERRIDE)
        return -1;
    return gf_production_mmio(p,bar,off,width,write,value);
}
int gf_production_read(void *v, u32 bar, u32 off, u32 width, u32 *value)
{ return raw(v,bar,off,width,false,value); }
static int save_local(struct gf_production *p, struct gf_production_local *table,
                       u32 count, u32 alias, u32 index, u32 offset)
{
    struct gf_production_local *free=0;
    for (u32 i=0;i<count;i++) {
        if (table[i].valid && table[i].alias==alias && table[i].index==index) return 0;
        if (!table[i].valid) free=&table[i];
    }
    u32 value;
    if (!free || raw(p,0,offset,1,false,&value)) return -1;
    *free=(struct gf_production_local){alias,index,value,true};
    return 0;
}
int gf_production_write(void *v, u32 bar, u32 off, u32 width, u32 value)
{
    struct gf_production *p=v;
    if (bar==GF_MODE_REGISTER_BAR && width==1) {
        for (u32 head=0;head<2;head++) {
            u32 cr=RECOVER_PRMCIO+head*RECOVER_HEAD_STRIDE+RECOVER_CRTC;
            const u32 aliases[]={cr,RECOVER_PRMVIO+head*RECOVER_HEAD_STRIDE+RECOVER_SEQ,
                               RECOVER_PRMVIO+head*RECOVER_HEAD_STRIDE+RECOVER_GFX};
            for (u32 i=0;i<3;i++)
                if (off==aliases[i] && save_local(p,p->indexes,GF_PRODUCTION_INDEXES,off,RECOVER_NONE,off)) return -1;
            if (off==cr+1) {
                u32 index;
                if (raw(p,0,cr,1,false,&index)) return -1;
                if ((index==RECOVER_CR1F || index==RECOVER_CR11 || index==RECOVER_CR44) &&
                    save_local(p,p->locks,GF_PRODUCTION_LOCKS,cr,index,off)) return -1;
            }
        }
    }
    return raw(p,bar,off,width,true,&value);
}
int gf_production_restore(void *v, struct gf_init_context *ctx)
{
    struct gf_production *p=v;
    int rc=gf_init_restore(ctx);
    /* Don't retry failed local writes. Even after table/lock failure attempt
     * the rest, including index restoration; identity guard remains active. */
    const u32 order[]={RECOVER_CR1F,RECOVER_CR11,RECOVER_CR44,RECOVER_CR1F};
    for (u32 k=0;k<sizeof(order)/sizeof(order[0]);k++)
        for (u32 i=0;i<GF_PRODUCTION_LOCKS;i++) {
            struct gf_production_local *s=&p->locks[i];
            if (!s->valid || s->index!=order[k]) continue;
            u32 index=s->index, value=s->value;
            if (index==RECOVER_CR1F)
                value=!k || value==RECOVER_CR1F_UNLOCKED ? RECOVER_CR1F_UNLOCK : RECOVER_CR1F_LOCK;
            if (raw(p,0,s->alias,1,true,&index)) rc=-1;
            else if (raw(p,0,s->alias+1,1,true,&value)) rc=-1;
        }
    for (u32 i=0;i<GF_PRODUCTION_LOCKS;i++) p->locks[i].valid=false;
    for (u32 i=0;i<GF_PRODUCTION_INDEXES;i++) {
        struct gf_production_local *s=&p->indexes[i];
        if (s->valid && raw(p,0,s->alias,1,true,&s->value)) rc=-1;
        s->valid=false;
    }
    if (rc) p->display.reusable=false;
    return rc ? -1 : 0;
}
int gf_production_point(void *v)
{
    struct gf_production *p=v;
    u32 now;
    if (gf_production_identity(p)) return -1;
    if (!p->acquiring) return 0;
    struct gf_init_context *ctx=p->display.init;
    if (!ctx || gf_production_clock(p,&now)) return -1;
    ctx->elapsed_us=now-ctx->started_us;
    return ctx->elapsed_us>=GF_INIT_EXEC_US ? -1 : 0;
}
int gf_production_decode_safe(struct gf_production *p)
{
    u32 value;
    if (!p || !p->prepared || gf_production_identity(p) ||
        pci_cfg_read16(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_COMMAND)!=RECOVER_COMMAND_OVERRIDE ||
        pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,RECOVER_CFG50) ||
        pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,RECOVER_CFG54)) return -1;
    if (raw(p,0,RECOVER_PBUS1850,4,false,&value) || value ||
        raw(p,0,RECOVER_PBUS1854,4,false,&value) || value) return -1;
    return 0;
}
int gf_production_quiesce(struct gf_production *p)
{
    if (!p) return -1;
    return p->prepared ? video_stop(&p->video) : 0;
}
