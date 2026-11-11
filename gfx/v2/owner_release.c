/* HAL v2 AS teardown and quarantine: HAL_V2_DESIGN §7-3. */
#include "gfx2_int.h"
#include "gfx2_exec.h"
#include "paging.h"

#define GFX2_RELEASE_DL 1000000u

/* Pages retained until reboot; refreshed across all adapters by drain. */
u32 gfx2_leak_pages;

int gfx2_surf_quarantine(struct gfx2_surf *s)
{
    int rc = 0;
    if (s->map_as) gfx2_map_detach(s);
    if (s->pool == GFX2_POOL_GART)
        rc = gfx2_gart_quarantine(s);
    s->owner = GFX2_QUAR_OWNER;
    (void)gfx2_surf_state_set(s, GFX2_ST_QUAR);
    return rc;
}

/* The table slot in a sid is not an ownership check: inspect the live sid. */
static int head_owned(u32 sid, u32 owner)
{
    const struct gfx2_surf *s = &gfx2_surfs[sid % GFX2_MAX_SURFACES];
    return s->state != GFX2_ST_FREE && s->sid == sid && s->owner == owner;
}

int gfx2_owner_release(struct addrspace *as)
{
    int poison = 0;
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++) {
        struct gfx2_surf *s = &gfx2_surfs[i];
        if (s->state == GFX2_ST_FREE ||
            (s->owner != as->owner && s->map_as != as)) continue;
        if (s->map_as == as) {
            if (gfx2_unmap(s)) {
                poison = 1;
                s->flags |= GFX2_SF_KEEP;
            }
            gfx2_map_detach(s);
        }
        gfx2_staging_drop(s);
        if (s->flags & GFX2_SF_ALIAS) s->flags &= ~GFX2_SF_KEEP;
        if (s->state == GFX2_ST_LOCKED_RO || s->state == GFX2_ST_LOCKED_RW)
            (void)gfx2_surf_state_set(s, GFX2_ST_IDLE);
    }
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++) {
        struct gfx2_adapter *a = gfx2_adapter_get(i);
        if (!a) continue;
        for (u32 h = 0; h < a->heads; h++) {
            if (head_owned(a->head[h].scan_sid, as->owner) ||
                head_owned(a->head[h].pend_sid, as->owner))
                (void)gfx2_head_restore_v1(a, h);
        }
    }
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++) {
        struct gfx2_surf *s = &gfx2_surfs[i];
        if (s->state == GFX2_ST_FREE || s->owner != as->owner) continue;
        if (!(s->flags & GFX2_SF_KEEP) && !s->scan_ref &&
            gfx2_surf_quiesce(s, GFX2_RELEASE_DL) == 0 &&
            gfx2_surf_destroy(s) == 0)
            continue;
        if (gfx2_surf_quarantine(s)) poison = 1;
    }
    gfx2_vaspace_drop(as);
    return poison ? OS32_ERR_IO : 0;
}

void gfx2_quarantine_drain(struct gfx2_adapter *a)
{
    gfx2_leak_pages = 0;
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++) {
        struct gfx2_surf *s = &gfx2_surfs[i];
        if (s->state != GFX2_ST_QUAR) continue;
        if (s->flags & GFX2_SF_KEEP) {
            gfx2_leak_pages += (s->bytes / MEM_PAGE_SIZE) +
                              (s->bytes % MEM_PAGE_SIZE != 0);
            continue;
        }
        if (s->adapter != a->index || a->state == GFX2_AD_LOST_HELD || s->scan_ref)
            continue;
        if (gfx2_fence_done(a, s->last_write) && gfx2_fence_done(a, s->last_read))
            (void)gfx2_surf_destroy(s);
    }
}
