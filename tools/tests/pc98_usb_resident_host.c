#include "usb_resident.h"
#include "usb_probe.h"
#include "input/hal/in2_int.h"
#include "drivers/usb/ohci_priv.h"
#include "kprintf.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_barsize.h"
#include "drivers/usb/ohci_regs.h"
#include "kernel/irq/irq.h"

#include "kernel/irq/tick.h"
#include "tick_order.h"
#include "kernel/irq/tick_budget.h"
static tick_fn periodic;
static int budget_ready;
u32 tick_budget_cnt(u32 us) { return us == IRQ0_SLOT_BUDGET_US ? 77 : 0; }
int tick_budget_peek(u32 share, u32 tail) { return share == 77 && !tail && budget_ready; }
int tick_register(unsigned slot, tick_fn fn)
{
    if (slot != PC98_TICK_USB || periodic) return -1;
    periodic = fn;
    return 0;
}

static unsigned regs[64], cards, maps, starts, services, registered, bad;
static int fail_register, refuse_detach;
static unsigned stuck_bm, usb_pending = 1, peer_calls;
static irq_handler_fn peer, handler;
static void *argument;

#define record module_records[1]
#define refs record.irq_refs
static int fail_map, fail_start, reclaimed;

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
    if (fail_map) return -1;
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
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{ return owner == 17 && pfn == 2 && n == 1; }
int ledger_reclaim_owner(u32 owner, u32 *pages)
{
    if (owner != 17 || refs || handler) bad++;
    *pages = record.pages;
    reclaimed++;
    return 1;
}
int ledger_owner_retire(u32 owner) { return owner == 17; }
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
    if (fail_start) return -USB_E_HC;
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

int kbd_shim_irq_depth;
volatile int kbd_gui_mode = 1;
static unsigned raw;
int legacy_active(void) { return 0; }
u32 in2_time_ms(void) { return 55; }
int legacy_stop_filter(int sc, int br, int mods)
{ (void)sc; (void)br; (void)mods; return 0; }
void legacy_raw_push(int sc, int br, int mods)
{ (void)sc; (void)br; (void)mods; raw++; }
void legacy_cooked_push(u16 entry) { (void)entry; bad++; }

extern char __executable_start, etext;
static void relocated(void)
{
    record = (struct module_record){
        .state = MODULE_RELOCATED, .generation = 7, .owner = 17,
        .init = (uptr)usb_module_init, .lifecycle.start = pc98_usb_module_start,
        .text = (uptr)&__executable_start, .text_size = (uptr)&etext - (uptr)&__executable_start,
        .backing = 8192, .pages = 2, .prefix_pages = 1
    };
}

static int module_boot(void)
{
    struct module_handle h = {1, 7};
    relocated();
    pc98_usb_early_quiesce();
    if (module_start(h) || record.state != MODULE_RUNNING ||
        periodic || maps || starts || services || registered || refs) return 21;
    cards = 1;
    pc98_usb_early_quiesce();
    if (irq_register(5, peer_irq, NULL, IRQ_F_SHARED)) return 22;
    relocated();
    fail_map = 1;
    if (module_start(h) != MODULE_INIT_FAILED || record.state != MODULE_ABSENT ||
        reclaimed != 1 || starts || services || bad) return 27;
    fail_map = 0;
    relocated();
    fail_start = 1;
    if (module_start(h) != MODULE_INIT_FAILED || record.state != MODULE_ABSENT ||
        reclaimed != 2 || handler || refs || services || bad) return 28;
    fail_start = 0;
    relocated();
    if (module_start(h) || record.state != MODULE_RUNNING ||
        starts != 2 || services != 1 || registered != 1 || !handler || refs != 1 || bad) return 22;
    if (!periodic || record.tick_refs != 1) return 33;
    periodic();
    if (services != 1) return 35;
    budget_ready = 1;
    periodic();
    if (services != 2) return 34;
    module_hc->isr_pending = 2;
    if (handler(5, argument) != IRQ_HANDLED || module_hc->isr_pending != 3) return 23;
    usb_pending = 0;
    if (handler(5, argument) != IRQ_NONE || module_hc->isr_pending != 3) return 24;
    int k = usbp_input_src_alloc(USBP_INPUT_KBD), m = usbp_input_src_alloc(USBP_INPUT_MOUSE);
    if (k != 1 || m != 1) return 25;
    usbp_input_key(k, 33, 1);
    usbp_input_key_repeat(k, 33);
    usbp_input_mouse(m, -4, 7, 5);
    InputEvent key, move, button, up;
    u16 seq;
    if (!in2_ring_pop(&key, &seq) || key.type != INPUT_EVENT_KEY || key.sc != 33 ||
        !key.down || key.time_ms != 55 || !input_key_pressed(33) || raw != 2 ||
        !in2_ring_pop(&move, &seq) || move.type != INPUT_EVENT_MOVE || move.x != -4 || move.y != 7 ||
        !in2_ring_pop(&button, &seq) || button.type != INPUT_EVENT_BUTTON ||
        button.buttons != 5 || input_buttons() != 5) return 26;
    usbp_input_release(USBP_INPUT_KBD, k);
    usbp_input_release(USBP_INPUT_MOUSE, m);
    if (input_key_pressed(33) || input_buttons() || !in2_ring_pop(&up, &seq) ||
        up.type != INPUT_EVENT_KEY || up.sc != 33 || up.down || up.src != key.src ||
        !in2_ring_pop(&up, &seq) || up.type != INPUT_EVENT_BUTTON || up.buttons ||
        up.sc != 5 || up.src != button.src) return 29;
    if (usbp_input_src_alloc(USBP_INPUT_KBD) != k || usbp_input_src_alloc(USBP_INPUT_MOUSE) != m)
        return 30;
    usbp_input_key(k, 33, 1);
    usbp_input_mouse(m, 0, 0, 5);
    button.sc = 0; /* Capture computes the changed-button mask on delivery. */
    if (!in2_ring_pop(&up, &seq) || up.src != key.src || up.generation == key.generation ||
        !in2_ring_pop(&up, &seq) || up.src != button.src || up.generation == button.generation ||
        input_capture(&key) >= 0 || input_capture(&button) >= 0 || kbd_shim_irq_depth || bad) return 30;
    return 0;
}

static int failed_detach(void)
{
    struct module_handle h = {1, 7};
    cards = 1;
    pc98_usb_early_quiesce();
    if (irq_register(5, peer_irq, NULL, IRQ_F_SHARED)) return 31;
    relocated();
    fail_register = refuse_detach = 1;
    if (module_start(h) != MODULE_INIT_FAILED || record.state != MODULE_QUARANTINED ||
        !module_hc || module_hc->state != HC_DEAD || !handler || refs != 1 ||
        reclaimed || services || module_exports(h) || bad) return 31;
    return 0;
}

static int resident(void)
{
    static const struct usb_hc_ops ops = {.start=start};
    static struct pc98_usb_core core = {.ops=&ops,.isr=isr,.register_hc=add,.dead=dead,.service=service};
    struct module_handle h = {1, 7};
    relocated();
    record.state = MODULE_STARTING;
    pc98_usb_early_quiesce();
    if (pc98_usb_resident_start(h, &core) || maps || starts || refs || services) return 1;
    cards = 2;
    if (pc98_usb_resident_start(h, &core) >= 0 || maps) return 7;
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
    if (pc98_usb_resident_start(h, &core) >= 0 || core.hc[0].state != HC_DEAD ||
        services != 1 || refs || handler || bad) return 6;
    cards = 2;
    stuck_bm = 1;
    fail_register = 0;
    pc98_usb_early_quiesce();
    if (pc98_usb_resident_start(h, &core) != 1 || core.hc[0].pci.fn != 1 || refs || bad)
        return 11;
    return 0;
}

void _start(void)
{
    char mode;
    int bytes;
    __asm__ volatile("int $0x80" : "=a"(bytes) : "0"(3), "b"(0), "c"(&mode), "d"(1) : "memory");
    int rc;
    if (bytes != 1) rc = 32;
    else if (mode == 'q') rc = failed_detach();
    else if (mode == 'm') rc = module_boot();
    else rc = resident();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
