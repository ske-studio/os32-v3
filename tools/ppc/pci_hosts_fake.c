/* HWOP_COMMON_DESIGN §2.2: map complete address/data pairs, key state by OF ID. */
#include "kboot_info.h"
#include "kernel_boot.h"
#include "paging.h"

static struct pm_kboot_info info;
static const struct pm_kboot_info *boot_info = &info;
static struct ppc_boot_devices boot_devices;
static struct ppc_boot_alias gmac_alias[3], fb_alias, clock_alias, via_alias;
static struct ppc_boot_alias installed[8];
static u32 count, fail_pa, erased;
static int overlap(u32 a, u32 n, u32 b, u32 m)
{
    return n && m && (a <= b ? b-a < n : a-b < m);
}
static int observe_alias_safe(u32 root, const struct ppc_boot_alias *a)
{
    return root == 1 && a->pa != fail_pa;
}
static int gmac_walk_valid(u32 root) { return root == 1; }
static void install_alias(u32 root, const struct ppc_boot_alias *a)
{
    if (root == 1 && count < 8) installed[count++] = *a;
}
static int observe_walk_valid(u32 root, const struct ppc_boot_alias *a)
{
    for (unsigned i = 0; root == 1 && i < count; i++)
        if (installed[i].va == a->va && installed[i].pa == a->pa) return 1;
    return 0;
}
static u32 *leaf(u32 root, u32 va)
{
    (void)root; (void)va;
    return &erased;
}
#include "pci_hosts.inc"

static int test(void)
{
    info.pci.bridges = 7;
    for (unsigned i = 0; i < 3; i++)
        info.pci.bridge[i] = (struct pm_of_bridge){.supported=1, .parent=-1,
            .id=10+i, .cfg_pa=0xe0000000+i*0x1000000};
    /* Same page under different IDs, and one host's data page at another's
     * address page. Include the GMAC fast path in the conflicting pair. */
    const u32 bases[] = {0xd0000000, 0xd0000004, 0xc0000000, 0xc0400000};
    for (unsigned i = 0; i < 4; i++)
        info.pci.bridge[3+i] = (struct pm_of_bridge){.supported=1, .parent=-1,
            .id=13+i, .cfg_pa=bases[i]};
    boot_devices.gmac_ready = 1;
    info.gmac.root_node = 13;
    info.gmac.root_pa = bases[0];
    gmac_alias[1].va = 4;
    gmac_alias[2].va = 8;
    fail_pa = 0xe2c00000;
    install_pci_hosts(1);
    struct pm_bridge_state *a = pm_bridge_lookup(10), *b = pm_bridge_lookup(11);
    if (!a || !b || a == b || a->addr_va == b->addr_va || pm_bridge_lookup(12) ||
        count != 4 || installed[0].pa != 0xe0800000 || installed[1].pa != 0xe0c00000 ||
        installed[2].pa != 0xe1800000 || installed[3].pa != 0xe1c00000) return 1;
    for (int id = 13; id <= 16; id++) if (pm_bridge_lookup(id)) return 4;
    a->isolated = 1;
    u32 va = a->addr_va;
    if (pm_bridge_register(10, 8, 16) != a || a->addr_va != va || !a->isolated || b->isolated)
        return 2;
    install_pci_hosts(1);
    return count == 4 ? 0 : 3;
}

void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc));
    __builtin_unreachable();
}
