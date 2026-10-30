/* Read-only Ra266 resource gate; no sizing. Uses the existing PCI scan and
 * bridge-window exclusions (gfinit_resources, PCI2050B / FreeBSD references).
 * Fixed lengths: GF_DRIVER_DESIGN §4-1, measured gf_a4 acceptance. */
#include "production.h"
#include "../../drivers/pci/pci.h"
#include "../../mm/pgalloc.h"
#include "../../kdebug/gfinit_resources.h"
int gf_production_identify(struct gf_production_resources *out)
{
    struct gf_production_resources next={0};
    struct recover_range exclude[RECOVER_RANGE_MAX];
    u32 count;
    const u32 lengths[]={GF_PRODUCTION_BAR0_BYTES,GF_PRODUCTION_BAR1_BYTES,
                         GF_PRODUCTION_BAR2_BYTES};
    if (!out) return -1;
    *out=next;
    next.identity=(struct recover_identity){.bdf=RECOVER_GF_BDF,
        .device=pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_VENDOR_ID),
        .subsys=pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_SUBSYS_VENDOR),
        .revision=pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_REVISION_ID)&PCI_REVISION_MASK};
    if (next.identity.device!=RECOVER_GF_DEVICE ||
        next.identity.subsys!=GF_PRODUCTION_SUBSYS) return -1;
    if (gfinit_resources(exclude,&count)) {gfinit_resources_rejected(&out->reject);return -1;}
    for (u32 i=0;i<RECOVER_BAR_COUNT;i++) {
        u32 raw=pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,RECOVER_GF_FN,PCI_CFG_BAR0+i*PCI_CFG_BAR_STRIDE);
        u32 base=raw&PCI_BAR_MEM_ADDR_MASK, len=lengths[i];
        u32 attr=i==GF_MODE_VRAM_BAR ? PCI_BAR_MEM_PREFETCH : PCI_BAR_MEM_TYPE32;
        if ((raw&PCI_BAR_MEM_ATTR_MASK)!=attr || !base || (base&(len-1)) ||
            (unsigned long long)base+len>RECOVER_ADDRESS_END ||
            pgalloc_range_has_ram(base/PAGE_SIZE,(base/PAGE_SIZE)+(len/PAGE_SIZE))) return -1;
        for (u32 j=0;j<count;j++)
            if ((unsigned long long)base<(unsigned long long)exclude[j].base+exclude[j].len &&
                (unsigned long long)exclude[j].base<(unsigned long long)base+len) return -1;
        for (u32 j=0;j<i;j++)
            if ((unsigned long long)base<(unsigned long long)next.bars[j].base+next.bars[j].length &&
                (unsigned long long)next.bars[j].base<(unsigned long long)base+len) return -1;
        next.bars[i]=(struct recover_bar_extent){raw,base,len,1};
    }
    next.identity.bar0=next.bars[0].raw;
    *out=next;
    return 0;
}
