#ifndef PCI_CONFIG_CONTRACT_H
#define PCI_CONFIG_CONTRACT_H

#include "types.h"

/* The enumerator probes before access. Word writes use an even byte offset
 * and must preserve the other half of the configuration dword. */
int pci_config_probe(void);
u32 pci_config_read32(u32 bus, u32 dev, u32 fn, u32 reg);
void pci_config_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 value);
void pci_config_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value);

#endif
