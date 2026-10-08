/* T2f f4: private public-policy unmap. */
#include "appmem.h"
#include "paging_app.h"
#include "io.h"

static int unmap_mask(struct addrspace *as, struct appmem_table *table,
                      u32 base, u32 bytes, u32 mask)
{
    struct appmem_unmap_plan plan;
    struct paging_app_unmap tx;
    if (!bytes || bytes % PAGE_SIZE || bytes > ~(u32)0 - base ||
        !paging_app_context(as)) return APPMEM_EINVAL;
    int rc = appmem_unmap_prepare(table, base, base + bytes, mask, &plan);
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

int appmem_unmap(struct addrspace *as, struct appmem_table *table, u32 base, u32 bytes)
{
    return unmap_mask(as, table, base, bytes, APPMEM_PUBLIC_UNMAP_MASK);
}

int appmem_exec_unmap(struct addrspace *as, u32 base, u32 bytes)
{
    if (!paging_app_context(as)) return APPMEM_EINVAL;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) {
        const struct appmem_extent *e = &as->appmem.e[i];
        if (e->base == base && e->end - e->base == bytes &&
            (e->kind == APPMEM_EXEC_ARENA || e->kind == APPMEM_EXEC_LARGE))
            return unmap_mask(as, &as->appmem, base, bytes, APPMEM_KIND_MASK(e->kind));
    }
    return APPMEM_EINVAL;
}

int appmem_exec_trim(struct addrspace *as, u32 base, u32 bytes)
{
    if (!as) return APPMEM_EINVAL;
    return unmap_mask(as, &as->appmem, base, bytes, APPMEM_KIND_MASK(APPMEM_EXEC_ARENA));
}
