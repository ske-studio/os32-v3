#include "usb_probe.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_barsize.h"

static struct pci_dev dev = {.bus=1, .dev=13, .fn=1, .vendor=0x1033,
    .device=0x0035, .class=12, .subclass=3, .progif=16};
static struct pci_bar_resource bar = {.base=0xd0001000, .len=4096, .kind=PCI_BAR_MEM32};
static int cached = 1;

int pci_bar_get(u32 bus, u32 slot, u32 fn, u32 reg, void *out)
{
    if (!cached || bus != dev.bus || slot != dev.dev || fn != dev.fn || reg != PCI_CFG_BAR0)
        return -1;
    *(struct pci_bar_resource *)out = bar;
    return 0;
}

u32 pci_cfg_read32(u32 bus, u32 slot, u32 fn, u32 reg)
{
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
    dev.progif = 0x10;
    bar.base = 0xfffff000;
    bar.unassigned = 1;
    if (!pc98_usb_ohci_function(&dev, &out)) return 3;
    cached = 0;
    bar.unassigned = 0;
    if (!pc98_usb_ohci_function(&dev, &out)) return 4;
    return out.fn != 1 || out.bar_base[0] != 0xd0001000;
}
