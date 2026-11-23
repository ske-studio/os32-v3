/* PowerMac HW_OP §4, §5 stage 4. PCI Bus Binding to IEEE 1275 §2.2.1.
 * Only the read-only OpenHCI 1.0a §7.1.1 revision register is admitted. */
#include "hwb_resource.h"
#include "kboot_info.h"
#include "hwb_window.h"

struct hwb_assignment {
    u32 pa, bdf, base;
    u64 bytes;
    int bar0, ohci;
};
static struct hwb_assignment resources[PM_PCI_ASSIGNED];
static unsigned resource_count;

static int translate(const struct pm_pci_capture *c, int bridge, unsigned space,
                     u64 base, u64 bytes, u32 *pa)
{
    while (bridge >= 0) {
        const struct pm_of_range *match = 0;
        if (c->bridge[bridge].incomplete) return 0;
        for (unsigned i = 0; i < c->ranges; i++) {
            const struct pm_of_range *r = &c->range[i];
            if (r->bridge != bridge || r->space != space ||
                base + bytes <= r->base || r->base + r->bytes <= base) continue;
            if (match || base < r->base || base + bytes > r->base + r->bytes) return 0;
            match = r;
        }
        if (!match) return 0;
        base = match->parent_base + (base - match->base);
        space = match->parent_space;
        bridge = c->bridge[bridge].parent;
    }
    if (base > U32_MAX || bytes-1 > U32_MAX-base) return 0;
    *pa = base;
    return 1;
}

void pm_hwb_resource_init(const struct pm_kboot_info *info)
{
    const struct pm_pci_capture *c = &info->pci;
    int root = -1;
    unsigned used = 0;
    resource_count = 0;
    if (c->incomplete) return;
    for (unsigned i = 0; i < c->bridges; i++) {
        if (c->bridge[i].incomplete) return;
        if (root < 0 && c->bridge[i].parent < 0 && c->bridge[i].supported)
            root = i;
    }
    if (root < 0 || !pm_bridge_lookup(c->bridge[root].id)) return;
    for (unsigned i = 0; i < c->assigned; i++) {
        const struct pm_of_assignment *a = &c->assignment[i];
        const struct pm_of_function *f = &c->function[a->function];
        unsigned space = (a->hi >> 24) & 3;
        if (!space) continue;
        u32 pa;
        if (!translate(c, f->bridge, space, a->base, a->bytes, &pa)) return;
        u32 bdf = (a->hi >> 8) & HW_BDF_MASK;
        const struct pm_of_bridge *bridge = &c->bridge[f->bridge];
        int bar0 = bridge->host == root && (a->hi & 255) == 0x10;
        int ohci = bar0 && HW_BDF_BUS(bdf) >= bridge->first && HW_BDF_BUS(bdf) <= bridge->last &&
                   space == 2 && a->base <= U32_MAX && a->bytes >= 4 && !(pa & 3) &&
                   f->vendor == 0x106b && f->device == 0x003f && f->class_code == 0x0c0310;
        for (unsigned j = 0; ohci && j < info->ram.count; j++) {
            const struct pm_region *r = &info->ram.v[j];
            if (pa < (u64)r->start + r->size && r->start < (u64)pa + 4) ohci = 0;
        }
        resources[used++] = (struct hwb_assignment){
            pa, bdf, (u32)a->base, a->bytes, bar0, ohci};
    }
    resource_count = used;
}

u32 pm_hwb_phys_check(u32 addr, u32 width, u32 bytes)
{
    if (width != 4 || bytes != 4 || (addr & 3)) return HW_WINDOW;
    unsigned owners = 0;
    int admitted = 0;
    for (unsigned i = 0; i < resource_count; ++i) {
        const struct hwb_assignment *r = &resources[i];
        if ((u64)addr + bytes <= r->pa || (u64)r->pa + r->bytes <= addr) continue;
        owners++;
        admitted |= r->ohci && addr == r->pa;
    }
    return owners == 1 && admitted ? HW_OK : HW_WINDOW;
}

u32 pm_hwb_bar(u32 bdf, u32 bar, u32 space, u32 base, u32 offset, u32 bytes, u32 *addr)
{
    if (space == HW_IO) return HW_NOSYS;
    if (bar || space != HW_PHYS) return HW_WINDOW;
    const struct hwb_assignment *found = 0;
    for (unsigned i = 0; i < resource_count; ++i) {
        const struct hwb_assignment *r = &resources[i];
        if (!r->bar0 || r->bdf != bdf) continue;
        if (found) return HW_WINDOW;
        found = r;
    }
    if (!found || !found->ohci || found->base != base) return HW_WINDOW;
    if (!bytes || (u64)offset + bytes > found->bytes) return HW_ARG;
    if (offset || pm_hwb_phys_check(found->pa, 4, bytes) != HW_OK) return HW_WINDOW;
    *addr = found->pa;
    return HW_OK;
}

u32 pm_hwb_phys(struct hw_req *req)
{
    if (req->action != HW_READ || pm_hwb_phys_check(req->addr, req->width, req->width))
        return HW_WINDOW;
    return pm_hwb_window_transfer(req->addr, req->width, 1, 0, 1, 0, &req->value);
}
