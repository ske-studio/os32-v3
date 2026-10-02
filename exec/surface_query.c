/* Dormant until T2e e11: value query and common query/lease authorization. */
#include "surface_query.h"
#include "appslot.h"
#include "con_sink.h"
#include "os32_kapi_shared.h"
#include "io.h"

STATIC_ASSERT(sizeof(struct surface_desc) == 60, surface_desc_size);
STATIC_ASSERT(sizeof(struct surface_query_result) == 244, surface_query_size);

int surface_query_error(int rc)
{
    if (!rc) return 0;
    if (rc == LEASE_STALE) return OS32_ERR_STALE;
    if (rc == LEASE_FULL) return OS32_ERR_FULL;
    if (rc == LEASE_NOMEM) return OS32_ERR_NOSPC;
    return OS32_ERR_INVAL;
}

int surface_query_authorize(const struct surface_query_source *s,
                            struct caller_access *out)
{
    struct caller_access c;
    AppSlot *slot;
    int gui, gfx;
    if (!s || !out || !s->ready || kctx_irq_depth || kctx_exc_depth ||
        !caller_access_get(&c) || c.origin != CALLER_USER)
        return OS32_ERR_INVAL;
    slot = appslot_get(c.app_id);
    /* get() checks the saved AS/owner/generation/current CR3. Require the
     * running state too: parked parents are not foreground beneficiaries. */
    if (!slot || slot->state != APP_STATE_RUNNING) return OS32_ERR_INVAL;
    gui = con_sink_is_enabled();
    gfx = !!(slot->hdr_flags & OS32X_FLAG_GFX);
    switch (s->role) {
    case LEDGER_ROLE_CLIENT:
        if (gui && gfx && appslot_gfx_owner() != c.app_id) return OS32_ERR_INVAL;
        break;
    case LEDGER_ROLE_DISPLAY:
        if (!gfx || (gui && appslot_gfx_owner() != c.app_id)) return OS32_ERR_INVAL;
        break;
    case LEDGER_ROLE_TVRAM:
        if (gui) return OS32_ERR_INVAL;
        break;
    case LEDGER_ROLE_UNICODE:
        break;
    default:
        return OS32_ERR_INVAL;
    }
    if (s->role == LEDGER_ROLE_CLIENT || s->role == LEDGER_ROLE_DISPLAY) {
        if (s->backend < LEDGER_SF_PC98 || s->backend > LEDGER_SF_CIRRUS)
            return OS32_ERR_INVAL;
    } else if (s->backend) return OS32_ERR_INVAL;
    *out = c;
    return 0;
}

/* IRQ-held bounded snapshot: at most four records, no allocation or walk of
 * backing PFNs here. The ledger publisher has already validated backing. */
static int surface_snapshot(const struct surface_query_source *s,
                            struct surface_query_result *out)
{
    u32 i, j, want = s->role == LEDGER_ROLE_DISPLAY &&
                    s->backend == LEDGER_SF_PC98 ? SURFACE_QUERY_MAX : 1;
    if (s->count != want) return OS32_ERR_INVAL;
    for (i = 0; i < s->count; i++) {
        const struct surface_ref *r = &s->refs[i];
        const struct ledger_surface *sf;
        struct surface_desc *d = &out->desc[i];
        if (r->sid >= LEDGER_MAX_SURFACES || !r->generation) return OS32_ERR_INVAL;
        for (j = 0; j < i; j++)
            if (s->refs[j].sid == r->sid) return OS32_ERR_INVAL;
        sf = &ledger_surfaces[r->sid];
        if (sf->gen != r->generation || sf->closing || !sf->npages)
            return OS32_ERR_STALE;
        if (sf->role != s->role || sf->backend != s->backend ||
            !sf->planes || sf->planes > SURFACE_QUERY_MAX ||
            !sf->width || !sf->height || !sf->pitch ||
            sf->npages > ~(u32)0 / PAGE_SIZE ||
            (sf->perm_max != LEDGER_PERM_RO && sf->perm_max != LEDGER_PERM_RW) ||
            (s->role == LEDGER_ROLE_UNICODE && sf->perm_max != LEDGER_PERM_RO))
            return OS32_ERR_INVAL;
        d->ref = *r;
        d->role = sf->role; d->backend = sf->backend; d->format = sf->format;
        d->width = sf->width; d->height = sf->height; d->pitch = sf->pitch;
        d->planes = sf->planes; d->bytes = sf->npages * PAGE_SIZE;
        d->access_max = sf->perm_max;
        for (j = 0; j < sf->planes; j++) {
            u32 bytes = (u32)sf->pitch * sf->height;
            if (sf->plane_offset[j] > d->bytes || bytes > d->bytes - sf->plane_offset[j])
                return OS32_ERR_INVAL;
            d->plane_offset[j] = sf->plane_offset[j];
        }
    }
    out->count = s->count;
    return 0;
}

int surface_query_refs(const struct surface_query_source *s,
                       const struct surface_ref *refs, u32 count, u32 access)
{
    struct caller_access c;
    struct surface_query_result snap = {0};
    u32 i, j;
    int stale = 0;
    unsigned int flags = irq_save();
    int rc = surface_query_authorize(s, &c);
    if (rc) goto done;
    if (!refs || count != s->count || count > SURFACE_QUERY_MAX ||
        (access != LEDGER_PERM_RO && access != LEDGER_PERM_RW)) {
        rc = OS32_ERR_INVAL; goto done;
    }
    /* Validate the whole bundle before reporting any stale generation. */
    for (i = 0; i < count; i++) {
        if (refs[i].sid >= LEDGER_MAX_SURFACES || !refs[i].generation) {
            rc = OS32_ERR_INVAL; goto done;
        }
        for (j = 0; j < i; j++)
            if (refs[j].sid == refs[i].sid) { rc = OS32_ERR_INVAL; goto done; }
    }
    rc = surface_snapshot(s, &snap);
    if (rc) goto done;
    for (i = 0; i < count; i++) {
        const struct ledger_surface *sf;
        if (refs[i].sid != snap.desc[i].ref.sid) {
            sf = &ledger_surfaces[refs[i].sid];
            if (sf->gen == refs[i].generation && !sf->closing && sf->npages) {
                rc = OS32_ERR_INVAL; goto done;
            }
            stale = 1;
        } else if (refs[i].generation != snap.desc[i].ref.generation) {
            stale = 1;
        }
    }
    if (stale) { rc = OS32_ERR_STALE; goto done; }
    for (i = 0; i < count; i++) {
        if (access == LEDGER_PERM_RW && snap.desc[i].access_max != LEDGER_PERM_RW) {
            rc = OS32_ERR_INVAL; goto done;
        }
    }
done:
    irq_restore(flags);
    return rc;
}

int surface_query(const struct surface_query_source *s,
                  struct surface_query_result *user_out)
{
    struct caller_access c;
    struct surface_query_result snap = {0};
    unsigned int flags = irq_save();
    int rc = surface_query_authorize(s, &c);
    if (rc) goto done;
    if (!check_caller_write_range(&c, user_out, sizeof(snap))) {
        rc = OS32_ERR_INVAL; goto done;
    }
    rc = surface_snapshot(s, &snap);
    if (!rc && !copy_to_caller(&c, user_out, &snap, sizeof(snap)))
        rc = OS32_ERR_INVAL;
done:
    irq_restore(flags);
    return rc;
}
