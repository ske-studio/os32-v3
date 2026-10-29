/* Native production transport. Permanent boot maps: no kpeek/recover window,
 * no temporary PTE edits. P5 owns paging_map_phys before reservation freeze. */
#include "production.h"
#include "../../kernel/paging.h"
#include "../../kernel/pgalloc.h"
#include "../../include/sys.h"
#define GF_PRODUCTION_WAIT_POLLS 1000000U
#ifdef GF_PRODUCTION_MODULE
#include "geforce_module.h"
#define reserved geforce_range_reserved
#else
static int reserved(u32 base, u32 length)
{
    u32 first=base/PAGE_SIZE, end=first+length/PAGE_SIZE;
    while (first<end) {
        u32 next=first;
        for (u32 i=0;i<ledger_region_count;i++) {
            const struct ledger_region *r=&ledger_regions[i];
            if (r->type==LEDGER_R_DEVICE && r->owner==LEDGER_OWNER_GFX &&
                r->cache==LEDGER_CACHE_UC && r->first<=first && r->end>next)
                next=r->end;
        }
        if (next==first) return -1;
        first=next;
    }
    return 0;
}
#endif

int gf_production_mapped(struct gf_production *p)
{
    for (u32 i=0;i<RECOVER_BAR_COUNT;i++) {
        const struct recover_bar_extent *b=&p->display.bars[i];
        if (reserved(b->base,b->length)) return -1;
        /* BAR2 has a decode reservation only. BAR0 covers register aliases,
         * PROM and all known script destinations; BAR1 maps DISPLAY only. */
        u32 bytes=i==0 ? b->length : i==GF_MODE_VRAM_BAR ? PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES) : 0;
        for (u32 off=0;off<bytes;off+=PAGE_SIZE) {
            u32 raw=paging_pte_raw(b->base+off);
            if ((raw&~(PAGE_SIZE-1))!=b->base+off ||
                (raw&(PTE_PRESENT|PTE_RW|PTE_PCD|PTE_USER))!=(PTE_PRESENT|PTE_RW|PTE_PCD)) return -1;
        }
    }
    return 0;
}
int gf_production_mmio(struct gf_production *p, u32 bar, u32 off, u32 width,
                        bool write, u32 *value)
{
    u32 phys=p->display.bars[bar].base+off;
    if (width==1) {
        volatile u8 *v=P2V_IO(phys);
        if (write) *v=(u8)*value; else *value=*v;
    } else if (width==2) {
        volatile u16 *v=P2V_IO(phys);
        if (write) *v=(u16)*value; else *value=*v;
    } else {
        volatile u32 *v=P2V_IO(phys);
        if (write) *v=*value; else *value=*v;
    }
    return 0;
}
int gf_production_clock(void *opaque, u32 *value)
{
    (void)opaque;
    u32 hi;
    return sys_time_now(value,&hi) ? -1 : 0;
}
int gf_production_wait(void *opaque, u32 us)
{
    u32 start,now;
    if (!us) return 0;
    if (us>GF_INIT_WAIT_CHUNK_US || gf_production_clock(opaque,&start)) return -1;
    for (u32 i=0;i<GF_PRODUCTION_WAIT_POLLS;i++) {
        if (gf_production_clock(opaque,&now)) return -1;
        if (now-start>=us) return 0;
    }
    return -1; /* finite even if the PIT stops */
}
