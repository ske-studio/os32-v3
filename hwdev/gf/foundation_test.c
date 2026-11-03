/* Foreground layer-1 bridge; serial command policy belongs to C9. */
#include "gf1_test.h"
#include "gf2_test.h"
#include "../../kdebug/gfinit_bus.h"
#include "../../include/memmap.h"
#include "../../include/io.h"
#include "../../drivers/video/geforce/geforce_module.h"

/* The lease bus owns the diagnostic foundation. IRQ/tick only use its
 * permanent pointer, never PCI checks or the recover window. */
static int diagnostic_read(void *opaque, u32 bar, u32 off, u32 width, u32 *value)
{
    struct gf_foundation *f=((struct gfinit_bus *)opaque)->foundation;
    (void)bar;(void)width;
    if (f->revoked) return -1;
    *value=*(volatile u32 *)(f->bar0+off);
    return 0;
}
static int diagnostic_write(void *opaque, u32 bar, u32 off, u32 width, u32 value)
{
    struct gf_foundation *f=((struct gfinit_bus *)opaque)->foundation;
    (void)bar;(void)width;
    if (f->revoked) return -1;
    *(volatile u32 *)(f->bar0+off)=value;
    return 0;
}
static int prepare(struct gf_foundation *f, const struct recover_bar_extent *bars, u32 n)
{
    if (!f || !bars || n!=RECOVER_BAR_COUNT || !f->io.opaque ||
        f->revoked || f->bm || f->registered || f->mapped || f->running || f->imem.used)
        return GF1_EINVAL;
    struct gfinit_bus *bus=f->io.opaque;
    if (bus->foundation!=f || !bus->ready) return GF1_EINVAL;
    for (u32 i=0;i<n;i++)
        if (!bars[i].valid || bars[i].raw!=bus->bars[i]) return GF1_EPCI;
    f->io.read=diagnostic_read;f->io.write=diagnostic_write;
    f->bar0=P2V_IO(bars[0].base);
    f->instmem_bar=bars[2];
    f->map_ops=&gf_production_instmem_map_ops;f->map_owner=bus;
    f->expected_command=RECOVER_COMMAND_STOP;
    return GF1_OK;
}
static int inst_page(struct gf_foundation *f, struct gf1_test_reply *reply)
{
    struct gf_inst page={0};
    int rc=gf_inst_alloc(f,MEM_PAGE_SIZE,MEM_PAGE_SIZE,false,&page);
    if (rc) return rc;
    for (u32 off=0;off<MEM_PAGE_SIZE && !rc;off+=sizeof(u32))
        rc=gf_inst_write32(f,&page,off,0x5aa50000U^off);
    for (u32 off=0;off<MEM_PAGE_SIZE && !rc;off+=sizeof(u32)) {
        u32 value;
        rc=gf_inst_read32(f,&page,off,&value);
        if (!rc && value!=(0x5aa50000U^off)) {
            reply->bad_off=off;rc=GF1_EIO;
        }
        if (!rc) reply->bytes+=sizeof(u32);
    }
    gf_inst_free(f,&page);
    return rc;
}
static int call(struct gf_foundation *f, const struct gf1_test_request *q,
                 struct gf1_test_reply *reply)
{
    if (!reply) return GF1_EINVAL;
    *reply=(struct gf1_test_reply){.rc=GF1_EINVAL,.bad_off=~0U};
    if (!f || !q || q->size!=sizeof(*q) || q->op>GF1T_DISPLAY_START ||
        q->heads>3 || (q->op!=GF1T_HEADS && q->heads)) return reply->rc;
    int rc=GF1_OK;
    if (f->revoked && q->op!=GF1T_STOP && q->op!=GF1T_REVOKE && q->op!=GF1T_SNAPSHOT)
        rc=GF1_EPCI;
    else switch (q->op) {
    case GF1T_MASTER: rc=gf_master_enable(f);break;
    case GF1T_START: rc=gf_foundation_start(f);break;
    case GF1T_STOP: rc=gf_foundation_stop(f);break;
    case GF1T_REVOKE: gf_foundation_revoke(f);break;
    case GF1T_HEADS: {
        unsigned flags=irq_save();
        rc=f->running ? gf_irq_enable(f,f->wanted_fifo!=0,f->wanted_graph!=0,q->heads) : GF1_EINVAL;
        irq_restore(flags);
        break;
    }
    case GF1T_INST_PAGE: rc=f->running ? inst_page(f,reply) : GF1_EINVAL;break;
    case GF1T_DISPLAY_STOP:
    case GF1T_DISPLAY_START:
        rc=f==gf1_module_ops.production ?
            geforce_foundation_display(q->op==GF1T_DISPLAY_START) : GF1_EINVAL;
        break;
    case GF1T_SNAPSHOT: break;
    }
    reply->command=f->expected_command;reply->pmc=f->expected_pmc;
    reply->flags=(f->bm?1U:0)|(f->mapped?2U:0)|(f->registered?4U:0)|
        (f->running?8U:0)|(f->revoked?16U:0);
    gf_irq_snapshot(f,&reply->stats);
    return reply->rc=rc;
}
struct gf1_test_ops gf1_module_ops={.prepare=prepare,.call=call,.command=&gf2_module_ops};
