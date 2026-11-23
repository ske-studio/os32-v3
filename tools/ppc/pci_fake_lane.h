/* Fake little-endian lanes over the raw PCI words in legacy fixtures. */
int pm_fake_lane(u32 va, u32 width, int write, u32 *value)
{
    u32 mask = U32_MAX >> ((4 - width) * 8);
    if (write) pm_pci_mmio_write32(va, __builtin_bswap32(*value & mask));
    else *value = __builtin_bswap32(pm_pci_mmio_read32(va)) & mask;
    return 0;
}
