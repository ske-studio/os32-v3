/* OF-PPC 2.1 text:338,357: architected vectors are not claimed; general
 * OF-time allocations require claim. This path writes neither vectors nor HPT
 * until detach, and never reclaims the loader's PT_LOAD (including BSS/stack).
 * Separate RAM/available: OpenBSD macppc/macppc/ofw_machdep.c:136-153.
 * Unlike BSD's general memory adapter, admit only the design's fixed window. */
#include "kboot_info.h"
#include "kboot_layout.h"
#define PM_ADMITTED_END 0x01000000U
#define PM_WINDOW_END 0x00f00000U
#define PM_VECTOR_END 0x00003000U
#define PM_HPT_LIMIT 0x02000000U
#define PM_HPT_BYTES 0x00020000U

static int valid(const struct pm_regions *r)
{
    if (!r->count || r->count>PM_REGION_COUNT) return 0;
    for (unsigned i=0; i<r->count; i++) {
        if (!r->v[i].size || r->v[i].size>0xffffffffU-r->v[i].start)
            return 0;
        if (i && r->v[i].start<r->v[i-1].start+r->v[i-1].size)
            return 0;
    }
    return 1;
}
/* Walk interval boundaries, never infer continuity from total RAM size. */
static int covered(const struct pm_regions *r, u32 first, u32 end)
{
    for (unsigned i=0; i<r->count && first<end; i++) {
        u32 stop=r->v[i].start+r->v[i].size;
        if (stop<=first) continue;
        if (r->v[i].start>first) return 0;
        first=stop;
    }
    return first>=end;
}
int pm_kboot_reserve(struct pm_kboot_info *info)
{
    info->admitted_end=info->image_first=info->image_end=0;
    info->hpt_pa=info->hpt_bytes=0;
    /* Last OF query; caller may subsequently only output/drain and detach.
     * The returned available remains a firmware snapshot, not an allocator. */
    pm_kboot_refresh_available(info);
    if (!valid(&info->ram) || !valid(&info->available))
        return PM_KBOOT_RESERVE_RANGES;
    u32 first=(u32)__kernel_image_start, end=(u32)&__kernel_resident_end;
    if (first!=PM_KERNEL_BASE || end<=first || end>PM_WINDOW_END ||
        !covered(&info->ram,first,end)) return PM_KBOOT_RESERVE_IMAGE;
    for (unsigned i=0; i<info->available.count; i++) {
        const struct pm_region *r=&info->available.v[i];
        if (r->start<end && r->start+r->size>first)
            return PM_KBOOT_RESERVE_IMAGE_AVAILABLE;
    }
    if (!covered(&info->ram,0,PM_VECTOR_END)) return PM_KBOOT_RESERVE_VECTOR;
    if (!covered(&info->ram,PM_KERNEL_BASE,PM_ADMITTED_END) ||
        !covered(&info->available,end,PM_WINDOW_END))
        return PM_KBOOT_RESERVE_WINDOW;
    u32 hpt=0;
    for (unsigned i=0; i<info->available.count; i++) {
        const struct pm_region *r=&info->available.v[i];
        u32 start=r->start<PM_ADMITTED_END?PM_ADMITTED_END:r->start;
        u32 stop=r->start+r->size;
        if (stop>PM_HPT_LIMIT) stop=PM_HPT_LIMIT;
        if (start>=stop) continue;
        start=(start+PM_HPT_BYTES-1)&~(PM_HPT_BYTES-1);
        if (start<=stop && stop-start>=PM_HPT_BYTES &&
            covered(&info->ram,start,start+PM_HPT_BYTES)) { hpt=start; break; }
    }
    if (!hpt) return PM_KBOOT_RESERVE_HPT;
    info->admitted_end=PM_ADMITTED_END;
    info->image_first=first; info->image_end=end;
    info->hpt_pa=hpt; info->hpt_bytes=PM_HPT_BYTES;
    return PM_KBOOT_RESERVE_OK;
}
