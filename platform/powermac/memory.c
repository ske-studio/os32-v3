#include "platform_memory.h"
#include "kboot_info.h"
#include "memory.h"
#define PM_MEMORY_HPT_BYTES 0x00020000UL
#define PM_MEMORY_HPT_LIMIT 0x02000000UL

/* B2 owns reg/available validation; check its admitted extent against mm.
 * Like pc98/memory.c, high probe returns only confirmed high RAM's end PFN.
 * OpenBSD macppc/ofw_machdep.c:136-153 separates reg from available;
 * neither snapshot alone admits additional allocator pages here. */
int pm_memory_validate(const struct pm_kboot_info *info)
{
    if (!info || info->admitted_end != platform_memory_geometry.ram_ceiling ||
        info->admitted_end != PM_MEMORY_ADMITTED_END ||
        info->image_first != PM_MEMORY_COMPAT_END ||
        info->image_end <= info->image_first ||
        info->image_end > platform_memory_geometry.legacy_end)
        return 0;
    if (info->hpt_bytes != PM_MEMORY_HPT_BYTES ||
        info->hpt_pa < info->admitted_end ||
        info->hpt_pa > PM_MEMORY_HPT_LIMIT - PM_MEMORY_HPT_BYTES ||
        (info->hpt_pa & (PM_MEMORY_HPT_BYTES - 1)))
        return 0;
    return 1;
}
u32 platform_memory_probe_high(void)
{
    return 0;
}
