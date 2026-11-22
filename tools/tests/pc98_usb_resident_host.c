#include "usb_resident.h"
#include "usb_probe.h"
#include "input/hal/in2_int.h"
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
struct module_record module_records[MODULE_RECORD_CAP] = {[1] = {.owner=17, .generation=7}};
#define record module_records[1]

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
static int module_case;
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
    if (h.slot != 1 || h.generation != 7) bad++;
    if (owner != record.owner || kind != MODULE_REF_IRQ || (!module_case && fn != (uptr)isr)) bad++;
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

static struct usb_hc *module_hc;
static u32 collect(struct usb_hc *hc) { return hc == module_hc ? usb_pending : 0; }
const struct usb_hc_ops ohci_ops = {.start = start, .intr_collect = collect};
int usb_hc_register(struct usb_hc *hc) { module_hc = hc; return add(hc); }
void usb_service(void) { service(); }
void usb_hc_dead(struct usb_hc *hc, u32 why) { dead(hc, why); }

static unsigned events, released, repeats;
int input_src_register(u8 kind, u8 origin, u16 *generation)
{
    if (origin != INPUT_ORIGIN_USB) bad++;
    *generation = 100 + kind;
    return 8 + kind;
}
void input_src_release(u8 src, u16 gen)
{
    if (gen != src + 92) bad++;
    released++;
}
u32 in2_time_ms(void) { return 55; }
int input_capture(const InputEvent *e)
{
    if (e->time_ms != 55 || e->generation != e->src + 92) bad++;
    if (events == 0 && (e->src != 9 || e->type != INPUT_EVENT_KEY || e->sc != 33 || !e->down)) bad++;
    if (events == 1 && (e->src != 10 || e->type != INPUT_EVENT_MOVE || e->x != -4 || e->y != 7)) bad++;
    if (events == 2 && (e->src != 10 || e->type != INPUT_EVENT_BUTTON || e->buttons != 5 || e->x || e->y)) bad++;
    events++;
    return 0;
}
void input_repeat(u8 id, u16 gen, u8 sc)
{
    if (id != 9 || gen != 101 || sc != 33) bad++;
    repeats++;
}
static int load_usb(void)
{
    u32 exports[2] = {1, (uptr)usb_module_init};
    return pc98_usb_module_start(&record, exports[1], exports);
}

static int module_boot(void)
{
    module_case = 1;
    pc98_usb_early_quiesce();
    if (load_usb() || maps || starts || services || registered || refs) return 21;
    cards = 1;
    pc98_usb_early_quiesce();
    if (irq_register(5, peer_irq, NULL, IRQ_F_SHARED) || load_usb() ||
        starts != 1 || services != 1 || registered != 1 || !handler || refs != 1 || bad) return 22;
    module_hc->isr_pending = 2;
    if (handler(5, argument) != IRQ_HANDLED || module_hc->isr_pending != 3) return 23;
    usb_pending = 0;
    if (handler(5, argument) != IRQ_NONE || module_hc->isr_pending != 3) return 24;
    int k = usbp_input_src_alloc(USBP_INPUT_KBD), m = usbp_input_src_alloc(USBP_INPUT_MOUSE);
    if (k != 1 || m != 1) return 25;
    usbp_input_key(k, 33, 1);
    usbp_input_key_repeat(k, 33);
    usbp_input_mouse(m, -4, 7, 5);
    usbp_input_release(USBP_INPUT_KBD, k);
    usbp_input_release(USBP_INPUT_MOUSE, m);
    if (events != 3 || released != 2 || repeats != 1 || bad) return 26;
    return 0;
}

int main(int argc, char **argv)
{
    static const struct usb_hc_ops ops = {.start=start};
    static struct pc98_usb_core core = {.ops=&ops,.isr=isr,.register_hc=add,.dead=dead,.service=service};
    struct module_handle h = {1, 7};
    (void)argv;
    if (argc > 1) return module_boot();
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
