#ifndef OS32_PPC_PMAP_H
#define OS32_PPC_PMAP_H
#include "ppc_oea.h"
#include "ppc_layout.h"
struct pm_region { u32 start, size; };
struct pm_regions { u32 count; struct pm_region v[PM_REGION_COUNT]; };
struct pm_boot_info {
    u32 tb_hz, escc_pa, hpt_pa, hpt_mask;
    const struct pm_regions *ram, *avail;
};
extern unsigned char pm_image_start[], pm_image_end[];
struct ppc_mapping { u32 va, pa, attr, slot, rc; };
struct ppc_mmu_context { u32 hpt_pa, hpt_mask, sr[PPC_SR_COUNT]; };
struct ppc_mmu_context *ppc_current_context(void);
void ppc_select_context(struct ppc_mmu_context *ctx);
/* EE=0; page aligned; BAT VAs excluded by caller. */
int ppc_pmap_enter(u32 va, u32 pa, u32 attr);
int ppc_pmap_remove(u32 va);
void ppc_pmap_bootstrap(const struct pm_boot_info *info);
void ppc_takeover(const struct pm_boot_info *info);
#endif
