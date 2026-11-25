#ifdef GF_PRODUCTION
static struct gfinit_resource_reject resource_rejected;
void gfinit_resources_rejected(struct gfinit_resource_reject *out)
{
    *out = resource_rejected;
}
#endif
static int resource_reject(const char *reason, u32 dev, u32 fn, u32 reg, u32 raw)
{
#ifdef GF_PRODUCTION
    resource_rejected = (struct gfinit_resource_reject){reason, dev, fn, reg, raw};
#else
    kprintf(KLOG_ATTR, "GF A1 reject=%s bdf=0:%u.%u reg=%x raw=%x\n",
            reason, dev, fn, reg, raw);
#endif
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
