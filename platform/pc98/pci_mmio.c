/* PC-98 USB BAR0. Boot owns reservations, controller start/stop owns maps.
 * Renesas S16265EJ5V0DS p.3, FreeBSD ohci_pci.c:225 / ehci_pci.c:349, USB_DESIGN §5-3. */
#include "../../drivers/pci/pci.h"
#include "../../drivers/pci/pci_barsize.h"
#include "../../mm/paging.h"
#include "../../mm/pgalloc.h"
#include "usb_platform.h"
#include "pci_resources.h"
#include "io.h"
#include "memmap.h"
#include "kprintf.h"

struct usb_page {
    u32 owner, rid, first, users;
};
static struct usb_window {
    u32 base, len;
    u16 bdf;
    struct usb_page *page;
    int mapped;
} windows[PCI_MAX_DEVS];
static struct usb_page pages[PCI_MAX_DEVS];
static u32 count;

/* Check every firmware decoder before reserving, including later functions. */
static int conflicts(const struct pci_dev *target, u32 base, u32 len)
{
    struct pci_dev d;
    u32 first = base & ~(PAGE_SIZE - 1);
    for (u32 i = 0; pci_get(i, &d) == 0; i++) {
        for (u32 b = 0; b <= PCI_CFG_BAR_COUNT; b++) {
            struct pci_bar_resource r;
            u32 reg = PCI_CFG_BAR0 + b * PCI_CFG_BAR_STRIDE;
            if (b == PCI_CFG_BAR_COUNT)
                reg = pci_header_layout(d.header) == PCI_HDR_LAYOUT_BRIDGE ?
                    PCI_CFG_ROM_BRIDGE : PCI_CFG_ROM_ENDPOINT;
            if (pci_bar_get(d.bus, d.dev, d.fn, reg, &r) ||
                !r.len || r.unassigned || r.kind == PCI_BAR_IO) continue;
            int same = target->bus == d.bus && target->dev == d.dev;
            if (same && d.fn == target->fn && b == 0) continue;
            int shared = same && d.fn != target->fn && !b && pc98_usb_bar_width(&d);
            unsigned long long lo = shared ? base : first;
            unsigned long long hi = lo + (shared ? len : PAGE_SIZE);
            if (r.base < hi && (r.base >= lo || r.len > lo - r.base)) return 1;
        }
    }
    return 0;
}

static const char *pci_mmio_reserve(const struct pci_dev *d, u32 width)
{
    struct pci_bar_resource bar;
    if (pci_bar_get(d->bus, d->dev, d->fn, PCI_CFG_BAR0, &bar) || !bar.len)
        return "missing BAR0 measurement";
    if (bar.unassigned) return "unassigned BAR0";
    if (bar.kind != PCI_BAR_MEM32 || bar.prefetch || bar.len != width ||
        (bar.base & (width - 1))) return "unexpected BAR0 type/size/alignment";
    u32 base = bar.base, first = base / PAGE_SIZE;
    if ((base >= MEM_APP_BAND_BASE && base < MEM_DEVICE_APERTURE_END) ||
        pgalloc_range_has_ram(first, first + 1) || paging_pte_raw(base) ||
        conflicts(d, base, width)) return "BAR0 conflict";
    u16 bdf = (d->bus << 8) | (d->dev << 3) | d->fn;
    struct usb_page *p = &pages[count];
    for (u32 i = 0; i < count; i++) {
        if (windows[i].page->first == first) p = windows[i].page;
    }
    if (p == &pages[count]) {
        /* Install an absent PTE only when the entire PT is missing. */
        if (!paging_registered_pt(base) && paging_set_page(base, 0, 0))
            return "shared PT unavailable";
        struct ledger_resource r = {.raw_bar=bar.raw, .decode_first=first,
            .decode_end=first+1, .bdf=bdf, .vendor=d->vendor, .device=d->device,
            .bus=LEDGER_BUS_PCI, .width_basis=LEDGER_WB_SIZING};
        /* Publish the window only after all three ledger operations succeed. */
        unsigned int flags = irq_save();
        const char *error = "resource table full";
        if (ledger_resource_add(&r, &p->rid)) {
            if (ledger_owner_new(LEDGER_KIND_DEVICE, bdf, "usb", &p->owner)) {
                struct ledger_span span = {first, first+1, LEDGER_SPAN_MMIO, p->rid};
                if (ledger_reserve_set(p->owner, &span, 1)) {
                    error = 0;
                } else {
                    ledger_owners[p->owner] = (struct ledger_owner){0};
                    error = "reservation refused";
                }
            }
            if (error) ledger_resources[p->rid] = (struct ledger_resource){0};
        }
        irq_restore(flags);
        if (error) return error;
        p->first = first;
    }
    windows[count++] = (struct usb_window){base, width, bdf, p, 0};
    return 0;
}

void pc98_usb_reserve(void)
{
    struct pci_dev d;
    for (u32 i = 0; pci_get(i, &d) == 0; i++) {
        u32 width = pc98_usb_bar_width(&d);
        if (!width) continue;
        const char *reason = pci_mmio_reserve(&d, width);
        if (reason) kprintf(KLOG_ATTR, "[usb] %u:%u.%u %s\n", d.bus, d.dev, d.fn, reason);
    }
}

static struct usb_window *lookup(const struct usb_pci_fn *f, u32 bar)
{
    if (!f || bar) return 0;
    for (u32 i = 0; i < count; i++)
        if (windows[i].bdf == ((f->bus << 8) | (f->dev << 3) | f->fn))
            return &windows[i];
    return 0;
}

int usbp_mmio_map(const struct usb_pci_fn *f, u32 bar, volatile void **va)
{
    struct usb_window *w = lookup(f, bar);
    if (!w || !va || w->base != f->bar_base[0] || w->len != f->bar_size[0])
        return -1;
    struct usb_page *p = w->page;
    unsigned int flags = irq_save();
    int rc = -1;
    if (!w->mapped && (p->users ||
        !paging_device_map(p->owner, p->rid, p->first, p->first + 1))) {
        p->users++;
        w->mapped = 1;
        *va = P2V_IO(w->base);
        rc = 0;
    }
    irq_restore(flags);
    return rc;
}

void usbp_mmio_unmap(const struct usb_pci_fn *f, u32 bar)
{
    struct usb_window *w = lookup(f, bar);
    if (!w) return;
    struct usb_page *p = w->page;
    unsigned int flags = irq_save();
    if (w->mapped && (p->users > 1 ||
        !paging_device_unmap(p->owner, p->rid, p->first, p->first + 1))) {
        p->users--;
        w->mapped = 0;
    }
    irq_restore(flags);
}
