/* Include the production unit so synthetic widths exercise the same reserve path. */
#include "pci_mmio.c"

static struct pci_dev devs[3];
static struct pci_bar_resource bars[3];
static unsigned n, resources, reserved, maps, unmaps, diagnostics;
struct ledger_resource ledger_resources[LEDGER_MAX_RESOURCES];
struct ledger_owner ledger_owners[LEDGER_MAX_OWNERS];
static int live, reject_reserve;
int pci_get(u32 i, void *out)
{
    if (i >= n) return -1;
    *(struct pci_dev *)out = devs[i];
    return 0;
}
int pci_bar_get(u32 bus, u32 dev, u32 fn, u32 reg, void *out)
{
    if (reg != PCI_CFG_BAR0 || fn >= n) return -1;
    *(struct pci_bar_resource *)out = bars[fn];
    return 0;
}
void kprintf(u8 attr, const char *fmt, ...) { diagnostics++; }
int pgalloc_range_has_ram(u32 first, u32 end) { return 0; }
u32 paging_pte_raw(u32 addr) { return 0; }
u32 paging_registered_pt(u32 addr) { return 0; }
int paging_set_page(u32 va, u32 pa, u32 flags) { return flags != 0; }
int ledger_resource_add(const struct ledger_resource *r, u32 *rid)
{
    for (u32 i = 0; i < LEDGER_MAX_RESOURCES; i++) {
        if (ledger_resources[i].bus) continue;
        if (r->decode_end != r->decode_first + 1) return 0;
        ledger_resources[i] = *r;
        *rid = i;
        resources++;
        return 1;
    }
    return 0;
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{
    for (u32 i = LEDGER_OWNER_FIXED_LAST + 1; i < LEDGER_MAX_OWNERS; i++) {
        if (ledger_owners[i].kind) continue;
        ledger_owners[i].kind = kind;
        *owner = i;
        return kind == LEDGER_KIND_DEVICE;
    }
    return 0;
}
int ledger_reserve_set(u32 owner, const struct ledger_span *s, u32 nspan)
{
    if (reject_reserve) return 0;
    reserved++;
    return nspan == 1 && s->kind == LEDGER_SPAN_MMIO;
}
int paging_device_map(u32 owner, u32 rid, u32 first, u32 end)
{
    maps++;
    if (live) return -1;
    live = 1;
    return 0;
}
int paging_device_unmap(u32 owner, u32 rid, u32 first, u32 end)
{
    unmaps++;
    if (!live) return -1;
    live = 0;
    return 0;
}
static void device(u32 fn, u16 id, u32 base, u32 len)
{
    devs[fn] = (struct pci_dev){.dev=12, .fn=fn, .vendor=0x1033, .device=id};
    bars[fn] = (struct pci_bar_resource){.raw=base, .base=base, .len=len,
        .kind=PCI_BAR_MEM32};
}
#define VERIFY(expr) do { if (!(expr)) return 1 + (__LINE__ % 126); } while (0)
int main(void)
{
    struct usb_pci_fn a = {.dev=12}, b = {.dev=12, .fn=2};
    volatile void *va;
    u32 base = 0x20000000;
    a.bar_base[0] = base;
    b.bar_base[0] = base + 256;
    a.bar_size[0] = b.bar_size[0] = 256;
    n = 3;
    device(0, 0x0035, base, 4096);
    device(2, 0x00e0, base + 256, 256);
    pc98_usb_reserve();
    VERIFY(count == 0 && diagnostics == 2);
    device(0, 0x0035, base + PAGE_SIZE, 4096);
    device(1, 0x0035, 0xfffff000, 4096);
    bars[1].unassigned = 1;
    pc98_usb_reserve();
    VERIFY(count == 2 && reserved == 2 && diagnostics == 3);
    VERIFY(usbp_mmio_map(&(struct usb_pci_fn){.dev=12, .fn=1}, 0, &va) < 0);
    VERIFY(!usbp_mmio_map(&b, 0, &va) && (uptr)va == base + 256);
    usbp_mmio_unmap(&b, 0);

    live = 0;
    count = resources = reserved = maps = unmaps = 0;
    device(0, 0x0035, base, 256);
    VERIFY(pci_mmio_reserve(&devs[0], OHCI_BAR_SIZE));
    bars[0].len = 0;
    VERIFY(pci_mmio_reserve(&devs[0], 256));
    bars[0].len = 256;
    bars[0].prefetch = 1;
    VERIFY(pci_mmio_reserve(&devs[0], 256));
    bars[0].prefetch = 0;
    devs[2].dev++;
    VERIFY(pci_mmio_reserve(&devs[0], 256));
    devs[2].dev--;
    VERIFY(!pci_mmio_reserve(&devs[0], 256) && !pci_mmio_reserve(&devs[2], 256));
    VERIFY(resources == 1 && reserved == 1);
    VERIFY(!usbp_mmio_map(&a, 0, &va) && (uptr)va == base);
    VERIFY(!usbp_mmio_map(&b, 0, &va) && (uptr)va == base + 256);
    usbp_mmio_unmap(&a, 0);
    VERIFY(live && maps == 1 && !unmaps);
    usbp_mmio_unmap(&b, 0);
    VERIFY(!live && unmaps == 1);
    /* Leave one resource slot. First exhaust owners, then refuse a span. */
    count = 0;
    for (u32 i = 0; i < LEDGER_MAX_RESOURCES; i++)
        ledger_resources[i].bus = LEDGER_BUS_PCI;
    u32 rid = LEDGER_MAX_RESOURCES - 1, owner = LEDGER_MAX_OWNERS - 1;
    ledger_resources[rid].bus = 0;
    for (u32 i = LEDGER_OWNER_FIXED_LAST + 1; i <= owner; i++)
        ledger_owners[i].kind = LEDGER_KIND_DEVICE;
    VERIFY(pci_mmio_reserve(&devs[0], 256));
    VERIFY(!count && !ledger_resources[rid].bus);
    ledger_owners[owner].kind = 0;
    reject_reserve = 1;
    VERIFY(pci_mmio_reserve(&devs[0], 256));
    VERIFY(!count && !ledger_resources[rid].bus && !ledger_owners[owner].kind);
    reject_reserve = 0;
    VERIFY(!pci_mmio_reserve(&devs[2], 256));
    VERIFY(count == 1 && ledger_resources[rid].bus && ledger_owners[owner].kind);
    return 0;
}
void _start(void)
{
    int rc = main();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
