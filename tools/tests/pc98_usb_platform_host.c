#include "usb_probe.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_barsize.h"

static struct pci_dev dev = {.bus=1, .dev=13, .fn=1, .vendor=0x1033,
    .device=0x0035, .class=12, .subclass=3, .progif=16};
static struct pci_bar_resource bar = {.base=0xd0001000, .len=4096, .kind=PCI_BAR_MEM32};
static int cached = 1;
static unsigned regs[64], mode, writes, polls, maps, unmapped, bm, bad;
static u32 owned, smi, mem;

int pci_get(u32 i, void *out)
{
    if (i) return -1;
    *(struct pci_dev *)out = dev;
    return 0;
}

int usbp_mmio_map(const struct usb_pci_fn *f, u32 idx, volatile void **va)
{
    if (idx || f->fn != 2 || f->bar_base[0] != bar.base || f->bar_size[0] != 256)
        return -1;
    ++maps;
    if (mode == 5) return -1;
    *va = regs;
    return 0;
}

void usbp_mmio_unmap(const struct usb_pci_fn *f, u32 idx)
{
    (void)f; (void)idx;
    if (bm || regs[10]) ++bad;
    ++unmapped;
}

int usbp_cfg_write8(const struct usb_pci_fn *f, u32 reg, u8 value)
{
    if (reg == 0x74 || reg == 0x75) {
        if (f->fn != 2 || value || owned || regs[8] != 1 || !regs[10]) ++bad;
        smi &= ~(0xffu << (8 * (reg - 0x74)));
        return 0;
    }
    if (f->fn != 2 || reg != 0x73 || value != 1 || !owned) ++bad;
    ++writes;
    return 0;
}

void usbp_udelay(u32 us)
{
    if (us == 10000) {
        if (writes != 1 || regs[8] != 1 || !bm || !regs[10]) ++bad;
        if (++polls == 3 && mode == 0) owned = 0;
    } else {
        if (owned || smi || regs[10]) ++bad;
        if (regs[8] == 0) regs[9] = 0x1000;
        if (regs[8] == 2) regs[8] = 0;
    }
}

int usbp_cfg_cmd(const struct usb_pci_fn *f, u16 set, u16 clear)
{
    if (f->fn != 2) ++bad;
    if (set & 2) mem = 1;
    if (clear & 2) mem = 0;
    if (clear & 4) bm = 0;
    return 0;
}

void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }


int pci_bar_get(u32 bus, u32 slot, u32 fn, u32 reg, void *out)
{
    if (!cached || bus != dev.bus || slot != dev.dev || fn != dev.fn || reg != PCI_CFG_BAR0)
        return -1;
    *(struct pci_bar_resource *)out = bar;
    return 0;
}

u32 pci_cfg_read32(u32 bus, u32 slot, u32 fn, u32 reg)
{
    if (fn == 2 && reg == 0x60) return 0x7002;
    if (fn == 2 && reg == 0x70) return (owned << 16) | 1;
    return bus == dev.bus && slot == dev.dev && fn == dev.fn && reg == 0x34 ? 0x60 : 0;
}

int main(void)
{
    struct usb_pci_fn out;
    for (dev.fn = 0; dev.fn < 2; dev.fn++) {
        if (pc98_usb_ohci_function(&dev, &out) || out.fn != dev.fn ||
            out.irq != dev.irq_line || out.vendor != dev.vendor || out.device != dev.device ||
            out.bar_base[0] != bar.base || out.bar_size[0] != bar.len ||
            out.bar_size[1] || usbp_cfg_read32(&out, 0x34) != 0x60)
            return 1;
        dev.irq_line = 9;
    }
    dev.progif = 0x20;
    if (!pc98_usb_ohci_function(&dev, &out)) return 2;
    dev.device = 0x00e0;
    if (!pc98_usb_ohci_function(&dev, &out)) return 5;
    dev.progif = 0x10;
    if (!pc98_usb_ohci_function(&dev, &out)) return 6;
    dev.device = 0x0035;
    bar.base = 0xfffff000;
    bar.unassigned = 1;
    if (!pc98_usb_ohci_function(&dev, &out)) return 3;
    cached = 0;
    bar.unassigned = 0;
    if (!pc98_usb_ohci_function(&dev, &out)) return 4;
    if (out.fn != 1 || out.bar_base[0] != 0xd0001000) return 7;
    cached = 1;
    for (mode = 0; mode < 6; ++mode) {
        dev.device = 0x00e0; dev.fn = 2; dev.progif = 0x20;
        bar.unassigned = mode == 4;
        bar.base = bar.unassigned ? 0xffffff00 : 0xd0002000; bar.len = 256;
        regs[0] = 0x20; regs[2] = 0x6000; regs[8] = regs[10] = 1;
        regs[24] = mode != 3;
        owned = mode != 1;
        writes = polls = maps = unmapped = bad = 0; bm = mem = 1; smi = 0xe011;
        pc98_usb_early_quiesce();
        if (mode >= 4) {
            if (bad || bm || mem || unmapped || maps != (mode == 5) ||
                writes || polls || smi != 0xe011 || regs[8] != 1 || regs[10] != 1)
                return 12;
        } else {
            if (bad || maps != 1 || unmapped != 1 || bm) return 8;
            if (writes != (mode != 1) || polls != (mode == 0 ? 3u : mode == 1 ? 0u : 500u))
                return 9;
            if (regs[24] != (mode == 2) || regs[8] != (mode >= 2)) return 10;
        }
        bar.base = 0xd0001000; bar.unassigned = 0;
        dev.device = 0x0035; dev.fn = 0; dev.progif = 0x10; bar.len = 4096;
        if ((pc98_usb_ohci_function(&dev, &out) < 0) != (mode == 2 || mode == 5)) return 11;
    }
    return 0;
}
