#include "gfinit_resources.h"
#include "kprintf.h"
#include "../drivers/pci.h"
#ifndef GFINIT_RESOURCES_HOST
#ifdef HWDEV_MODULE
#include "hwdev/generated/hwdev_import_macros.h"
#else
#include "generated/kdebug_import_macros.h"
#endif
#endif
/* PCI Type 1 memory windows: PCI Local Bus configuration header, matching
 * FreeBSD releng/14.3 sys/dev/pci/pcireg.h PCIR_MEMBASE_1/PCIR_MEMLIMIT_1 and
 * PCIR_PMBASEL_1/PCIR_PMLIMITL_1; TI PCI2050B SCPS076G sections
 * 4.20-4.25 (memory/prefetch base, limit and upper bits).
 * No sizing writes to other devices.
 * Unknown endpoint apertures deliberately fail ADDENDUM's length contract. */
#define GF_RES_DEVICES 32U
#define GF_RES_FUNCTIONS 8U
#define GF_RES_HEADER 0x0cU
#define GF_RES_MEM_WINDOW 0x20U
#define GF_RES_PREF_WINDOW 0x24U
#define GF_RES_PREF_BASE_HIGH 0x28U
#define GF_RES_PREF_LIMIT_HIGH 0x2cU
#define GF_RES_ROM_ENDPOINT 0x30U
#define GF_RES_ROM_BRIDGE 0x38U
#define GF_RES_WINDOW_MASK 0xfff0U
#define GF_RES_WINDOW_SHIFT 16U
#define GF_RES_WINDOW_GRANULE 0x100000U

static int resource_reject(const char *reason, u32 dev, u32 fn, u32 reg, u32 raw)
{
    kprintf(0x07, "GF A1 reject=%s bdf=0:%u.%u reg=%x raw=%x\n",
            reason, dev, fn, reg, raw);
    return -1;
}

static int resource_window(struct recover_range *exclude, u32 *used,
                            u32 dev, u32 fn, u32 reg)
{
    u32 raw = pci_cfg_read32(0, dev, fn, reg);
    u32 base = (raw & GF_RES_WINDOW_MASK) << GF_RES_WINDOW_SHIFT;
    u32 last = ((raw >> 16) & GF_RES_WINDOW_MASK) << GF_RES_WINDOW_SHIFT;
    u32 type = raw & 15U, limit_type = (raw >> 16) & 15U;
    if (type != limit_type || type > (reg == GF_RES_PREF_WINDOW ? 1U : 0U))
        return resource_reject("window_type", dev, fn, reg, raw);
    if (type && (pci_cfg_read32(0, dev, fn, GF_RES_PREF_BASE_HIGH) ||
                 pci_cfg_read32(0, dev, fn, GF_RES_PREF_LIMIT_HIGH)))
        return resource_reject("window_high", dev, fn, reg, raw);
    if (base > last) return 0; /* PCI disabled window encoding. */
    unsigned long long len = (unsigned long long)last + GF_RES_WINDOW_GRANULE - base;
    if (len > 0xffffffffU || *used == RECOVER_RANGE_MAX)
        return resource_reject("window_range", dev, fn, reg, raw);
    exclude[(*used)++] = (struct recover_range){base, (u32)len};
    return 0;
}

int gfinit_resources(struct recover_range *exclude, u32 *count)
{
    if (!count) return -1;
    *count = 0;
    if (!exclude) return -1;
    u32 used = 0;
    bool gpu = false;
    /* Read every bus-0 function: the boot cache can be truncated/depth-limited.
     * All bridges are off the GPU's path, so their exact forwarding windows
     * exclude the complete downstream memory space, including child devices. */
    for (u32 dev = 0; dev < GF_RES_DEVICES; dev++) {
        for (u32 fn = 0; fn < GF_RES_FUNCTIONS; fn++) {
            u32 id = pci_cfg_read32(0, dev, fn, PCI_CFG_VENDOR_ID);
            if ((id & 0xffffU) == PCI_VENDOR_NONE) continue;
            if (dev == GF_RES_GPU_DEV && fn == 0) {
                if (id != GF_RES_GPU_ID) return resource_reject("gpu_id", dev, fn, PCI_CFG_VENDOR_ID, id);
                gpu = true;
                continue;
            }
            u32 header = (pci_cfg_read32(0, dev, fn, GF_RES_HEADER) >> 16) & 0x7fU;
            if (header != PCI_HDR_LAYOUT_DEVICE && header != PCI_HDR_LAYOUT_BRIDGE)
                return resource_reject("header", dev, fn, GF_RES_HEADER, header);
            if (!(pci_cfg_read16(0, dev, fn, PCI_CFG_COMMAND) & RECOVER_COMMAND_MEM)) continue;
            u32 bars = header == PCI_HDR_LAYOUT_BRIDGE ? 2U : PCI_CFG_BAR_COUNT;
            for (u32 bar = 0; bar < bars; bar++) {
                u32 raw = pci_cfg_read32(0, dev, fn, PCI_CFG_BAR0 + 4 * bar);
                /* Address alignment is only an upper bound, not BAR length. */
                if (raw && !(raw & PCI_BAR_SPACE_IO))
                    return resource_reject("memory_bar_length", dev, fn, PCI_CFG_BAR0 + 4 * bar, raw);
            }
            u32 rom = header == PCI_HDR_LAYOUT_BRIDGE ? GF_RES_ROM_BRIDGE : GF_RES_ROM_ENDPOINT;
            u32 rom_raw = pci_cfg_read32(0, dev, fn, rom);
            if (rom_raw & 1U) return resource_reject("rom_length", dev, fn, rom, rom_raw);
            if (header == PCI_HDR_LAYOUT_BRIDGE &&
                (resource_window(exclude, &used, dev, fn, GF_RES_MEM_WINDOW) ||
                 resource_window(exclude, &used, dev, fn, GF_RES_PREF_WINDOW))) return -1;
        }
    }
    if (!gpu) return resource_reject("gpu_missing", GF_RES_GPU_DEV, 0, 0, 0);
    *count = used;
    return 0;
}
