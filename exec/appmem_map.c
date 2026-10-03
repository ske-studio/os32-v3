/* T2f f3: one serialized transaction; unlinked from kernel until f5. */
#include "appmem.h"
#include "paging_app.h"
#include "io.h"

int appmem_map(struct addrspace *as, struct appmem_table *table,
               const struct appmem_layout *layout, u32 bytes, u32 hint,
               u32 map_flags, u32 kind, u32 extent_flags, u32 *base_out)
{
    struct appmem_plan plan;
    struct paging_app_stage tx;
    if (!base_out || !paging_app_context(as)) return APPMEM_EINVAL;
    int rc = appmem_prepare(table, layout, bytes, hint, map_flags, kind, extent_flags, &plan);
    if (rc) return rc;
    if (!appmem_plan_valid(table, &plan)) return APPMEM_EINVAL;
    rc = paging_app_stage(&tx, as, plan.base, plan.end);
    if (rc) return rc;
    if (!appmem_plan_valid(table, &plan)) {
        paging_app_abort(&tx);
        return APPMEM_EINVAL;
    }
    unsigned int saved = irq_save();
    paging_app_commit(&tx);
    appmem_publish(table, &plan);
    if (paging_current_cr3() == as->pd_phys) paging_load_cr3(as->pd_phys);
    irq_restore(saved);
    *base_out = plan.base;
    return 0;
}
