static int resource_append(struct recover_range *exclude, u32 *used,
                            u32 dev, u32 fn, u32 bar, u32 raw, u32 len,
                            u32 cap, bool exact)
{
    u32 base = raw & PCI_BAR_MEM_ADDR_MASK;
    u32 reg = PCI_CFG_BAR0 + PCI_CFG_BAR_STRIDE * bar;
    if (!base || !len || (exact && ((len & (len - 1U)) || (base & (len - 1U)))) ||
        (unsigned long long)base + len > RECOVER_ADDRESS_END)
        return resource_reject("memory_bar_range", dev, fn, reg, raw);
    if (exact && len > cap)
        return resource_reject("memory_bar_bound", dev, fn, reg, raw);
    if (*used >= RECOVER_RANGE_MAX)
        return resource_reject("memory_bar_count", dev, fn, reg, raw);
    exclude[(*used)++] = (struct recover_range){base, len};
    return 0;
}

static int resource_memory_bar(struct recover_range *exclude, u32 *used,
                            const struct resource_bounds *b, u32 id,
                            u32 dev, u32 fn, u32 header, u32 bar, u32 raw)
{
    u32 reg = PCI_CFG_BAR0 + PCI_CFG_BAR_STRIDE * bar;
    if (raw & PCI_BAR_MEM_TYPE)
        return resource_reject("memory_bar_type", dev, fn, reg, raw);
    const struct resource_bar_spec *spec = header == PCI_HDR_LAYOUT_DEVICE ? resource_spec(id, bar) : 0;
    bool bounded = header == PCI_HDR_LAYOUT_DEVICE && id == GF_RES_TRIDENT_ID &&
        bar <= GF_RES_BAR2 &&
        (pci_cfg_read32(0, dev, fn, PCI_CFG_REVISION_ID) & PCI_REVISION_MASK) == GF_RES_TRIDENT_REV;
    if (!spec && !bounded)
        return resource_reject("memory_bar_length", dev, fn, reg, raw);
    if ((raw & PCI_BAR_MEM_ATTR_MASK) != (spec ? spec->attr : PCI_BAR_MEM_TYPE32))
        return resource_reject("memory_bar_attr", dev, fn, reg, raw);
    u32 cap = resource_cap(b, raw & PCI_BAR_MEM_ADDR_MASK);
    return resource_append(exclude, used, dev, fn, bar, raw,
                            spec ? spec->len : cap, cap, spec != 0);
}
