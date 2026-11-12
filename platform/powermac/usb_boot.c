/* Takeover: FreeBSD (BSD-2) controller/ohci.c:183-214, ohci_pci.c.
 * PCI resources: Open Firmware PCI Binding 2.1. No BAR sizing writes.
 * IRQ-less OHCI: MIE stays clear; the 100Hz resident slot runs usb_service. */
#include "usb_boot.h"
#include "drivers/usb/usb_core.h"
#include "drivers/usb/ohci_regs.h"
#include "endian_dma.h"
#include "mm/paging.h"
#include "kstring.h"
#include "ppc_trap.h"
#include "ppc_oea.h"
extern const struct usb_hc_ops ohci_ops;
static struct usb_hc controllers[PM_USB_CONTROLLERS];
static u8 observed[PM_USB_CONTROLLERS][USB_DEV_MAX + 1];
static u32 count;
static void service(void)
{
    usb_service();
    for (u32 i = 0; i < count; i++) {
        struct usb_hc *hc = &controllers[i];
        for (u32 a = 1; a <= USB_DEV_MAX; a++) {
            struct usb_dev *d = hc->devices[a];
            int present = d && d->state == DEV_CONFIGURED;
            if (present && !observed[i][a]) {
                u32 cls = d->ddesc[4], sub = d->ddesc[5], proto = d->ddesc[6];
                if (!cls && d->cfg) {
                    for (u32 off = 0; off + 9 <= d->cfg_len && d->cfg[off] >= 2;
                         off += d->cfg[off]) {
                        if (d->cfg[off + 1] == 4) {
                            cls = d->cfg[off + 5]; sub = d->cfg[off + 6]; proto = d->cfg[off + 7]; break;
                        }
                    }
                }
                usbp_log("USB DEV addr=%u vid:pid=%04x:%04x class=%02x subclass=%02x protocol=%02x",
                         a, d->ddesc[8] | ((u32)d->ddesc[9] << 8),
                         d->ddesc[10] | ((u32)d->ddesc[11] << 8), cls, sub, proto);
            } else if (!present && observed[i][a]) usbp_log("USB DETACH addr=%u", a);
            observed[i][a] = present;
        }
    }
}
static int takeover(struct usb_hc *hc)
{
    volatile u8 *r = hc->regs;
    u32 ctl = mmio_rd32_le(r + OHCI_CONTROL);
    if (ctl & OHCI_CTL_IR) {
        mmio_wr32_le(r + OHCI_COMMAND_STATUS, OHCI_CMD_OCR);
        for (u32 i = 0; i < 100; i++) {
            usbp_udelay(1000);
            if (!(mmio_rd32_le(r + OHCI_CONTROL) & OHCI_CTL_IR)) break;
        }
        /* FreeBSD falls back to reset if firmware did not respond. */
    }
    mmio_wr32_le(r + OHCI_INTERRUPT_DISABLE, OHCI_INTR_ALL);
    mmio_wr32_le(r + OHCI_CONTROL, OHCI_CTL_RESET);
    usbp_udelay(100000);
    if (usbp_cfg_cmd(&hc->pci, 2, 4)) return -1;
    usbp_log("USB TAKEOVER quiet");
    return 0;
}
void pm_usb_boot(u32 tb_hz)
{
    pm_usb_platform_init(tb_hz);
    for (u32 root = 0; root < pm_usb_root_count && !count; root++) {
        pm_usb_root = &pm_usb_roots[root];
        if (paging_map_phys(PM_USB_ALIAS, pm_usb_root->pa + 0x800000, 1, PAGE_RW | PTE_PCD) ||
            paging_map_phys(PM_USB_ALIAS + PM_USB_PAGE, pm_usb_root->pa + 0xc00000, 1, PAGE_RW | PTE_PCD)) continue;
        pm_usb_pci = (struct pm_pci){PM_USB_ALIAS, PM_USB_ALIAS + PM_USB_PAGE,
                                    pm_usb_root->first, pm_usb_root->last, 0};
        for (u32 bus = pm_usb_root->first; bus <= pm_usb_root->last; bus++)
            for (u32 dev = bus == pm_usb_root->first ? 11 : 0; dev < 32; dev++)
                for (u32 fn = 0; fn < 8 && count < PM_USB_CONTROLLERS; fn++) {
                    struct usb_hc *hc = &controllers[count];
                    hc->pci = (struct usb_pci_fn){.bus = bus, .dev = dev, .fn = fn};
                    u32 id = usbp_cfg_read32(&hc->pci, 0);
                    if ((id & 0xffff) == 0xffff || (usbp_cfg_read32(&hc->pci, 8) >> 8) != 0x0c0310) continue;
                    hc->pci.vendor = id; hc->pci.device = id >> 16;
                    u32 bar = usbp_cfg_read32(&hc->pci, 0x10);
                    if ((bar & 15) || !(bar & ~15u)) continue;
                    hc->pci.bar_base[0] = bar; hc->pci.bar_size[0] = PM_USB_PAGE;
                    if (usbp_cfg_cmd(&hc->pci, 2, 0) ||
                        usbp_mmio_map(&hc->pci, 0, &hc->regs) || takeover(hc)) continue;
                    hc->ops = &ohci_ops;
                    if (hc->ops->start(hc) || usb_hc_register(hc)) {
                        usbp_log("USB START error"); continue;
                    }
                    count++;
                    usbp_log("USB HC bdf=%02x:%02x.%u ports=%u", bus, dev, fn, hc->nports);
                }
    }
    if (count && usbp_tick_attach(service)) usbp_log("USB TICK error");
}

/* Blocking input runs inside a USER syscall with EE clear. Consume an overdue
 * DEC cooperatively through the same clock/tick handler, without nesting an
 * interrupt on the syscall frame. A later pending DEC with positive counter
 * contributes no invented ticks (kernel_dec.c's existing contract). */
void pm_usb_poll(void)
{
    u32 flags = usbp_irq_save();
    if (ppc_read_dec() <= 0) ppc_decrementer_interrupt(0);
    usbp_irq_restore(flags);
}
