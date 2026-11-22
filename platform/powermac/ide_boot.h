#pragma once
#include "types.h"
#define PM_IDE_VA 0xfe3c7000U
#define PM_IDE_CTL 0x160U
#define PM_IDE_BANKS 2
extern u32 pm_ide_base[PM_IDE_BANKS];
u32 pm_ide_pci_phys(int device, u32 address, u32 bytes);
void pm_ide_capture(u32 pa[PM_IDE_BANKS]);
#if OS32_FEATURE_IDE
extern int pm_root_unit;
extern u32 pm_root_partition;
int pm_root_select(void);
void pm_ide_boot(void);
#else
static inline void pm_ide_boot(void) {}
#endif
