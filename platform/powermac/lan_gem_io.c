#include "lan_boot.h"
#include "drivers/net/gem/gem.h"
#include "arch/ppc/kernel_boot.h"
#include "kboot_info.h"
#include "kboot_log.h"
#include "kprintf.h"

#define PCI_COMMAND 4u
#define PCI_MASTER 4u

static struct { struct pm_pci pci; u32 va; } hw;

static u32 rd(void *ctx, u32 off)
{
    u32 value, addr = hw.va + off;
    (void)ctx;
    __asm__ volatile("lwbrx %0,0,%1; eieio" : "=r"(value) : "r"(addr) : "memory");
    return value;
}

static void wr(void *ctx, u32 off, u32 value)
{
    u32 addr = hw.va + off;
    (void)ctx;
    __asm__ volatile("sync; stwbrx %0,0,%1; eieio" : : "r"(value), "r"(addr) : "memory");
}

static unsigned long long tb64(void *ctx)
{
    u32 hi, lo, again;
    (void)ctx;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return ((unsigned long long)hi << 32) | lo;
}

static int master(void *ctx)
{
    u32 command;
    (void)ctx;
    if (pm_pci_read32(&hw.pci, hw.pci.target_bdf, PCI_COMMAND, &command))
        return GEM_EIO;
    command = (command & 0xffffu) | PCI_MASTER;
    if (pm_pci_write32(&hw.pci, hw.pci.target_bdf, PCI_COMMAND, command))
        return GEM_EIO;
    if (pm_pci_read32(&hw.pci, hw.pci.target_bdf, PCI_COMMAND, &command))
        return GEM_EIO;
    return (command & PCI_MASTER) ? GEM_OK : GEM_EIO;
}

static void rx_bad_length(u32 count, unsigned long long flags,
                          unsigned long long addr, u32 length,
                          const u8 *data, u32 captured)
{
    static const char hex[] = "0123456789abcdef";
    char prefix[2 * GEM_RX_DIAG_BYTES + 1];
    for (u32 i = 0; i < captured; i++) {
        prefix[2 * i] = hex[data[i] >> 4];
        prefix[2 * i + 1] = hex[data[i] & 15];
    }
    prefix[2 * captured] = '\0';

    int saved = pm_log_stream_only;
    pm_log_stream_only = 1;
    kprintf(7, "G4 H n=%u flags=%08x%08x addr=%08x%08x len=%u captured=%u data=%s\r\n",
            count, (u32)(flags >> 32), (u32)flags, (u32)(addr >> 32), (u32)addr,
            length, captured, prefix);
    pm_log_stream_only = saved;
}

int pm_lan_gem_io(const struct pm_gmac_boot *b, struct gem_io *out)
{
    const struct ppc_boot_devices *dev = ppc_boot_devices();
    hw.va = dev->gmac_va;
    hw.pci = (struct pm_pci){dev->pci_addr_va, dev->pci_data_va,
                             b->bus_first, b->bus_last, b->bdf};
    *out = (struct gem_io){ .ctx = &hw, .read32 = rd, .write32 = wr,
        .tb = tb64, .tb_hz = ppc_boot_info()->tb_hz,
        .sync = pm_gmac_dma_sync, .master_enable = master,
        .rx_bad_length = rx_bad_length };
    return GEM_OK;
}
