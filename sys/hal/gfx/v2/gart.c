/* HAL v2 GART page table and ownership, HAL_V2_DESIGN §8. */
#include "gfx2_int.h"
#include "dma_contract.h"

#define GFX2_GART_KB 8192u
#define GART_SLOTS (GFX2_GART_KB * 1024u / MEM_PAGE_SIZE)

static u32 gart_pfns[GART_SLOTS];
static u32 gart_owners[GART_SLOTS];

int gfx2_gart_alloc(struct gfx2_adapter *a, struct gfx2_surf *s, u32 owner)
{
    u32 n, first, i, allocated = 0, pinned = 0, off;
    int rc;

    if (!s->bytes) return OS32_ERR_INVAL;
    n = s->bytes / MEM_PAGE_SIZE + (s->bytes % MEM_PAGE_SIZE != 0);
    if (n > GART_SLOTS) return OS32_ERR_NOSPC;
    for (first = 0; first <= GART_SLOTS - n; first++) {
        for (i = 0; i < n && !gart_pfns[first + i]; i++) {}
        if (i == n) break;
        first += i;
    }
    if (first > GART_SLOTS - n) return OS32_ERR_NOSPC;
    for (i = 0; i < n; i++) {
        if (!pgalloc_alloc_n_owner(owner, 1, 0, DMA_PHYS_LIMIT / MEM_PAGE_SIZE,
                                   LEDGER_BOTTOM_UP, &gart_pfns[first + i])) {
            rc = OS32_ERR_NOSPC;
            goto fail;
        }
        allocated++;
    }
    for (i = 0; i < n; i++) {
        if (!pgalloc_pin(gart_pfns[first + i], 1, owner)) {
            rc = OS32_ERR_IO;
            goto fail;
        }
        pinned++;
    }
    rc = a->be->gart_bind(a->ctx, &gart_pfns[first], n, &off);
    if (rc) goto fail;
    for (i = 0; i < n; i++) gart_owners[first + i] = owner;
    s->gart_first = first;
    s->gart_n = n;
    s->gart_moved = 0;
    s->gpu_off = off;
    return 0;
fail:
    while (pinned) {
        pinned--;
        pgalloc_unpin(gart_pfns[first + pinned], 1, owner);
    }
    while (allocated) {
        allocated--;
        pgalloc_free_n_owner(owner, gart_pfns[first + allocated], 1);
        gart_pfns[first + allocated] = 0;
    }
    return rc;
}

int gfx2_gart_pfns(const struct gfx2_surf *s, const u32 **pfn, u32 *n)
{
    if (s->pool != GFX2_POOL_GART || !s->gart_n) return OS32_ERR_INVAL;
    *pfn = &gart_pfns[s->gart_first];
    *n = s->gart_n;
    return 0;
}

int gfx2_gart_quarantine(struct gfx2_surf *s)
{
    u32 i, first = s->gart_first, owner = gart_owners[first];
    s->gart_moved = 0;
    if (!GFX2_QUAR_OWNER) return OS32_ERR_IO;
    for (i = 0; i < s->gart_n; i++)
        if (!pgalloc_page_owned(gart_pfns[first + i], owner) ||
            !pgalloc_pinned(gart_pfns[first + i])) return OS32_ERR_IO;
    for (i = 0; i < s->gart_n; i++) {
        if (!ledger_transfer_pinned(gart_pfns[first + i], 1, owner, GFX2_QUAR_OWNER))
            return OS32_ERR_IO;
        gart_owners[first + i] = GFX2_QUAR_OWNER;
        s->gart_moved++;
    }
    return 0;
}

int gfx2_gart_free(struct gfx2_adapter *a, struct gfx2_surf *s)
{
    u32 i, first = s->gart_first;
    u32 release_n = s->state == GFX2_ST_QUAR ? s->gart_moved : s->gart_n;
    if (a->state == GFX2_AD_LOST_HELD) return OS32_ERR_BUSY;
    if (a->be->gart_unbind(a->ctx, s->gpu_off, s->gart_n)) return OS32_ERR_IO;
    for (i = 0; i < s->gart_n; i++) {
        if (i < release_n) {
            pgalloc_unpin(gart_pfns[first + i], 1, gart_owners[first + i]);
            pgalloc_free_n_owner(gart_owners[first + i], gart_pfns[first + i], 1);
        }
        gart_pfns[first + i] = 0;
        gart_owners[first + i] = 0;
    }
    s->gart_first = s->gart_n = s->gart_moved = 0;
    return 0;
}
