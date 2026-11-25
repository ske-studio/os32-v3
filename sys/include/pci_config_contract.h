#ifndef PCI_CONFIG_CONTRACT_H
#define PCI_CONFIG_CONTRACT_H

#include "types.h"

/* Nonempty, NUL-terminated name used in the boot log. */
const char *pci_config_name(void);
#define PCI_CONFIG_MAX_ROOTS 4
/* After a successful probe, return 0 and the indexed root bus, or -1 at
 * the end of the list (including every index outside the limit).
 * The bus ranges beneath distinct roots must not overlap. */
int pci_config_root_bus(u32 idx, u32 *bus);
/* 1 permits BAR sizing writes during enumeration, 0 permits reads only. */
int pci_config_bar_measure(void);

int pci_config_probe(void);
/* reg is a byte offset (0..255). The backend aligns it to a dword. */
u32 pci_config_read32(u32 bus, u32 dev, u32 fn, u32 reg);
void pci_config_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 value);
/* Command (04h) is the promise for every backend. PC-98 writes the 16-bit
 * lane of any even reg and keeps the other half (hwdev/gf writes 0Ch).
 * A backend that cannot do lane writes must reject other offsets. */
void pci_config_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value);

#endif
