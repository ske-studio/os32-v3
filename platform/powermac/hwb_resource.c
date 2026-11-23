/* PowerMac HW_OP §4, §5 stage 4. PCI Bus Binding to IEEE 1275 §2.2.1.
 * OpenHCI 1.0a §7.1.1 HcRevision and the UniNorth GEM MAC config registers
 * (OpenBSD gemreg.h) permit aligned single-word reads. */
#include "hwb_resource.h"
#include "kboot_info.h"
#include "hwb_window.h"
#include "drivers/net/gem/gem.h"

struct hwb_assignment {
    u32 pa, bdf, base;
    u64 bytes;
    u8 bar0, little, writable, widths;
    u32 first_reg, reg_bytes;
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

void pm_hwb_resource_init(const struct pm_kboot_info *info, int gmac_verified)
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
        u32 first = 0, length = 0;
        if (bar0 && HW_BDF_BUS(bdf) >= bridge->first && HW_BDF_BUS(bdf) <= bridge->last &&
            space == 2 && a->base <= U32_MAX && !(pa & 3) && f->vendor == 0x106b) {
            if (f->device == 0x003f && f->class_code == 0x0c0310) length = 4;
            if (gmac_verified && f->device == 0x0021 && info->gmac.profile == PM_GMAC_PROFILE_UNINORTH &&
                info->gmac.node == (u32)f->id && info->gmac.vendor_device == 0x0021106b &&
                info->gmac.root_node == (u32)c->bridge[root].id &&
                info->gmac.bdf == bdf << 8 && info->gmac.bar_pa == pa &&
                info->gmac.bar_bus == a->base) {
                first = GEM_MAC_TX_CONFIG;
                length = GEM_MAC_RX_CONFIG + 4 - first;
            }
        }
        if (a->bytes <= first) length = 0;
        else if (a->bytes - first < length) length = a->bytes - first;
        for (unsigned j = 0; length && j < info->ram.count; j++) {
            const struct pm_region *r = &info->ram.v[j];
            if ((u64)pa + first < (u64)r->start + r->size &&
                r->start < (u64)pa + first + length) length = 0;
        }
        resources[used++] = (struct hwb_assignment){
            .pa=pa, .bdf=bdf, .base=(u32)a->base, .bytes=a->bytes, .bar0=bar0,
            .little=1, .widths=4, .first_reg=first, .reg_bytes=length};
    }
    resource_count = used;
}

/* Every intersecting assignment counts as an owner, including a device
 * whose byte order or register profile is unknown. */
static const struct hwb_assignment *lookup(u32 addr, u32 bytes)
{
    const struct hwb_assignment *owner = 0;
    if (!bytes || bytes-1 > U32_MAX-addr) return 0;
    for (unsigned i = 0; i < resource_count; ++i) {
        const struct hwb_assignment *r = &resources[i];
        u64 end = (u64)addr + bytes;
        if (end <= r->pa || (u64)r->pa + r->bytes <= addr) continue;
        u64 first = (u64)r->pa + r->first_reg;
        if (owner || addr < first || end > first + r->reg_bytes) return 0;
        owner = r;
    }
    return owner;
}

static int allows(const struct hwb_assignment *r, u32 addr, u32 width, u32 bytes)
{
    return r && (width == 1 || width == 2 || width == 4) && (r->widths & width) &&
           !(addr & (width-1)) && !(bytes & (width-1)) && (r->writable || bytes == width);
}

u32 pm_hwb_phys_check(u32 addr, u32 width, u32 bytes)
{
    return allows(lookup(addr, bytes), addr, width, bytes) ? HW_OK : HW_WINDOW;
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
    if (!found || !found->reg_bytes || found->base != base) return HW_WINDOW;
    if (!bytes || (u64)offset + bytes > found->bytes) return HW_ARG;
    if (lookup(found->pa + offset, bytes) != found) return HW_WINDOW;
    *addr = found->pa + offset;
    return HW_OK;
}

u32 pm_hwb_phys(struct hw_req *req)
{
    const struct hwb_assignment *r = lookup(req->addr, req->width);
    int store = req->action == HW_WRITE;
    if (!allows(r, req->addr, req->width, req->width) ||
        (req->action != HW_READ && !store) || (store && !r->writable))
        return HW_WINDOW;
    return pm_hwb_window_transfer(req->addr, req->width, r->little, store, 1, 0, &req->value);
}

u32 pm_hwb_block(struct hw_block_req *block)
{
    struct hw_req *req = &block->req;
    if (!req->width || !block->count || block->count > HW_BLOCK_BYTES / req->width)
        return HW_ARG;
    u32 bytes = block->count * req->width;
    const struct hwb_assignment *r = lookup(req->addr, bytes);
    if (!allows(r, req->addr, req->width, bytes) || !r->writable) return HW_WINDOW;
    return pm_hwb_window_transfer(req->addr, req->width, r->little, 1,
                                  block->count, block->increment, &req->value);
}
