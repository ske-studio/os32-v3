#ifndef PM_USB_BOOT_H
#define PM_USB_BOOT_H
#include "lan_boot.h"
#include "arch/ppc/kernel_boot.h"
#define PM_USB_ROOTS 3u
#define PM_USB_CONTROLLERS 3u
#define PM_USB_PAGE 4096u
#define PM_USB_CFG_ADDR_OFFSET 0x800000u
#define PM_USB_CFG_DATA_OFFSET 0xc00000u
#define PM_USB_DMA_BYTES 0x10000u
#define PM_USB_DMA_WINDOW 0x1000000u
#define PM_TICK_USB 0u
struct pm_usb_root {
    int host_id;
    struct pm_pci pci;
    u32 pa, first, last, nwin;
    struct pm_pci_window win[8];
};
extern struct pm_usb_root pm_usb_roots[PM_USB_ROOTS];
extern u32 pm_usb_root_count;
void pm_usb_capture(void);
void pm_usb_boot(u32 tb_hz);
void pm_usb_scan(void);
void pm_usb_poll(void);
void pm_usb_platform_init(u32 tb_hz);
#endif
