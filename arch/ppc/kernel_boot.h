#ifndef OS32_PPC_KERNEL_BOOT_H
#define OS32_PPC_KERNEL_BOOT_H
#include "types.h"
#include "memmap.h"
#define PPC_BOOT_ALIAS_BYTES 0x40000U
#if KBOOT_SHELL || KBOOT_SH
#define PPC_BOOT_FB_VA (MEM_DEVICE_APERTURE_BASE + 0x100000U)
#define PPC_BOOT_FB_LIMIT 0x200000U
#else
#define PPC_BOOT_FB_VA (MEM_DEVICE_APERTURE_BASE + 0x200000U)
#define PPC_BOOT_FB_LIMIT 0x40000U
#endif
#define PPC_BOOT_ESCC_VA (MEM_DEVICE_APERTURE_BASE + 0x300000U)
#define PPC_BOOT_GMAC_VA 0xFE380000U
#define PPC_BOOT_PCI_ADDR_VA 0xFE3C0000U
#define PPC_BOOT_PCI_DATA_VA 0xFE3C1000U
#define PPC_BOOT_GMAC_CLOCK_VA 0xFE3C2000U
#define PPC_BOOT_USB_VA 0xFE3D0000U
#define PPC_BOOT_GMAC_BYTES 0x10000U
#define PPC_BOOT_MPIC_VA (MEM_DEVICE_APERTURE_BASE + 0x340000U)
struct ppc_boot_alias { u32 pa, bytes, va; };
struct pm_kboot_info;
/* Device VAs inside the static aperture slots (design section 4). */
struct ppc_boot_devices {
    u32 escc_csr_va, escc_data_va, mpic_va, mpic_bytes;
    u32 gmac_va, pci_addr_va, pci_data_va;
    u32 fb_va, clock_va;
    int fb_full_rc;
    int gmac_ready;
    int mpic_order; /* enum pm_mpic_order from E1 */
};
const struct ppc_boot_devices *ppc_boot_devices(void);
const struct pm_kboot_info *ppc_boot_info(void);
int ppc_boot_alias_plan(u32 pa, u32 bytes, u32 va,
                        struct ppc_boot_alias *out);
void ppc_boot_load_root(u32 root);
void ppc_kernel_real_start(const struct pm_kboot_info *info)
    __attribute__((noreturn));
#endif
