#include "usb_resident.h"
#include "usb_probe.h"
#include "kprintf.h"
#include "idle_work.h"
#include "platform_ver.h"
#include "io.h"
#include "drivers/pci/pci.h"
#include "kernel/irq/irq.h"

#include "kernel/irq/tick.h"
#include "tick_order.h"
#include "kernel/irq/tick_budget.h"

#include "arch_tick_budget.h"

/* Provisional host model: allow 30 us per HC for completion and port scan. */
#define USB_SCAN_RESERVE_US (PC98_USB_HCS * 30u)
static u32 scan_reserve_cnt;
static struct module_handle tick_owner;
static void (*tick_target)(void);

/* The USB tick reference pins the module while events are pending. Its
 * format strings are literals. Service diagnostics use at most six numeric
 * arguments, and producers run with interrupts masked on this machine. */
#define USB_LOG_CAPACITY 32u
static struct usb_log_event {
    const char *fmt;
    u32 args[6];
} events[USB_LOG_CAPACITY];
static unsigned head, count;
static int first_tick, foreground_log, scan_pending;
static u32 tick_reach, tick_deny, tick_runs, idle_runs;

static void print_line(const char *fmt, const u32 *args)
{
    pc98_usb_tick_vprintf(KLOG_ATTR, fmt, args);
    pc98_usb_tick_vprintf(KLOG_ATTR, "\n", NULL);
}

void pc98_usb_service_log(const char *fmt, const u32 *args)
{
    if (!tick_target) {
        print_line(fmt, args);
        return;
    }
    if (count == USB_LOG_CAPACITY) return;
    struct usb_log_event *event = &events[(head + count) % USB_LOG_CAPACITY];
    unsigned n = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') continue;
        if (p[1] == '%') { p++; continue; }
        if (n == sizeof(event->args) / sizeof(event->args[0])) return;
        event->args[n] = args[n];
        n++;
    }
    event->fmt = fmt;
    count++;
}

static void log_drain_one(void)
{
    u32 flags = irq_save();
    if (!count) {
        irq_restore(flags);
        return;
    }
    struct usb_log_event event = events[head];
    head = (head + 1) % USB_LOG_CAPACITY;
    count--;
    irq_restore(flags);
    print_line(event.fmt, event.args);
}

void pc98_usb_tick_counts(struct usb_tick_counts *out)
{
    u32 flags = irq_save();
    *out = (struct usb_tick_counts){tick_reach, tick_deny, tick_runs, idle_runs};
    irq_restore(flags);
}

/* Include already observed events in the boot snapshot even before idle. */
void pc98_usb_flush_boot_log(void)
{
    u32 flags = irq_save();
    unsigned pending = count;
    u32 reach = tick_reach, deny = tick_deny, run = tick_runs, idle = idle_runs;
    irq_restore(flags);
    while (pending--) log_drain_one();
    kprintf(KLOG_ATTR, "[usb] tick reach=%u deny=%u run=%u idle=%u\n",
            reach, deny, run, idle);
}

/* Both callers hold the IRQ mask through the claim and service. The OHCI
 * handler either published isr_pending already or collects after restore. */
static void run_scan(void)
{
    scan_pending = 0;
    if (first_tick) {
        first_tick = 0;
        pc98_usb_service_log("[usb] first tick", NULL);
    }
    tick_target();
}

static void usb_idle(void)
{
    u32 flags = irq_save();
    if (scan_pending && tick_target) {
        idle_runs++;
        run_scan();
    }
    irq_restore(flags);
    log_drain_one();
}

static void usb_tick(void)
{
    if (!tick_target) return;
    tick_reach++;
    if (!tick_budget_peek(tick_budget_cnt(IRQ0_SLOT_BUDGET_US), scan_reserve_cnt)) {
        tick_deny++;
        scan_pending = 1;
        return;
    }
    tick_runs++;
    run_scan();
}

int usb_resident_tick_set(void (*service)(void))
{
    struct module_handle owner = tick_owner;
    struct module_record *r = module_record_get(owner);
    if (!service) {
        if (tick_target) {
            tick_target = NULL;
            scan_pending = 0;
            head = count = 0; /* Discard literals before releasing the owner. */
            module_ref_release(owner, MODULE_REF_TICK);
        }
        return 0;
    }
    if (tick_target == service && r && r->tick_refs) return 0;
    if (tick_target || !r ||
        !module_ref_acquire(owner, r->owner, MODULE_REF_TICK, (uptr)service))
        return -USB_E_HC;
    if (tick_register(PC98_TICK_USB, usb_tick)) {
        module_ref_release(owner, MODULE_REF_TICK);
        return -USB_E_HC;
    }
    scan_reserve_cnt = arch_tick_budget_cnt(USB_SCAN_RESERVE_US);
    if (!foreground_log) {
        foreground_log = idle_work_register(usb_idle) == 0;
        if (!foreground_log) kprintf(KLOG_ATTR, "[usb] idle unavailable, budgeted tick only\n");
    }
    head = count = 0;
    first_tick = 1;
    tick_target = service;
    return 0;
}

static struct usb_irq_slot {
    struct module_handle owner;
    struct usb_pci_fn pci;
    int (*isr)(void *);
    void *arg;
} slots[PC98_USB_HCS];

static int dispatch(unsigned irq, void *arg)
{
    struct usb_irq_slot *s = arg;
    (void)irq;
    return s->isr(s->arg) == USBP_IRQ_HANDLED ? IRQ_HANDLED : IRQ_NONE;
}

static int same_function(const struct usb_pci_fn *a, const struct usb_pci_fn *b)
{
    return a->bus == b->bus && a->dev == b->dev && a->fn == b->fn;
}

int usb_resident_irq_attach(struct module_handle owner, const struct usb_pci_fn *f,
                            int (*isr)(void *), void *arg)
{
    struct module_record *r = module_record_get(owner);
    struct usb_irq_slot *slot = NULL;
    for (unsigned i = 0; i < PC98_USB_HCS; i++) {
        if (slots[i].isr && same_function(&slots[i].pci, f)) return -1;
        if (!slots[i].isr) slot = &slots[i];
    }
    if (!slot || !r || !isr || !module_ref_acquire(owner, r->owner, MODULE_REF_IRQ, (uptr)isr))
        return -1;
    *slot = (struct usb_irq_slot){owner, *f, isr, arg};
    if (!irq_register(f->irq, dispatch, slot, IRQ_F_SHARED)) return 0;
    slot->isr = NULL;
    module_ref_release(owner, MODULE_REF_IRQ);
    return -1;
}

int usb_resident_irq_detach(const struct usb_pci_fn *f)
{
    for (unsigned i = 0; i < PC98_USB_HCS; i++) {
        struct usb_irq_slot *s = &slots[i];
        if (!s->isr || !same_function(&s->pci, f)) continue;
        if (irq_unregister(s->pci.irq, dispatch, s)) return -1;
        s->isr = NULL;
        module_ref_release(s->owner, MODULE_REF_IRQ);
        return 0;
    }
    return -1;
}

int pc98_usb_resident_start(struct module_handle owner, struct pc98_usb_core *core)
{
    struct pci_dev dev;
    unsigned used = 0;
    int active = 0, found = 0;
    for (u32 i = 0; used < PC98_USB_HCS && !pci_get(i, &dev); i++) {
        struct usb_hc *hc = &core->hc[used];
        if (!pc98_usb_is_ohci(&dev)) continue;
        found = 1;
        const char *stage = NULL;
        if (!pc98_usb_ohci_quiet(i, &hc->firmware)) stage = "quiesce";
        else if (pc98_usb_ohci_function(&dev, &hc->pci)) stage = "PCI/BAR";
        else if (usbp_mmio_map(&hc->pci, 0, &hc->regs)) stage = "BAR map";
        if (stage) {
            kprintf(KLOG_ATTR, "[usb] %u:%u.%u failed: %s\n", dev.bus, dev.dev, dev.fn, stage);
            continue;
        }
        hc->ops = core->ops;
        hc->has_irq = !usb_resident_irq_attach(owner, &hc->pci, core->isr, hc);
        if (!hc->has_irq)
            kprintf(KLOG_ATTR, "[usb] %u:%u.%u IRQ unavailable, polling\n", dev.bus, dev.dev, dev.fn);
        if (!hc->ops->start(hc)) {
            if (!core->register_hc(hc)) {
                active++;
                used++;
                continue;
            }
            kprintf(KLOG_ATTR, "[usb] %u:%u.%u HC register failed\n", dev.bus, dev.dev, dev.fn);
            core->dead(hc, USB_WHY_START);
        }
        if (hc->has_irq) usb_resident_irq_detach(&hc->pci);
        /* Keep failed HC storage too: quarantine or failed detach can retain it. */
        used++;
    }
    if (!found) kprintf(KLOG_ATTR, "[usb] no OHCI candidate\n");
    if (active && !tick_target) tick_owner = owner;
    if (active && (owner.slot != tick_owner.slot || owner.generation != tick_owner.generation ||
                   usb_resident_tick_set(core->service))) {
        kprintf(KLOG_ATTR, "[usb] tick register failed\n");
        for (unsigned i = 0; i < used; i++) {
            struct usb_hc *hc = &core->hc[i];
            if (hc->state == HC_RUNNING) core->dead(hc, USB_WHY_ABORT);
        }
        return -USB_E_HC;
    }
    if (active) core->service();
    return found && !active ? -USB_E_HC : active;
}

/* module_start supplies the validated entry and its owning record. */
int pc98_usb_module_start(struct module_record *record, u32 entry, u32 *exports)
{
    int (*init)(struct module_handle) = (int (*)(struct module_handle))(uptr)entry;
    struct module_handle owner = {record - module_records, record->generation};
    (void)exports;
    return init(owner);
}
