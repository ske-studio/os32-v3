/* UniNorth configuration access: NetBSD macppc/pci/uninorth.c:191-271. */
#include "lan_boot.h"
#include "ppc_oea.h"

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
void pm_pci_mmio_write32(u32 va, u32 value)
{
    __asm__ volatile("stwx %0,0,%1; eieio; sync" : : "r"(value), "r"(va) : "memory");
}
#else
u32 pm_pci_mmio_read32(u32 va);
void pm_pci_mmio_write32(u32 va, u32 value);
#endif

/* The raw MMIO layer transfers a big-endian CPU word; PCI is little-endian. */
static u32 le_swap(u32 value)
{
    return __builtin_bswap32(value);
}
static u32 le_read(u32 va) { return le_swap(pm_pci_mmio_read32(va)); }
static void le_write(u32 va, u32 value) { pm_pci_mmio_write32(va, le_swap(value)); }

static int address(struct pm_pci *p, u32 bdf, u32 reg, u32 *out)
{
    u32 bus = (bdf >> 16) & 255u, dev = (bdf >> 11) & 31u;
    u32 fn = (bdf >> 8) & 7u;
    if (!p || bdf != p->target_bdf || (bdf & 0xff0000ffu) ||
        p->bus_first > p->bus_last || bus < p->bus_first || bus > p->bus_last ||
        reg >= 256 || (reg & 3))
        return LAN_EINVAL;
    if (bus == p->bus_first) {
        if (dev < 11) return LAN_EINVAL;
        *out = (1u << dev) | (fn << 8) | reg;
    } else {
        *out = (bus << 16) | (dev << 11) | (fn << 8) | reg | 1u;
    }
    return LAN_OK;
}

static int transaction(struct pm_pci *p, u32 bdf, u32 reg, u32 *value, int write)
{
    u32 a, flags;
    if (!value || address(p, bdf, reg, &a)) return LAN_EINVAL;
    flags = ppc_irq_save();
    le_write(p->addr_va, a);
    (void)le_read(p->addr_va);
    if (write) {
        if (reg == PCI_COMMAND) *value &= 0xffffu;
        le_write(p->data_va + (reg & 4u), *value);
    } else {
        *value = le_read(p->data_va + (reg & 4u));
    }
    le_write(p->addr_va, 0);
    (void)le_read(p->addr_va);
    ppc_irq_restore(flags);
    return LAN_OK;
}
int pm_pci_read32(struct pm_pci *p, u32 bdf, u32 reg, u32 *value)
{ return transaction(p, bdf, reg, value, 0); }
int pm_pci_write32(struct pm_pci *p, u32 bdf, u32 reg, u32 value)
{ return transaction(p, bdf, reg, &value, 1); }

static const struct pm_gmac_boot *saved_boot;
static struct pm_pci saved_pci;
/* P25 supplies the captured snapshot and P9's mapped aliases. */
void pm_gmac_quiesce_setup(const struct pm_gmac_boot *boot, const struct pm_pci *pci)
{
    saved_boot = boot;
    if (pci) saved_pci = *pci;
    else saved_boot = 0;
}
int pm_gmac_early_quiesce(void)
{
    u32 id, command;
    const struct pm_gmac_boot *b = saved_boot;
    if (!b || !b->node || b->profile != PM_GMAC_PROFILE_UNINORTH ||
        b->vendor_device != GMAC_ID || saved_pci.target_bdf != b->bdf)
        return 0;
    if (pm_pci_read32(&saved_pci, b->bdf, 0, &id) || id != b->vendor_device)
        return 0;
    if (pm_pci_read32(&saved_pci, b->bdf, PCI_COMMAND, &command)) return 0;
    if (pm_pci_write32(&saved_pci, b->bdf, PCI_COMMAND, command & ~PCI_MASTER)) return 0;
    if (pm_pci_read32(&saved_pci, b->bdf, PCI_COMMAND, &command)) return LAN_EIO;
    return (command & PCI_MASTER) ? LAN_EIO : 0;
}
