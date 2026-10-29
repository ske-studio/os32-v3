static int resource_window(struct recover_range *exclude, u32 *used,
                            u32 dev, u32 fn, u32 reg)
{
    u32 raw = pci_cfg_read32(0, dev, fn, reg);
    u32 base = (raw & PCI_BRIDGE_WINDOW_MASK) << PCI_BRIDGE_WINDOW_SHIFT;
    u32 last = ((raw >> 16) & PCI_BRIDGE_WINDOW_MASK) << PCI_BRIDGE_WINDOW_SHIFT;
    u32 type = raw & PCI_BAR_MEM_ATTR_MASK, limit_type = (raw >> 16) & PCI_BAR_MEM_ATTR_MASK;
    if (type != limit_type || type > (reg == PCI_CFG_PREF_WINDOW ? 1U : 0U))
        return resource_reject("window_type", dev, fn, reg, raw);
    if (type && (pci_cfg_read32(0, dev, fn, PCI_CFG_PREF_BASE_HIGH) ||
                 pci_cfg_read32(0, dev, fn, PCI_CFG_PREF_LIMIT_HIGH)))
        return resource_reject("window_high", dev, fn, reg, raw);
    if (base > last) return 0; /* PCI disabled window encoding. */
    unsigned long long len = (unsigned long long)last + PCI_BRIDGE_WINDOW_GRANULE - base;
    if (len > 0xffffffffU || *used == RECOVER_RANGE_MAX)
        return resource_reject("window_range", dev, fn, reg, raw);
    exclude[(*used)++] = (struct recover_range){base, (u32)len};
    return 0;
}
