#include "lan_boot.h"
#include "pci_config_contract.h"
#include "drivers/pci/pci_bind.h"

static struct pm_pci root;

void pm_pci_boot(const struct pm_pci *window, u32 probe_bdf)
{
    root = window ? *window : (struct pm_pci){0};
    root.target_bdf = probe_bdf;
}

void pm_pci_enumerate(void)
{
    pci_init();
    pci_bind_all(NULL, 0);
}

const char *pci_config_name(void)
{
    return "uni-north";
}

int pci_config_root_bus(u32 idx, u32 *bus)
{
    if (idx != 0 || !bus || !root.bridge)
        return -1;
    *bus = root.bus_first;
    return 0;
}

int pci_config_bar_measure(void)
{
    return 0;
}

int pci_config_probe(void)
{
    return root.bridge != NULL;
}

static int select_function(struct pm_pci *p, u32 bus, u32 dev, u32 fn, u32 reg)
{
    if (!root.bridge || bus > 255 || dev > 31 || fn > 7 || reg > 255)
        return 0;
    *p = root;
    p->target_bdf = (bus << 16) | (dev << 11) | (fn << 8);
    return 1;
}

u32 pci_config_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    struct pm_pci p;
    u32 value;
    if (!select_function(&p, bus, dev, fn, reg) ||
        pm_pci_read32(&p, p.target_bdf, reg & ~3u, &value))
        return U32_MAX;
    return value;
}

void pci_config_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 value)
{
    struct pm_pci p;
    if (!(reg & 3u) && select_function(&p, bus, dev, fn, reg))
        pm_pci_write32(&p, p.target_bdf, reg, value);
}

void pci_config_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value)
{
    if (reg == 4)
        pci_config_write32(bus, dev, fn, reg, value);
}

u32 pm_hwb_cfg_read(u32 bdf, u32 offset, u32 width, u32 *value)
{ return pm_pci_access(&root, bdf << 8, offset, width, 0, value); }

u32 pm_hwb_cfg_write(u32 bdf, u32 offset, u32 width, u32 value)
{ return pm_pci_access(&root, bdf << 8, offset, width, 1, &value); }
