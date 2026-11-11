/* HAL v2 mode transition, HAL_V2_DESIGN §9-3. */
#include "gfx2_int.h"
#include "../gfx_policy.h"
#include "../gfx.h"

#define GFX2_MODESET_DL 1000000u

static int connectors_ok(struct gfx2_adapter *a, u32 head, u32 mask)
{
    GFX2_ConnectorInfo c[32];
    u32 count = a->caps.connectors;
    if (count > 32) count = 32;
    for (u32 i = 0; i < 32; i++) {
        if (!(mask & (1u << i))) continue;
        if (i >= count || a->be->enum_conn(a->ctx, i, &c[i]) ||
            !(c[i].possible_heads & (1u << head))) return OS32_ERR_INVAL;
        if ((mask & (mask - 1)) && (c[i].clone_mask & (mask & ~(1u << i))) != (mask & ~(1u << i)))
            return OS32_ERR_NOSYS;
    }
    return 0;
}

static int mode_find(struct gfx2_adapter *a, u32 head, u32 id, GFX2_ModeInfo *m)
{
    for (u32 fmt = 1; fmt <= 10; fmt++)
        for (u32 i = 0; !a->be->enum_mode(a->ctx, head, fmt, i, m); i++)
            if (m->id == id) return 0;
    return OS32_ERR_INVAL;
}

int gfx2_mode_set(u32 index, u32 head, u32 conn_mask, u32 mode_id, u32 sid)
{
    struct gfx2_adapter *a = gfx2_adapter_get(index);
    struct gfx2_caller caller;
    struct gfx2_surf *s;
    GFX2_ModeInfo mode;
    int rc;
    if (!a || gfx2_caller_get(&caller)) return OS32_ERR_INVAL;
    if (!(a->flags & GFX2_AF_ACTIVE)) return OS32_ERR_NOSYS;
    if (caller.app_id != gfx_screen_owner()) return OS32_ERR_BUSY;
    if (head >= a->heads || !conn_mask) return OS32_ERR_INVAL;
    rc = connectors_ok(a, head, conn_mask);
    if (rc) return rc;
    if (mode_find(a, head, mode_id, &mode)) return OS32_ERR_INVAL;
    s = gfx2_surf_get(sid, caller.owner, &rc);
    if (!s) return rc;
    if (s->adapter != index || !(s->usage & GFX2_USE_SCANOUT) ||
        s->fmt != mode.fmt || s->w != mode.w || s->h != mode.h ||
        (a->caps.scanout_align && s->gpu_off % a->caps.scanout_align) ||
        (a->caps.pitch_align && s->pitch % a->caps.pitch_align)) return OS32_ERR_INVAL;
    rc = gfx2_surf_quiesce(s, GFX2_MODESET_DL);
    if (rc) return rc;
    if (a->state == GFX2_AD_LOST_HELD && a->be->stop(a->ctx)) return OS32_ERR_STALE;
    rc = a->be->mode_set(a->ctx, head, conn_mask, &mode, s);
    if (rc) return rc;
    if (a->state != GFX2_AD_OK) gfx2_adapter_recovered(a);
    gfx2_scan_ref_swap(a, head, s);
    a->head[head].mode_id = mode_id;
    a->head[head].conn_mask = conn_mask;
    gfx_policy_scan_changed();
    if (gfx_fullscreen_owner()) a->restore_on_leave = 1;
    if (a->be == &gfx2_v1_shim) v1_bridge_rebind();
    return 0;
}
