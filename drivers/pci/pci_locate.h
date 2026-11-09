#ifndef PCI_LOCATE_H
#define PCI_LOCATE_H
#include "types.h"

struct pci_locate { u8 found, dev, second_found, second; };
/* Scan fn0 on one bus in device order, without the truncated boot table. */
void pci_locate_fn0(u32 bus, u32 id, struct pci_locate *out);
#endif
