/* UniNorth window offsets: NetBSD sys/arch/macppc/pci/uninorth.c:165-171,
 * revision 36eddebcc36c. Boot foreground registers each pair once. */
STATIC_ASSERT(PPC_BOOT_GMAC_VA + PPC_BOOT_GMAC_BYTES + 2*PM_PCI_BRIDGES*PAGE_SIZE
              <= PPC_BOOT_PCI_ADDR_VA, pm_cfg_slots_fit_aperture);
static struct pm_bridge_state host_state[PM_PCI_BRIDGES];
static unsigned host_count;

struct pm_bridge_state *pm_bridge_lookup(int host_id)
{
    for (unsigned i = 0; i < host_count; i++)
        if (host_state[i].host_id == host_id) return &host_state[i];
    return 0;
}

struct pm_bridge_state *pm_bridge_register(int host_id, u32 addr_va, u32 data_va)
{
    struct pm_bridge_state *state = pm_bridge_lookup(host_id);
    if (state) return state;
    if (!host_id || host_id == -1 || !addr_va || !data_va || host_count == PM_PCI_BRIDGES)
        return 0;
    state = &host_state[host_count++];
    *state = (struct pm_bridge_state){.host_id=host_id, .addr_va=addr_va, .data_va=data_va};
    return state;
}

static int cfg_alias_safe(u32 root, const struct ppc_boot_alias *a)
{
    if (a->pa > U32_MAX - a->bytes || !observe_alias_safe(root, a) ||
        overlap(a->pa, a->bytes, fb_alias.pa, fb_alias.bytes) ||
        overlap(a->pa, a->bytes, clock_alias.pa, clock_alias.bytes) ||
        overlap(a->pa, a->bytes, via_alias.pa, via_alias.bytes)) return 0;
#if OS32_FEATURE_IDE
    for (unsigned i = 0; i < PM_IDE_BANKS; i++)
        if (overlap(a->pa, a->bytes, ide_alias[i].pa, ide_alias[i].bytes)) return 0;
#endif
    return 1;
}

static void install_pci_hosts(u32 root)
{
    const struct pm_pci_capture *c = &boot_info->pci;
    for (unsigned i = 0; i < c->bridges; i++) {
        const struct pm_of_bridge *b = &c->bridge[i];
        if (!b->supported || b->incomplete || b->parent >= 0 || pm_bridge_lookup(b->id)) continue;
        if (boot_devices.gmac_ready && (u32)b->id == boot_info->gmac.root_node &&
            b->cfg_pa == boot_info->gmac.root_pa && gmac_walk_valid(root)) {
            pm_bridge_register(b->id, gmac_alias[1].va, gmac_alias[2].va);
            continue;
        }
        /* 48 free pages between GMAC end and its original configuration VA. */
        u32 va = PPC_BOOT_GMAC_VA + PPC_BOOT_GMAC_BYTES + i * 2 * PAGE_SIZE;
        u32 offset = b->cfg_pa & (PAGE_SIZE-1);
        if (offset > PAGE_SIZE-8) continue;
        struct ppc_boot_alias addr = {(b->cfg_pa & PTE_FRAME_MASK)+0x800000, PAGE_SIZE, va+offset};
        struct ppc_boot_alias data = {addr.pa+0x400000, PAGE_SIZE, va+PAGE_SIZE+offset};
        if (!cfg_alias_safe(root, &addr) || !cfg_alias_safe(root, &data)) continue;
        install_alias(root, &addr);
        install_alias(root, &data);
        if (observe_walk_valid(root, &addr) && observe_walk_valid(root, &data))
            pm_bridge_register(b->id, addr.va, data.va);
        else {
            *leaf(root, va) = 0;
            *leaf(root, va+PAGE_SIZE) = 0;
        }
    }
}
