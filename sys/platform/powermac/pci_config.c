/* UniNorth configuration access: NetBSD macppc/pci/uninorth.c:191-271. */
#include "lan_boot.h"
#include "ppc_oea.h"
#include "kernel_boot.h"
#include "sys/dev/net/gem/gem.h"

#include "sys/kern/kdebug/hwb_wire.h"
#define PCI_COMMAND 4u
#define PCI_MASTER 4u
#define GMAC_ID 0x0021106bu

#ifndef PM_PCI_FAKE_MMIO
u32 pm_pci_mmio_read32(u32 va)
{
    u32 value;
    __asm__ volatile("lwzx %0,0,%1; eieio" : "=r"(value) : "r"(va) : "memory");
    return value;
}
#else
u32 pm_pci_mmio_read32(u32 va);
#endif

/* The raw MMIO layer transfers a big-endian CPU word; PCI is little-endian. */
static u32 le_read(u32 va) { return __builtin_bswap32(pm_pci_mmio_read32(va)); }
/* A rejected arm leaves the previous owner and the bus untouched. */
#ifndef PM_PCI_FAKE_LANE
#include "ppc_trap.h"
#include "hwb_instructions.h"
static int access_lane(u32 va, u32 width, int write, u32 *value)
{
    const struct transfer *t = &transfers[write][width >> 1][1];
    if (!ppc_trap_onfault_transfer((u32)t->pc, va, va, width, write, (u32)t->resume))
        return -1;
    if (t->run(va, write ? *value : (u32)value, 1, 0)) return 1;
    ppc_trap_onfault_clear();
    return 0;
}
#else
int pm_fake_lane(u32 va, u32 width, int write, u32 *value);
#define access_lane pm_fake_lane
#endif

static int selectable(const struct pm_pci *p, u32 bdf)
{
    u32 bus = bdf >> 16, dev = (bdf >> 11) & 31;
    return bus >= p->bus_first && bus <= p->bus_last &&
           (bus != p->bus_first || dev >= 11);
}

u32 pm_pci_access(const struct pm_pci *p, u32 bdf, u32 reg, u32 width,
                  int write, u32 *value)
{
    if (!value) return HW_ARG;
    u32 tmp = write ? *value : 0;
    if (!write) *value = 0;
    if (!p || !p->bridge) return HW_NOSYS;
    if ((width != 1 && width != 2 && width != 4) || reg >= 256 ||
        (reg & (width - 1)) || (bdf & 0xff0000ffu)) return HW_ARG;
    u32 bus = bdf >> 16, dev = (bdf >> 11) & 31, fn = bdf & 0x700;
    struct pm_bridge_state *bridge = p->bridge;
    u32 flags = ppc_irq_save(), rc = HW_IO_ERROR;
    if (bridge->busy || bridge->isolated) goto end;
    if (!selectable(p, bdf)) {
        if (!write) *value = U32_MAX >> ((4 - width) * 8);
        rc = HW_OK;
        goto end;
    }
    bridge->busy = 1;
    u32 select = (bus == p->bus_first ? (1u << dev) | fn : bdf | 1u) | (reg & ~3u);
    u32 addr = bridge->addr_va, rb, zero = 0;
    int selected = access_lane(addr, 4, 1, &select);
    /* A failed selection instruction still needs deselection. Both reads
     * flush posted writes. UniNorth has no verified readback bits. */
    if (selected >= 0) {
        if (!selected && !access_lane(addr, 4, 0, &rb) &&
            !access_lane(bridge->data_va + (reg & 7), width, write, &tmp))
            rc = HW_OK;
        if (access_lane(addr, 4, 1, &zero) || access_lane(addr, 4, 0, &rb)) {
            bridge->isolated = 1;
            rc = HW_IO_ERROR;
        }
    }
    bridge->busy = 0;
    if (!write && rc == HW_OK) *value = tmp;
end:
    ppc_irq_restore(flags);
    return rc;
}
int pm_pci_read32(struct pm_pci *p, u32 bdf, u32 reg, u32 *value)
{
    u32 rc = pm_pci_access(p, bdf, reg, 4, 0, value);
    return rc == HW_ARG ? LAN_EINVAL : rc == HW_OK ? LAN_OK : LAN_EIO;
}
int pm_pci_write32(struct pm_pci *p, u32 bdf, u32 reg, u32 value)
{
    u32 width = reg == PCI_COMMAND ? 2 : 4;
    u32 rc = pm_pci_access(p, bdf, reg, width, 1, &value);
    return rc == HW_ARG ? LAN_EINVAL : rc == HW_OK ? LAN_OK : LAN_EIO;
}

static const struct pm_gmac_boot *saved_boot;
static struct pm_pci saved_pci;
static u32 saved_clock_va, rh_setup;
static struct pm_gmac_rh_sample rh;
const struct pm_gmac_rh_sample *pm_gmac_rh_sample(void) { return &rh; }
/* P25 supplies the captured snapshot and P9's mapped aliases. */
void pm_gmac_quiesce_setup(const struct pm_gmac_boot *boot, const struct pm_pci *pci)
{
    saved_clock_va = rh_setup = 0;
    rh = (struct pm_gmac_rh_sample){0};
    saved_boot = boot;
    if (pci) saved_pci = *pci;
    else saved_boot = 0;
}
int pm_gmac_quiesce_setup_rh(const struct pm_gmac_boot *b, const struct pm_pci *p,
                            u32 clock_va)
{
    pm_gmac_quiesce_setup(b, p);
    saved_clock_va = clock_va;
    rh_setup = 1;
    return b && p ? LAN_OK : LAN_EINVAL;
}
int pm_gmac_early_quiesce(void)
{
    u32 id, command;
    const struct pm_gmac_boot *b = saved_boot;
    if (!b || !b->node || b->profile != PM_GMAC_PROFILE_UNINORTH ||
        b->vendor_device != GMAC_ID)
        return 0;
    if (saved_pci.target_bdf != b->bdf || !selectable(&saved_pci, b->bdf)) return LAN_EIO;
    if (b->clock.valid) {
        rh.clock_rc = pm_gmac_clock(b, saved_clock_va);
        if (rh.clock_rc) return LAN_EIO;
    }
    if (pm_pci_read32(&saved_pci, b->bdf, 0, &id)) return LAN_EIO;
    if (id != b->vendor_device)
        return 0;
    if (pm_pci_access(&saved_pci, b->bdf, PCI_COMMAND, 2, 0, &command)) return LAN_EIO;
    if (rh_setup) {
        rh.command = command & 0xffffu;
        rh.tx_enable = le_read(PPC_BOOT_GMAC_VA + GEM_MAC_TX_CONFIG) & GEM_MAC_TX_ENABLE;
        rh.rx_enable = le_read(PPC_BOOT_GMAC_VA + GEM_MAC_RX_CONFIG) & GEM_MAC_RX_ENABLE;
        rh.valid = 1;
    }
    if (pm_pci_write32(&saved_pci, b->bdf, PCI_COMMAND, command & ~PCI_MASTER)) return LAN_EIO;
    if (pm_pci_access(&saved_pci, b->bdf, PCI_COMMAND, 2, 0, &command)) return LAN_EIO;
    return (command & PCI_MASTER) ? LAN_EIO : 0;
}
