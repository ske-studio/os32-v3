#include "platform_device.h"
#include "pci.h"
#include "pci_bind.h"

int platform_pci_count(void)
{
    return pci_count();
}

int platform_pci_get(u32 idx, void *out)
{
    return pci_get(idx, out);
}

u32 platform_pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    return pci_cfg_read32(bus, dev, fn, reg);
}

int platform_pci_bind_info_get(int idx, struct pci_bind_info *out)
{
    return pci_bind_info_get(idx, out);
}
