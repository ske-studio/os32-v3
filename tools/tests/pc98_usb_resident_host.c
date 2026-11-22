#include "usb_resident.h"
#include "usb_probe.h"
#include "drivers/usb/ohci_priv.h"
#include "kprintf.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_barsize.h"
#include "drivers/usb/ohci_regs.h"
#include "kernel/irq/irq.h"

static unsigned regs[64], cards, maps, starts, services, registered, refs, bad;
static int fail_register, refuse_detach;
static unsigned stuck_bm, usb_pending = 1, peer_calls;
static irq_handler_fn peer, handler;
static void *argument;
static struct module_record record = {.owner=17};

int pci_get(u32 i, void *out)
{
    if (i > cards) return -1;
    *(struct pci_dev *)out = (struct pci_dev){.dev=i ? 13 : 12, .fn=i ? i-1 : 0, .irq_line=i==1 ? 5 : 0,
        .class=12, .subclass=3, .vendor=i ? 0x1033 : 0x1234, .device=0x0035, .progif=16};
    return 0;
}
int pci_bar_get(u32 bus, u32 dev, u32 fn, u32 reg, void *out)
{
    (void)bus; (void)dev; (void)reg;
    *(struct pci_bar_resource *)out = (struct pci_bar_resource){.kind=PCI_BAR_MEM32,
        .base=0xd0000000 + fn*4096, .len=4096};
    return 0;
}
int usbp_mmio_map(const struct usb_pci_fn *f, u32 bar, volatile void **va)
{
    if (f->dev != 13 || bar) bad++;
    maps++;
    regs[OHCI_CONTROL/4] = OHCI_CTL_IR;
    regs[OHCI_COMMAND_STATUS/4] = regs[OHCI_INTERRUPT_DISABLE/4] = 0;
    *va = regs;
    return 0;
}
void usbp_mmio_unmap(const struct usb_pci_fn *f, u32 bar) { (void)f; (void)bar; }
void usbp_udelay(u32 us)
{
    if (us == 1000 && regs[OHCI_COMMAND_STATUS/4] == OHCI_CMD_OCR)
        regs[OHCI_CONTROL/4] = 0;
}
int usbp_cfg_cmd(const struct usb_pci_fn *f, u16 set, u16 clear)
{
    (void)f;
    if (clear == PCI_CMD_BUS_MASTER && regs[OHCI_CONTROL/4] == OHCI_CTL_RESET &&
        regs[OHCI_INTERRUPT_DISABLE/4] == (unsigned)OHCI_INTR_ALL &&
        regs[OHCI_COMMAND_STATUS/4] == OHCI_CMD_OCR) return 0;
    return set == PCI_CMD_MEM_ENABLE && !clear ? 0 : -1;
}
u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    if (bus || dev != 13 || reg != PCI_CFG_COMMAND) bad++;
    return stuck_bm && !fn ? PCI_CMD_BUS_MASTER : 0;
}
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
int ehci_early_quiesce(struct usb_hc *hc, u32 *blocked)
{ (void)hc; (void)blocked; bad++; return -1; }
static int isr(void *arg) { return usb_pending && ((struct usb_hc *)arg)->pci.fn == 0; }
static int peer_irq(unsigned irq, void *arg)
{
    (void)arg;
    peer_calls++;
    return irq == 5 ? IRQ_HANDLED : IRQ_NONE;
}
struct module_record *module_record_get(struct module_handle h) { (void)h; return &record; }
int module_ref_acquire(struct module_handle h, u32 owner, enum module_ref_kind kind, u32 fn)
{
    (void)h;
    if (owner != record.owner || kind != MODULE_REF_IRQ || fn != (uptr)isr) bad++;
    refs++;
    return 1;
}
int module_ref_release(struct module_handle h, enum module_ref_kind kind)
{ (void)h; (void)kind; refs--; return 1; }
int irq_register(unsigned irq, irq_handler_fn fn, void *arg, unsigned flags)
{
    if (irq != 5 || flags != IRQ_F_SHARED) return -1;
    if (!peer) { peer = fn; return 0; }
    if (handler) return -1;
    handler = fn; argument = arg;
    return 0;
}
int irq_unregister(unsigned irq, irq_handler_fn fn, void *arg)
{
    if (refuse_detach || irq != 5 || handler != fn || arg != argument) return -1;
    handler = NULL;
    return 0;
}
static int start(struct usb_hc *hc)
{
    starts++;
    if (hc->has_irq != (hc->pci.irq == 5)) bad++;
    return 0;
}
static int add(struct usb_hc *hc) { (void)hc; registered++; return fail_register; }
static void dead(struct usb_hc *hc, u32 why)
{ if (why != USB_WHY_START) bad++; hc->state = HC_DEAD; }
static void service(void) { services++; }

int main(void)
{
    static const struct usb_hc_ops ops = {.start=start};
    static struct pc98_usb_core core = {.ops=&ops,.isr=isr,.register_hc=add,.dead=dead,.service=service};
    struct module_handle h = {1, 7};
    pc98_usb_early_quiesce();
    if (pc98_usb_resident_start(h, &core) || maps || starts || refs || services) return 1;
    cards = 2;
    if (pc98_usb_resident_start(h, &core) || maps) return 7;
    pc98_usb_early_quiesce();
    if (maps != 2 || starts || refs) return 8;
    if (irq_register(5, peer_irq, NULL, IRQ_F_SHARED)) return 9;
    if (pc98_usb_resident_start(h, &core) != 2 || maps != 4 || starts != 2 ||
        registered != 2 || services != 1 || bad || refs != 1) return 2;
    if (!handler || handler(5, argument) != IRQ_HANDLED) return 3;
    usb_pending = 0;
    if (peer(5, NULL) != IRQ_HANDLED || handler(5, argument) != IRQ_NONE || peer_calls != 1)
        return 10;
    refuse_detach = 1;
    if (!usb_resident_irq_detach(&core.hc[0].pci) || refs != 1) return 4;
    refuse_detach = 0;
    if (usb_resident_irq_detach(&core.hc[0].pci) || refs || handler) return 5;
    fail_register = cards = 1;
    if (pc98_usb_resident_start(h, &core) || core.hc[0].state != HC_DEAD ||
        services != 1 || refs || handler || bad) return 6;
    cards = 2;
    stuck_bm = 1;
    fail_register = 0;
    pc98_usb_early_quiesce();
    if (pc98_usb_resident_start(h, &core) != 1 || core.hc[0].pci.fn != 1 || refs || bad)
        return 11;
    return 0;
}
