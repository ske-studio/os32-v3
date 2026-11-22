#ifndef PC98_PCI_RESOURCES_H
#define PC98_PCI_RESOURCES_H
#include "pci_dev_contract.h"

enum { NEC_VENDOR = 0x1033, NEC_OHCI = 0x0035, NEC_EHCI = 0x00e0 };
/* Ra266, TASK_USB §3: compare the measured decode with the controller width. */
enum { OHCI_BAR_SIZE = 4096, EHCI_BAR_SIZE = 256 };

static inline u32 pc98_usb_bar_width(const struct pci_dev *d)
{
    if (d->vendor != NEC_VENDOR) return 0;
    if (d->device == NEC_OHCI) return OHCI_BAR_SIZE;
    if (d->device == NEC_EHCI) return EHCI_BAR_SIZE;
    return 0;
}
/* Import, log, then install the IRQ state reader. */
void pc98_boot_pci_resources(void);
void pc98_usb_reserve(void);
#endif
