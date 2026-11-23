/* DMA must be quiescent before the allocator claims firmware memory. */
static void pci_boot_memory(const struct pm_kboot_info *info, const struct ppc_boot_devices *dev)
{
    if (info->pci.gmac_known) {
        struct pm_pci pci = {info->gmac.bus_first, info->gmac.bus_last, info->gmac.bdf,
                             pm_bridge_lookup(info->gmac.root_node)};
        if (info->gmac_rc || !dev->gmac_ready || !pci.bridge ||
            pm_gmac_quiesce_setup_rh(&info->gmac, &pci, dev->clock_va) ||
            pm_gmac_early_quiesce() < 0) kboot_stop("gmac_quiesce");
    }
    for (unsigned i = 0; i < info->pci.bridges; i++) {
        const struct pm_of_bridge *b = &info->pci.bridge[i];
        if (b->parent >= 0 || !b->supported) continue;
        struct pm_pci pci = {b->first, b->last, 0, pm_bridge_lookup(b->id)};
        pm_pci_boot(&pci, 0);
        break;
    }
    if (!memory_boot_init(KBOOT_MEM_KB)) kboot_stop("memory_boot_init");
}
