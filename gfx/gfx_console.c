#include "gfx_console.h"
#include "v2/gfx2_int.h"

#define CONSOLE_DL_US 1000000u

static struct gfx2_surf *target_surface(struct gfx_console_target *t)
{
    struct gfx2_adapter *a;
    struct gfx2_surf *s;
    int rc;
    if (!t || !t->sid) return NULL;
    a = gfx2_adapter_get(t->adapter);
    if (!a || a->gen != t->generation || a->state != GFX2_AD_OK ||
        !(a->flags & GFX2_AF_ACTIVE)) return NULL;
    s = gfx2_surf_get(t->sid, t->owner, &rc);
    if (!s || s->adapter != t->adapter || s->state == GFX2_ST_LOST ||
        a->head[t->head].scan_sid != t->sid) return NULL;
    return s;
}

int gfx_console_open(u32 adapter, u32 head, struct gfx_console_target *t)
{
    struct gfx2_adapter *a = gfx2_adapter_get(adapter);
    struct gfx2_surf *s;
    GFX2_ModeInfo mode;
    u32 owner;
    struct gfx2_surf *previous;
    int rc;
    if (!t || t->sid || !a || head >= a->heads) return OS32_ERR_INVAL;
    if (a->state != GFX2_AD_OK || !(a->flags & GFX2_AF_ACTIVE)) return OS32_ERR_STALE;
    if (a->head[head].pend_sid) return OS32_ERR_BUSY;
    previous = a->head[head].scan_sid ?
        &gfx2_surfs[a->head[head].scan_sid & (GFX2_MAX_SURFACES - 1)] : NULL;
    /* The boot v1 bridge may already own scanout. Normal application surfaces
     * are exclusive; only the borrowed v1 alias may be replaced here. */
    if (previous && !(previous->flags & GFX2_SF_ALIAS)) return OS32_ERR_BUSY;
    rc = a->be->enum_mode(a->ctx, head, GFX2_FMT_INDEX8, 0, &mode);
    if (rc) return rc;
    if (!ledger_owner_new(LEDGER_KIND_PERSIST, 0, "fbconsole", &owner)) return OS32_ERR_NOSPC;
    rc = gfx2_surf_new(a, mode.w, mode.h, GFX2_FMT_INDEX8, GFX2_POOL_SYSMEM,
                      GFX2_USE_RENDER2D | GFX2_USE_SCANOUT, owner, &s);
    if (rc) { ledger_owner_retire(owner); return rc; }
    rc = a->be->mode_set(a->ctx, head, 1u, &mode, s);
    if (rc) { gfx2_surf_destroy(s); ledger_owner_retire(owner); return rc; }
    gfx2_scan_ref_swap(a, head, s);
    a->head[head].mode_id = mode.id;
    a->head[head].conn_mask = 1;
    *t = (struct gfx_console_target){adapter, a->gen, head, s->sid, owner,
                                    s->w, s->h, s->pitch, 0};
    return 0;
}

int gfx_console_lock(struct gfx_console_target *t, u8 **pixels)
{
    struct gfx2_surf *s = target_surface(t);
    int rc;
    if (!pixels || !s) return OS32_ERR_STALE;
    if (t->locked) return OS32_ERR_BUSY;
    rc = gfx2_cpu_begin(s, GFX2_CPU_R | GFX2_CPU_W, CONSOLE_DL_US);
    if (rc) return rc;
    rc = gfx2_surf_state_set(s, GFX2_ST_LOCKED_RW);
    if (rc) { gfx2_cpu_end(s, GFX2_CPU_R | GFX2_CPU_W, NULL); return rc; }
    t->locked = 1;
    *pixels = s->kva;
    return 0;
}

int gfx_console_unlock(struct gfx_console_target *t, i32 x, i32 y, i32 w, i32 h)
{
    struct gfx2_surf *s = target_surface(t);
    struct gfx2_rect rect;
    if (!s) return OS32_ERR_STALE;
    if (!t->locked) return OS32_ERR_INVAL;
    if (x < 0 || y < 0 || w < 0 || h < 0 || x > s->w || y > s->h ||
        w > s->w - x || h > s->h - y) return OS32_ERR_INVAL;
    rect = (struct gfx2_rect){x, y, w, h};
    gfx2_cpu_end(s, GFX2_CPU_R | GFX2_CPU_W, &rect);
    gfx2_dirty_add(s, &rect);
    t->locked = 0;
    return gfx2_surf_state_set(s, GFX2_ST_IDLE);
}

int gfx_console_present(struct gfx_console_target *t)
{
    struct gfx2_surf *s = target_surface(t);
    struct gfx2_adapter *a;
    int rc;
    if (!s) return OS32_ERR_STALE;
    if (t->locked) return OS32_ERR_BUSY;
    a = gfx2_adapter_get(t->adapter);
    rc = gfx2_surf_quiesce(s, CONSOLE_DL_US);
    if (rc) return rc;
    return a->be->flip(a->ctx, t->head, s, 0, CONSOLE_DL_US);
}

int gfx_console_close(struct gfx_console_target *t)
{
    struct gfx2_adapter *a;
    struct gfx2_surf *s;
    int rc;
    if (!t || !t->sid) return OS32_ERR_INVAL;
    a = gfx2_adapter_get(t->adapter);
    s = gfx2_surf_get(t->sid, t->owner, &rc);
    if (s) {
        if (t->locked) {
            gfx2_cpu_end(s, GFX2_CPU_R | GFX2_CPU_W, NULL);
            if (s->state == GFX2_ST_LOCKED_RW) gfx2_surf_state_set(s, GFX2_ST_IDLE);
        }
        if (a && t->head < a->heads && a->head[t->head].scan_sid == t->sid)
            gfx2_head_detach(a, t->head);
        rc = gfx2_surf_destroy(s);
        if (rc) return rc;
    }
    if (!ledger_owner_retire(t->owner)) return OS32_ERR_BUSY;
    *t = (struct gfx_console_target){0};
    return 0;
}
