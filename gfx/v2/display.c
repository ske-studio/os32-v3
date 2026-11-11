/* HAL v2 display operations (HAL_V2_DESIGN §§7-3, 9-2). */
#include "gfx2_int.h"
#include "gfx_policy.h"

#define DISPLAY_DL_US 1000000u

static struct gfx2_surf *by_sid(u32 sid)
{
    struct gfx2_surf *s = &gfx2_surfs[sid & (GFX2_MAX_SURFACES - 1)];
    return sid && s->sid == sid && s->state != GFX2_ST_FREE ? s : NULL;
}

static int context(u32 id, u32 head, int owner, struct gfx2_adapter **out,
                   struct gfx2_caller *c)
{
    struct gfx2_adapter *a = gfx2_adapter_get(id);
    int rc = gfx2_caller_get(c);
    if (rc) return rc;
    if (!a || head >= a->heads) return OS32_ERR_INVAL;
    if (!(a->flags & GFX2_AF_ACTIVE)) return OS32_ERR_NOSYS;
    if (owner && c->app_id != gfx_screen_owner()) return OS32_ERR_BUSY;
    *out = a;
    return 0;
}

void gfx2_flip_poll(struct gfx2_adapter *a)
{
    for (u32 h = 0; h < a->heads; h++) {
        struct gfx2_head *head = &a->head[h];
        u32 retired = 0;
        int rc;
        if (!head->pend_sid) continue;
        rc = a->be->flip_done(a->ctx, h, &retired);
        if (rc == OS32_ERR_NOSYS || (!rc && retired)) {
            struct gfx2_surf *s = by_sid(head->pend_sid);
            gfx2_scan_ref_swap(a, h, s);
            if (s) gfx2_scan_ref_put(s);
            head->pend_sid = 0;
            gfx_policy_scan_changed();
        }
    }
}

void gfx2_head_detach(struct gfx2_adapter *a, u32 head)
{
    struct gfx2_surf *s = by_sid(a->head[head].pend_sid);
    if (s) gfx2_scan_ref_put(s);
    a->head[head].pend_sid = 0;
    gfx2_scan_ref_swap(a, head, NULL);
}

int gfx2_flip(u32 id, u32 head, u32 sid, u32 flags, u32 deadline_ms)
{
    struct gfx2_adapter *a; struct gfx2_caller c; struct gfx2_surf *s;
    int rc = context(id, head, 1, &a, &c);
    u32 dl = deadline_ms > U32_MAX / 1000u ? U32_MAX : deadline_ms * 1000u;
    if (rc) return rc;
    gfx2_flip_poll(a);
    if (a->head[head].pend_sid) return OS32_ERR_BUSY;
    if (flags & ~GFX2_FLIP_VBLANK) return OS32_ERR_INVAL;
    s = gfx2_surf_get(sid, c.owner, &rc);
    if (!s) return rc;
    if (s->adapter != id || !(s->usage & GFX2_USE_SCANOUT)) return OS32_ERR_INVAL;
    rc = gfx2_surf_quiesce(s, dl);
    if (rc) return rc;
    rc = a->be->flip(a->ctx, head, s, flags, dl);
    if (rc) return rc;
    gfx2_scan_ref_get(s);
    a->head[head].pend_sid = sid;
    gfx2_flip_poll(a);
    return 0;
}

int gfx2_vblank_wait(u32 id, u32 head, u32 after, u32 deadline_ms, u32 *seen)
{
    struct gfx2_adapter *a; struct gfx2_caller c;
    int rc = context(id, head, 0, &a, &c);
    u32 dl = deadline_ms > U32_MAX / 1000u ? U32_MAX : deadline_ms * 1000u;
    if (rc) return rc;
    if (!seen) return OS32_ERR_INVAL;
    gfx2_flip_poll(a);
    rc = a->be->vblank_wait(a->ctx, head, after, dl, seen);
    gfx2_flip_poll(a);
    gfx2_closing_sweep(a);
    return rc;
}

int gfx2_palette(u32 id, u32 first, u32 count, const u32 *rgb)
{
    struct gfx2_adapter *a; struct gfx2_caller c;
    int rc = context(id, 0, 1, &a, &c);
    if (rc) return rc;
    if (!rgb || first >= 256 || !count || count > 256 - first) return OS32_ERR_INVAL;
    rc = a->be->palette(a->ctx, first, count, rgb);
    if (!rc) gfx2_pal_set(first, count, rgb);
    return rc;
}

int gfx2_gamma(u32 id, u32 head, const u16 *rgb)
{
    struct gfx2_adapter *a; struct gfx2_caller c;
    int rc = context(id, head, 1, &a, &c);
    if (rc) return rc;
    return rgb ? a->be->gamma(a->ctx, head, rgb) : OS32_ERR_INVAL;
}

int gfx2_cursor_image(u32 id, u32 head, u32 sid)
{
    struct gfx2_adapter *a; struct gfx2_caller c; struct gfx2_surf *s;
    int rc = context(id, head, 1, &a, &c);
    if (rc) return rc;
    s = gfx2_surf_get(sid, c.owner, &rc);
    if (!s) return rc;
    if (s->adapter != id || s->fmt != GFX2_FMT_ARGB8888 ||
        s->w > a->caps.cursor_w || s->h > a->caps.cursor_h) return OS32_ERR_INVAL;
    rc = gfx2_cpu_begin(s, GFX2_CPU_R, DISPLAY_DL_US);
    if (rc) return rc;
    rc = a->be->cursor_image(a->ctx, head, s);
    gfx2_cpu_end(s, GFX2_CPU_R, NULL);
    return rc;
}

int gfx2_cursor_move(u32 id, u32 head, i32 x, i32 y)
{
    struct gfx2_adapter *a; struct gfx2_caller c;
    int rc = context(id, head, 1, &a, &c);
    return rc ? rc : a->be->cursor_move(a->ctx, head, x, y);
}
int gfx2_cursor_show(u32 id, u32 head, u32 show)
{
    struct gfx2_adapter *a; struct gfx2_caller c;
    int rc = context(id, head, 1, &a, &c);
    return rc ? rc : a->be->cursor_show(a->ctx, head, show);
}
int gfx2_dpms(u32 id, u32 head, u32 state)
{
    struct gfx2_adapter *a; struct gfx2_caller c;
    int rc = context(id, head, 1, &a, &c);
    return rc ? rc : a->be->dpms(a->ctx, head, state);
}

int gfx2_present(u32 id, u32 head, i16 x, i16 y, u16 w, u16 h)
{
    struct gfx2_adapter *a; struct gfx2_caller c; struct gfx2_surf *s = NULL;
    GfxSyncCtx *ctx; u32 mode, cpp; i32 l, t, r, b;
    int rc = context(id, head, 0, &a, &c);
    if (rc) return rc;
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++) {
        struct gfx2_surf *p = &gfx2_surfs[i];
        if (p->state != GFX2_ST_FREE && (p->flags & GFX2_SF_ALIAS) &&
            p->adapter == id && p->head == head && p->role == GFX2_ROLE_DISPLAY) { s = p; break; }
    }
    if (!s) return OS32_ERR_INVAL;
    rc = gfx2_sync_get(a, head, &ctx, &mode);
    if (rc == OS32_ERR_NOTFOUND || (!rc && mode == GFXSYNC_DIRECT)) return 0;
    if (rc) return rc;
    l = x > s->dirty.x ? x : s->dirty.x; t = y > s->dirty.y ? y : s->dirty.y;
    r = (i32)x + w; b = (i32)y + h;
    if (r > s->dirty.x + s->dirty.w) r = s->dirty.x + s->dirty.w;
    if (b > s->dirty.y + s->dirty.h) b = s->dirty.y + s->dirty.h;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > s->w) r = s->w;
    if (b > s->h) b = s->h;
    if (r <= l || b <= t) return 0;
    cpp = s->fmt == GFX2_FMT_XRGB8888 || s->fmt == GFX2_FMT_ARGB8888 ? 4 :
          s->fmt == GFX2_FMT_INDEX8 || s->fmt == GFX2_FMT_A8 || s->fmt == GFX2_FMT_L8 ? 1 : 2;
    rc = gfxsync_write_rows(ctx, l, t, r - l, b - t, s->kva + t * s->pitch + l * cpp, s->pitch);
    if (rc) return OS32_ERR_IO;
    s->dirty = (struct gfx2_rect){0};
    return 0;
}

int gfx2_head_restore_v1(struct gfx2_adapter *a, u32 head)
{
    const GfxMode *m = gfx_backend_mode(a->v1);
    struct gfx2_surf *s = NULL;
    GFX2_ModeInfo mode = {0};
    if (head >= a->heads) return OS32_ERR_IO;
    if (!m) { gfx2_head_detach(a, head); return OS32_ERR_IO; }
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++) {
        struct gfx2_surf *p = &gfx2_surfs[i];
        if (p->state != GFX2_ST_FREE && (p->flags & GFX2_SF_ALIAS) && p->adapter == a->index &&
            p->head == head && p->role == GFX2_ROLE_DISPLAY) { s = p; break; }
    }
    if (!s) { gfx2_head_detach(a, head); return OS32_ERR_IO; }
    mode.id = m->hw_mode; mode.w = m->width; mode.h = m->height; mode.fmt = s->fmt;
    if (a->be->mode_set(a->ctx, head, a->head[head].conn_mask, &mode, s)) {
        gfx2_head_detach(a, head);
        return OS32_ERR_IO;
    }
    gfx2_head_detach(a, head);
    gfx2_scan_ref_swap(a, head, s);
    gfx_policy_scan_changed();
    return 0;
}
