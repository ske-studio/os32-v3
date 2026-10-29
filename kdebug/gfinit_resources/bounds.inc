static int resource_reject(const char *reason, u32 dev, u32 fn, u32 reg, u32 raw)
{
    kprintf(KLOG_ATTR, "GF A1 reject=%s bdf=0:%u.%u reg=%x raw=%x\n",
            reason, dev, fn, reg, raw);
    return -1;
}

/* Match the kernel scan: absent fn0 means absent device; single-function
 * devices may alias fn0 configuration at fn1..7 (Ra266 Trident). */
static u32 resource_function_count(u32 dev)
{
    u32 id = pci_cfg_read32(0, dev, 0, PCI_CFG_VENDOR_ID);
    if ((id & 0xffffU) == PCI_VENDOR_NONE) return 0;
    u8 hdr0 = pci_cfg_read32(0, dev, 0, PCI_CFG_CACHE_LINE) >>
        ((PCI_CFG_HEADER_TYPE - PCI_CFG_CACHE_LINE) * 8);
    return pci_is_multifunction(hdr0) ? PCI_FN_COUNT : 1;
}

static int resource_collect_bounds(struct resource_bounds *b)
{
    b->count = 0;
    for (u32 dev = 0; dev < PCI_DEV_COUNT; dev++) {
        u32 nfn = resource_function_count(dev);
        for (u32 fn = 0; fn < nfn; fn++) {
            u32 id = pci_cfg_read32(0, dev, fn, PCI_CFG_VENDOR_ID);
            if ((id & 0xffffU) == PCI_VENDOR_NONE ||
                (dev == GF_RES_GPU_DEV && fn == 0)) continue;
            u32 header = (pci_cfg_read32(0, dev, fn, PCI_CFG_CACHE_LINE) >> ((PCI_CFG_HEADER_TYPE - PCI_CFG_CACHE_LINE) * 8)) & PCI_HDR_LAYOUT_MASK;
            if (header != PCI_HDR_LAYOUT_DEVICE && header != PCI_HDR_LAYOUT_BRIDGE)
                return resource_reject("header", dev, fn, PCI_CFG_CACHE_LINE, header);
            if (!(pci_cfg_read16(0, dev, fn, PCI_CFG_COMMAND) & RECOVER_COMMAND_MEM)) continue;
            u32 bars = header == PCI_HDR_LAYOUT_BRIDGE ? PCI_BRIDGE_BAR_COUNT : PCI_CFG_BAR_COUNT;
            for (u32 bar = 0; bar < bars; bar++) {
                u32 reg = PCI_CFG_BAR0 + PCI_CFG_BAR_STRIDE * bar;
                u32 raw = pci_cfg_read32(0, dev, fn, reg);
                if (raw & PCI_BAR_SPACE_IO) continue;
                if ((raw & PCI_BAR_MEM_TYPE) == PCI_BAR_MEM_TYPE64) { bar++; continue; }
                if (raw & PCI_BAR_MEM_TYPE) continue;
                u32 base = raw & PCI_BAR_MEM_ADDR_MASK;
                if (!base) continue;
                /* Every BDF/register is visited exactly once; equal bases here
                 * necessarily belong to different registers, never duplicates
                 * of the same register. ROMs and forwarding windows are not
                 * candidates (BARLEN final C1/C2). */
                for (u32 i = 0; i < b->count; i++)
                    if (b->base[i] == base)
                        return resource_reject("memory_bar_alias", dev, fn, reg, raw);
                if (b->count >= RECOVER_RANGE_MAX)
                    return resource_reject("memory_bar_count", dev, fn, reg, raw);
                b->base[b->count++] = base;
            }
        }
    }
    return 0;
}

static u32 resource_cap(const struct resource_bounds *b, u32 base)
{
    u32 cap = base & (0U - base);
    for (u32 i = 0; i < b->count; i++)
        if (b->base[i] > base && b->base[i] - base < cap)
            cap = b->base[i] - base;
    return cap;
}

static const struct resource_bar_spec *resource_spec(u32 id, u32 bar)
{
    for (u32 i = 0; i < sizeof(resource_bar_specs) / sizeof(resource_bar_specs[0]); i++)
        if (resource_bar_specs[i].id == id && resource_bar_specs[i].bar == bar)
            return &resource_bar_specs[i];
    return 0;
}
