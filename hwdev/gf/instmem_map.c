/* BAR2 aperture lifecycle. nouveau nvkm/subdev/instmem/nv40.c:238-253
 * maps the instance resource; OS32 retains its boot reservation and shared PTs. */
#include "foundation.h"
#include "../../mm/paging.h"
#include "../../include/memmap.h"
#include "../../drivers/video/geforce/geforce_module.h"

static int production_map(void *owner, const struct recover_bar_extent *bar,
                          u32 bytes, volatile u8 **out)
{
    (void)owner;
    u32 resource_owner, rid;
    if (!bar || !out || !bytes || ((bar->base | bytes) & (PAGE_SIZE - 1)) ||
        geforce_resource_lookup(bar->base, bytes, &resource_owner, &rid))
        return -1;
    u32 first = bar->base / PAGE_SIZE;
    if (paging_device_map(resource_owner, rid, first, first + bytes / PAGE_SIZE))
        return -1;
    *out = P2V_IO(bar->base);
    return 0;
}

static int production_unmap(void *owner, volatile u8 *ptr, u32 bytes)
{
    (void)owner;
    u32 base = V2P(ptr);
    u32 resource_owner, rid;
    if (!bytes || ((base | bytes) & (PAGE_SIZE - 1)) ||
        geforce_resource_lookup(base, bytes, &resource_owner, &rid))
        return -1;
    u32 first = base / PAGE_SIZE;
    return paging_device_unmap(resource_owner, rid, first, first + bytes / PAGE_SIZE);
}

const struct gf_instmem_map_ops gf_production_instmem_map_ops = {
    .map = production_map, .unmap = production_unmap
};

int gf_instmem_map(struct gf_foundation *f, const struct recover_bar_extent *bar,
                   u32 bytes)
{
    volatile u8 *ptr;
    if (!f || !bar || !f->map_ops || !f->map_ops->map || f->mapped ||
        !bar->valid || !bytes || (bar->base & (PAGE_SIZE - 1)) ||
        (bar->length & (PAGE_SIZE - 1)) || (bytes & (PAGE_SIZE - 1)) ||
        bytes > bar->length || bar->length - 1 > (u32)-1 - bar->base)
        return GF1_EMAP;
    if (f->map_ops->map(f->map_owner, bar, bytes, &ptr)) return GF1_EMAP;
    f->bar2 = ptr;
    f->imem.aperture_bytes = bytes;
    f->mapped = true;
    return GF1_OK;
}

int gf_instmem_unmap(struct gf_foundation *f)
{
    if (!f || !f->mapped || !f->map_ops || !f->map_ops->unmap ||
        f->map_ops->unmap(f->map_owner, f->bar2, f->imem.aperture_bytes))
        return GF1_EMAP;
    f->mapped = false;
    f->bar2 = 0;
    f->imem.aperture_bytes = 0;
    return GF1_OK;
}
