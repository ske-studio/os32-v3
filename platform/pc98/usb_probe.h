#ifndef PC98_USB_PLATFORM_H
#define PC98_USB_PLATFORM_H
#include "pci_dev_contract.h"
#include "usb_platform.h"

/* Convert one enumerated function for the module probe. On failure out is unchanged. */
int pc98_usb_ohci_function(const struct pci_dev *dev, struct usb_pci_fn *out);
/* Index in the immutable boot PCI enumeration. */
int pc98_usb_ohci_quiet(u32 index);
void pc98_usb_early_quiesce(void);
#endif
