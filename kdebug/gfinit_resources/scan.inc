int gfinit_resources(struct recover_range *exclude, u32 *count)
{
    if (!count) return -1;
    *count = 0;
    if (!exclude) return -1;
    struct resource_bounds bounds;
    if (resource_collect_bounds(&bounds)) return -1;
    u32 used = 0;
    bool gpu = false;
    /* Read every present bus-0 function: the boot cache can be truncated/depth-limited.
     * All bridges are off the GPU's path, so their exact forwarding windows
     * exclude the complete downstream memory space, including child devices. */
    for (u32 dev = 0; dev < PCI_DEV_COUNT; dev++) {
        u32 nfn = resource_function_count(dev);
        for (u32 fn = 0; fn < nfn; fn++) {
            u32 id = pci_cfg_read32(0, dev, fn, PCI_CFG_VENDOR_ID);
            if ((id & 0xffffU) == PCI_VENDOR_NONE) continue;
            if (dev == GF_RES_GPU_DEV && fn == 0) {
                if (id != GF_RES_GPU_ID) return resource_reject("gpu_id", dev, fn, PCI_CFG_VENDOR_ID, id);
                gpu = true;
                continue;
            }
            u32 header = (pci_cfg_read32(0, dev, fn, PCI_CFG_CACHE_LINE) >> ((PCI_CFG_HEADER_TYPE - PCI_CFG_CACHE_LINE) * 8)) & PCI_HDR_LAYOUT_MASK;
            if (header != PCI_HDR_LAYOUT_DEVICE && header != PCI_HDR_LAYOUT_BRIDGE)
                return resource_reject("header", dev, fn, PCI_CFG_CACHE_LINE, header);
            if (!(pci_cfg_read16(0, dev, fn, PCI_CFG_COMMAND) & RECOVER_COMMAND_MEM)) continue;
            u32 bars = header == PCI_HDR_LAYOUT_BRIDGE ? PCI_BRIDGE_BAR_COUNT : PCI_CFG_BAR_COUNT;
            for (u32 bar = 0; bar < bars; bar++) {
                u32 raw = pci_cfg_read32(0, dev, fn, PCI_CFG_BAR0 + PCI_CFG_BAR_STRIDE * bar);
                if (raw && !(raw & PCI_BAR_SPACE_IO)) {
                    if (resource_memory_bar(exclude, &used, &bounds, id, dev, fn, header, bar, raw))
                        return -1;
                }
            }
            u32 rom = header == PCI_HDR_LAYOUT_BRIDGE ? PCI_CFG_ROM_BRIDGE : PCI_CFG_ROM_ENDPOINT;
            u32 rom_raw = pci_cfg_read32(0, dev, fn, rom);
            if (rom_raw & 1U) return resource_reject("rom_length", dev, fn, rom, rom_raw);
            if (header == PCI_HDR_LAYOUT_BRIDGE &&
                (resource_window(exclude, &used, dev, fn, PCI_CFG_MEM_WINDOW) ||
                 resource_window(exclude, &used, dev, fn, PCI_CFG_PREF_WINDOW))) return -1;
        }
    }
    if (!gpu) return resource_reject("gpu_missing", GF_RES_GPU_DEV, 0, 0, 0);
    *count = used;
    return 0;
}
