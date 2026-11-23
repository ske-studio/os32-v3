#ifndef PM_HWB_RESOURCE_H
#define PM_HWB_RESOURCE_H
#include "kdebug/hwb_wire.h"
struct pm_kboot_info;
void pm_hwb_resource_init(const struct pm_kboot_info *info, int gmac_verified);
u32 pm_hwb_bar(u32 bdf, u32 bar, u32 space, u32 base, u32 offset, u32 bytes, u32 *addr);
u32 pm_hwb_phys_check(u32 addr, u32 width, u32 bytes);
u32 pm_hwb_phys(struct hw_req *req);
u32 pm_hwb_block(struct hw_block_req *block);
#endif
