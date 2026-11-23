#ifndef PC98_USB_PLATFORM_H
#define PC98_USB_PLATFORM_H
#include "pci_dev_contract.h"
#include "usb_platform.h"

static inline int pc98_usb_is_ohci(const struct pci_dev *dev)
{
    return dev->vendor == 0x1033 && dev->device == 0x0035 &&
           dev->class == 0x0c && dev->subclass == 0x03 && dev->progif == 0x10;
}

/* Convert one enumerated function for the module probe. On failure out is unchanged. */
int pc98_usb_ohci_function(const struct pci_dev *dev, struct usb_pci_fn *out);
/* Index in the immutable boot PCI enumeration. */
struct ohci_firmware;
int pc98_usb_ohci_quiet(u32 index, struct ohci_firmware *saved);
void pc98_usb_early_quiesce(void);
#endif
