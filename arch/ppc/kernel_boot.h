#ifndef OS32_PPC_KERNEL_BOOT_H
#define OS32_PPC_KERNEL_BOOT_H
#include "types.h"
#include "memmap.h"
#define PPC_BOOT_ALIAS_BYTES 0x40000U
#define PPC_BOOT_ESCC_VA (MEM_DEVICE_APERTURE_BASE + 0x300000U)
#define PPC_BOOT_MPIC_VA (MEM_DEVICE_APERTURE_BASE + 0x340000U)
struct ppc_boot_alias { u32 pa, bytes, va; };
struct pm_kboot_info;
int ppc_boot_alias_plan(u32 pa, u32 bytes, u32 va,
                        struct ppc_boot_alias *out);
void ppc_boot_load_root(u32 root);
void ppc_kernel_real_start(const struct pm_kboot_info *info)
    __attribute__((noreturn));
#endif
