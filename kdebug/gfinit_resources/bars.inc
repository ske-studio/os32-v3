static int resource_memory_bar(struct recover_range *exclude, u32 *used,
                               u32 dev, u32 fn, u32 reg, u32 raw)
{
    struct pci_bar_resource r;
    if (pci_bar_get(0, dev, fn, reg, &r))
        return resource_reject("memory_bar_cache", dev, fn, reg, raw);
    if (raw != r.raw)
        return resource_reject("memory_bar_changed", dev, fn, reg, raw);
    if (r.unassigned) return 0;
    if (!r.len) return resource_reject("memory_bar_length", dev, fn, reg, raw);
    if (r.kind == PCI_BAR_MEM64 &&
        pci_cfg_read32(0, dev, fn, reg + PCI_CFG_BAR_STRIDE) != r.high)
        return resource_reject("memory_bar_changed", dev, fn, reg, raw);
    if (!r.base || r.len > U32_MAX || r.base >= RECOVER_ADDRESS_END || r.base + r.len > RECOVER_ADDRESS_END ||
        (r.base & (r.len - 1U)))
        return resource_reject("memory_bar_range", dev, fn, reg, raw);
    if (*used >= RECOVER_RANGE_MAX)
        return resource_reject("memory_bar_count", dev, fn, reg, raw);
    exclude[(*used)++] = (struct recover_range){(u32)r.base, (u32)r.len};
    return 0;
}
