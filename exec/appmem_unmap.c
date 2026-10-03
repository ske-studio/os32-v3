/* T2f f4: public-policy unmap, host-linked only until f5. */
#include "appmem.h"
#include "paging_app.h"
#include "io.h"

int appmem_unmap(struct addrspace *as, struct appmem_table *table, u32 base, u32 bytes)
{
    struct appmem_unmap_plan plan;
    struct paging_app_unmap tx;
    if (!bytes || bytes % PAGE_SIZE || bytes > ~(u32)0 - base ||
        !paging_app_context(as)) return APPMEM_EINVAL;
    int rc = appmem_unmap_prepare(table, base, base + bytes, APPMEM_PUBLIC_UNMAP_MASK, &plan);
    if (rc) return rc;
    if (!appmem_unmap_plan_valid(table, &plan)) return APPMEM_EINVAL;
    rc = paging_app_unmap_prepare(&tx, as, base, base + bytes);
    if (rc) return rc;
    if (!appmem_unmap_plan_valid(table, &plan)) return APPMEM_EINVAL;
    unsigned int saved = irq_save();
    paging_app_unmap_withdraw(&tx);
    irq_restore(saved);
    rc = paging_app_unmap_free(&tx);
    if (rc) return rc;
    appmem_unmap_publish(table, &plan);
    return 0;
}
