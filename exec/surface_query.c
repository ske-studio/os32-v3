/* KAPI v70: value query and common query/lease authorization. */
#include "surface_query.h"
#include "appslot.h"
#include "con_sink.h"
#include "os32_kapi_shared.h"
#include "io.h"

STATIC_ASSERT(sizeof(struct surface_desc) == 60, surface_desc_size);
STATIC_ASSERT(sizeof(struct surface_query_result) == 244, surface_query_size);

STATIC_ASSERT(sizeof(OS32_SurfaceRef) == sizeof(struct surface_ref), abi_surface_ref_size);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceRef, sid) ==
              __builtin_offsetof(struct surface_ref, sid), abi_surface_ref_sid);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceRef, generation) ==
              __builtin_offsetof(struct surface_ref, generation), abi_surface_ref_generation);
STATIC_ASSERT(sizeof(OS32_SurfaceDesc) == sizeof(struct surface_desc), abi_surface_desc_size);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, ref) ==
              __builtin_offsetof(struct surface_desc, ref), abi_surface_desc_ref);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, role) ==
              __builtin_offsetof(struct surface_desc, role), abi_surface_desc_role);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, backend) ==
              __builtin_offsetof(struct surface_desc, backend), abi_surface_desc_backend);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, format) ==
              __builtin_offsetof(struct surface_desc, format), abi_surface_desc_format);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, width) ==
              __builtin_offsetof(struct surface_desc, width), abi_surface_desc_width);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, height) ==
              __builtin_offsetof(struct surface_desc, height), abi_surface_desc_height);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, pitch) ==
              __builtin_offsetof(struct surface_desc, pitch), abi_surface_desc_pitch);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, planes) ==
              __builtin_offsetof(struct surface_desc, planes), abi_surface_desc_planes);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, plane_offset) ==
              __builtin_offsetof(struct surface_desc, plane_offset), abi_surface_desc_plane_offset);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, bytes) ==
              __builtin_offsetof(struct surface_desc, bytes), abi_surface_desc_bytes);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceDesc, access_max) ==
              __builtin_offsetof(struct surface_desc, access_max), abi_surface_desc_access_max);
STATIC_ASSERT(sizeof(OS32_SurfaceQueryResult) == sizeof(struct surface_query_result), abi_surface_query_result_size);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceQueryResult, count) ==
              __builtin_offsetof(struct surface_query_result, count), abi_surface_query_result_count);
STATIC_ASSERT(__builtin_offsetof(OS32_SurfaceQueryResult, desc) ==
              __builtin_offsetof(struct surface_query_result, desc), abi_surface_query_result_desc);
STATIC_ASSERT(sizeof(OS32_LeaseView) == sizeof(struct lease_view), abi_lease_view_size);
STATIC_ASSERT(__builtin_offsetof(OS32_LeaseView, token) ==
              __builtin_offsetof(struct lease_view, token), abi_lease_view_token);
STATIC_ASSERT(__builtin_offsetof(OS32_LeaseView, base) ==
              __builtin_offsetof(struct lease_view, base), abi_lease_view_base);
STATIC_ASSERT(__builtin_offsetof(OS32_LeaseView, bytes) ==
              __builtin_offsetof(struct lease_view, bytes), abi_lease_view_bytes);
STATIC_ASSERT(__builtin_offsetof(OS32_LeaseView, planes) ==
              __builtin_offsetof(struct lease_view, planes), abi_lease_view_planes);
STATIC_ASSERT(sizeof(OS32_LeaseResult) == sizeof(struct surface_lease_result), abi_surface_lease_result_size);
STATIC_ASSERT(__builtin_offsetof(OS32_LeaseResult, count) ==
              __builtin_offsetof(struct surface_lease_result, count), abi_surface_lease_result_count);
STATIC_ASSERT(__builtin_offsetof(OS32_LeaseResult, views) ==
              __builtin_offsetof(struct surface_lease_result, views), abi_surface_lease_result_views);
STATIC_ASSERT(OS32_SURFACE_MAX == SURFACE_QUERY_MAX, abi_surface_max);
STATIC_ASSERT(OS32_SURFACE_CLIENT == LEDGER_ROLE_CLIENT, abi_surface_client);
STATIC_ASSERT(OS32_SURFACE_DISPLAY == LEDGER_ROLE_DISPLAY, abi_surface_display);
STATIC_ASSERT(OS32_SURFACE_TVRAM == LEDGER_ROLE_TVRAM, abi_surface_tvram);
STATIC_ASSERT(OS32_SURFACE_UNICODE == LEDGER_ROLE_UNICODE, abi_surface_unicode);
STATIC_ASSERT(OS32_SURFACE_RO == LEDGER_PERM_RO, abi_surface_ro);
STATIC_ASSERT(OS32_SURFACE_RW == LEDGER_PERM_RW, abi_surface_rw);
STATIC_ASSERT(OS32_SURFACE_PC98 == LEDGER_SF_PC98, abi_surface_pc98);
STATIC_ASSERT(OS32_SURFACE_PEGC == LEDGER_SF_PEGC, abi_surface_pegc);
STATIC_ASSERT(OS32_SURFACE_CIRRUS == LEDGER_SF_CIRRUS, abi_surface_cirrus);
STATIC_ASSERT(OS32_SURFACE_TABLE == LEDGER_FMT_TABLE, abi_surface_table);
STATIC_ASSERT(OS32_SURFACE_TEXT == LEDGER_FMT_TEXT, abi_surface_text);

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

/* Public KAPI bodies; publishers and caller identity stay kernel-owned. */
#include "../gfx/gfx.h"
STATIC_ASSERT(OS32_SURFACE_PLANAR4 == GFX_FMT_PLANAR4, abi_surface_planar4);
STATIC_ASSERT(OS32_SURFACE_PACKED8 == GFX_FMT_PACKED8, abi_surface_packed8);
#include "system_surface.h"
__attribute__((section(".text.surface_api")))
static int surface_api_source(u32 role, struct surface_query_source *source)
{
    switch (role) {
    case LEDGER_ROLE_CLIENT:
    case LEDGER_ROLE_DISPLAY:
        return gfx_surface_source(role, source);
    case LEDGER_ROLE_TVRAM:
    case LEDGER_ROLE_UNICODE:
        return system_surface_source(role, source);
    default:
        return OS32_ERR_INVAL;
    }
}
__attribute__((section(".text.surface_api")))
int surface_api_query(u32 role, struct surface_query_result *out)
{
    struct surface_query_source source;
    int rc = surface_api_source(role, &source);
    return rc ? rc : surface_query(&source, out);
}
__attribute__((section(".text.surface_api")))
int surface_api_lease(u32 role, const struct surface_ref *ref, u32 access,
                      struct lease_view *out)
{
    struct surface_query_source source;
    int rc = surface_api_source(role, &source);
    return rc ? rc : surface_lease(&source, ref, access, out);
}
__attribute__((section(".text.surface_api")))
int surface_api_bundle(u32 role, const struct surface_ref *refs, u32 count,
                       u32 access, struct surface_lease_result *out)
{
    struct surface_query_source source;
    int rc = surface_api_source(role, &source);
    return rc ? rc : surface_lease_bundle(&source, refs, count, access, out);
}
__attribute__((section(".text.surface_api")))
int surface_api_unlease(u32 token)
{
    struct caller_access caller;
    unsigned int flags = irq_save();
    int valid = !kctx_irq_depth && !kctx_exc_depth && caller_access_get(&caller) &&
                caller.origin == CALLER_USER;
    AppSlot *slot = valid ? appslot_get(caller.app_id) : 0;
    valid = valid && slot && slot->state == APP_STATE_RUNNING;
    if (valid) {
        for (u32 i = 0; i < MEM_LEASE_MAX; i++) {
            const struct as_lease *lease = &caller.as->leases[i];
            if (lease->token == token && (lease->flags & AS_LEASE_GFX_COMPAT)) {
                valid = 0;
                break;
            }
        }
    }
    irq_restore(flags);
    /* No callbacks/scheduling before release; get revalidates AS identity. */
    return valid ? surface_query_error(lease_release(caller.as, token)) : OS32_ERR_INVAL;
}
