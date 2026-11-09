#include "pci_locate.h"
#include "pci.h"

void pci_locate_fn0(u32 bus, u32 id, struct pci_locate *out)
{
    *out = (struct pci_locate){0};
    for (u32 dev = 0; dev < PCI_DEV_COUNT; dev++) {
        if (pci_cfg_read32(bus, dev, 0, PCI_CFG_VENDOR_ID) != id) continue;
        if (!out->found) { out->found = 1; out->dev = (u8)dev; }
        else if (!out->second_found) { out->second_found = 1; out->second = (u8)dev; }
    }
}
