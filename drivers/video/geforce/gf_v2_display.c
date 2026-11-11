/* HAL v2 GeForce display hooks; HAL_V2_DESIGN G2. */
#include "gf_v2.h"
#include "hwdev/gf/display/api.h"
#include "kprintf.h"


static const struct gf4_test_ops *display(void)
{
    const struct gf4_test_ops *ops = gf_v2_disp();
    return ops && ops->ctx && ops->ctx->state ? ops : 0;
}

int gf_v2_owns(const struct gf3_surface *s)
{
    u32 i;
    if (!s) return 0;
    for (i = 0; i < GFX2_MAX_SURFACES; i++) {
        const struct gf_vram_reservation *v = &gf_v2_inst.node[i].value;
        if (v->owner && v->reservation_id == s->reservation_id &&
            v->generation == s->generation) return 1;
    }
    return 0;
}

static int surface(const struct gfx2_surf *s, struct gf3_surface *out)
{
    enum gf3_fmt fmt;
    switch (s->fmt) {
    case GFX2_FMT_INDEX8: fmt = GF3_INDEX8; break;
    case GFX2_FMT_RGB565: fmt = GF3_RGB565; break;
    case GFX2_FMT_XRGB8888: fmt = GF3_XRGB8888; break;
    default: return OS32_ERR_INVAL;
    }
    *out = (struct gf3_surface){ .off = s->gpu_off, .bytes = s->bytes,
        .pitch = s->pitch, .width = s->w, .height = s->h, .fmt = fmt,
        .reservation_id = s->priv[0], .generation = s->priv[1], .dma = 0 };
    return 0;
}

int gf_v2_mode_set(void *ctx, u32 head, u32 mask, const GFX2_ModeInfo *m,
                   const struct gfx2_surf *s)
{
    struct gf_v2 *g = ctx;
    const struct gf4_test_ops *ops = display();
    struct gf4_ctx *c; struct gf3_surface front; struct gf4_route route;
    struct gf_v2_home home = {0}; u32 dl = 0, conn; int rc;
    if (!ops) return OS32_ERR_IO;
    if (head >= GF4_HEADS || !s || !m) return OS32_ERR_INVAL;
    c = ops->ctx;
    rc = gf_v2_deadline(c->ch, GF3_TIMEOUT_US, &dl);
    if (rc) return gf_v2_rc(rc);
    if (g->lost) { rc = gf_v2_start(g, dl); if (rc) return gf_v2_rc(rc); }
    if (s->flags & GFX2_SF_ALIAS) {
        if (head) return OS32_ERR_INVAL;
        if (!g->home.valid)
            return c->state->head[0].front.off == s->gpu_off ? 0 : OS32_ERR_INVAL;
        conn = g->home.conn;
        if (conn >= c->nroutes) return OS32_ERR_INVAL;
        route = c->routes[conn]; route.head = head;
        rc = ops->set_mode(c, &route, g->home.mode_id, &g->home.front, dl);
        if (rc) return gf_v2_rc(rc);
        if (g->home.front.fmt == GF3_INDEX8 &&
            gf4_palette_set(c, 0, 0, 256, g->home.pal))
            kprintf(KLOG_ATTR, "[gf_v2] restore palette failed\n");
        g->home.valid = 0; g->pal_set = 0;
        g->head[head] = (struct gf_v2_head){0};
        return 0;
    }
    if (!mask || (mask & (mask - 1))) return OS32_ERR_INVAL;
    for (conn = 0; conn < 32 && !(mask & (1u << conn)); conn++) {}
    if (conn >= c->nroutes || !(c->routes[conn].head_mask & (1u << head))) return OS32_ERR_INVAL;
    rc = surface(s, &front); if (rc) return rc;
    route = c->routes[conn]; route.head = head;
    if (!head && c->state->head[0].enabled && !gf_v2_owns(&c->state->head[0].front)) {
        const struct gf4_head_image *h = &c->state->head[0];
        home.valid = 1; home.conn = gf_v2_route_index(c, &h->route);
        home.mode_id = h->mode.id; home.front = h->front;
        for (u32 i = 0; i < 768; i++) home.pal[i] = h->palette[i];
    }
    rc = ops->set_mode(c, &route, m->id, &front, dl);
    if (rc) return gf_v2_rc(rc);
    if (home.valid) g->home = home;
    g->head[head] = (struct gf_v2_head){s->sid, 0, 0};
    if (front.fmt == GF3_INDEX8 && g->pal_set && gf4_palette_set(c, head, 0, 256, g->pal))
        kprintf(KLOG_ATTR, "[gf_v2] palette failed\n");
    return 0;
}

int gf_v2_flip(void *ctx, u32 head, const struct gfx2_surf *s, u32 flags, u32 us)
{
    struct gf_v2 *g = ctx; const struct gf4_test_ops *ops = display();
    struct gf3_surface front; u32 dl = 0; int rc;
    if (!ops) return OS32_ERR_IO;
    if (head >= GF4_HEADS || !s || (s->flags & GFX2_SF_ALIAS)) return OS32_ERR_INVAL;
    if (g->head[head].pend_sid) return OS32_ERR_BUSY;
    rc = surface(s, &front); if (rc) return rc;
    rc = gf_v2_deadline(ops->ctx->ch, us, &dl);
    if (!rc) rc = ops->flip(ops->ctx, head, &front, !!(flags & GFX2_FLIP_VBLANK), dl);
    if (rc) return gf_v2_rc(rc);
    if (ops->ctx->hold[head].pending) g->head[head].pend_sid = s->sid;
    else { g->head[head].retired_sid = g->head[head].scan_sid; g->head[head].scan_sid = s->sid; }
    return 0;
}

int gf_v2_flip_done(void *ctx, u32 head, u32 *retired)
{
    struct gf_v2 *g = ctx; const struct gf4_test_ops *ops = display();
    struct gf4_ctx *c; struct gf_v2_head *h; u32 dl = 0; int rc;
    if (!ops) return OS32_ERR_IO;
    if (head >= GF4_HEADS || !retired) return OS32_ERR_INVAL;
    c = ops->ctx; h = &g->head[head];
    if (h->pend_sid) {
        if (c->hold[head].pending) {
            struct gf_irq_stats stats;
            gf_irq_snapshot(c->ch->f, &stats);
            if (c->hold[head].base_verified && stats.vblank[head] == c->hold[head].after) return OS32_ERR_BUSY;
            rc = gf_v2_deadline(c->ch, GF3_TIMEOUT_US, &dl);
            if (!rc) rc = gf4_flip_complete(c, head, dl);
            if (rc) { h->pend_sid = 0; return gf_v2_rc(rc); }
        }
        h->retired_sid = h->scan_sid; h->scan_sid = h->pend_sid; h->pend_sid = 0;
    }
    *retired = h->retired_sid; h->retired_sid = 0; return 0;
}

int gf_v2_vblank_wait(void *ctx, u32 head, u32 after, u32 us, u32 *seen)
{
    const struct gf4_test_ops *ops = display(); u32 dl = 0; int rc; (void)ctx;
    if (!ops) return OS32_ERR_IO;
    if (head >= GF4_HEADS || !seen) return OS32_ERR_INVAL;
    rc = gf_v2_deadline(ops->ctx->ch, us, &dl);
    if (!rc) rc = ops->vblank_wait(ops->ctx, head, after, dl, seen);
    return gf_v2_rc(rc);
}

int gf_v2_palette(void *ctx, u32 first, u32 count, const u32 *xrgb)
{
    struct gf_v2 *g = ctx; const struct gf4_test_ops *ops = display(); int result = 0;
    if (!ops) return OS32_ERR_IO;
    if (first > 256 || count > 256 - first || (!xrgb && count)) return OS32_ERR_INVAL;
    for (u32 i = 0; i < count; i++) {
        u32 v = xrgb[i], p = (first + i) * 3;
        g->pal[p] = v >> 16; g->pal[p + 1] = v >> 8; g->pal[p + 2] = v;
    }
    g->pal_set = 1;
    for (u32 h = 0; h < GF4_HEADS; h++) {
        const struct gf4_head_image *im = &ops->ctx->state->head[h];
        if (im->enabled && im->front.fmt == GF3_INDEX8) {
            int rc = gf4_palette_set(ops->ctx, h, first, count, g->pal + first * 3);
            if (rc && !result) result = gf_v2_rc(rc);
        }
    }
    return result;
}

int gf_v2_gamma(void *ctx, u32 head, const u16 *rgb)
{
    const struct gf4_test_ops *ops = display(); (void)ctx;
    if (!ops) return OS32_ERR_IO;
    if (head >= GF4_HEADS || !rgb) return OS32_ERR_INVAL;
    return gf_v2_rc(ops->gamma(ops->ctx, head, (const u16 (*)[256])rgb));
}
int gf_v2_cursor_image(void *ctx, u32 head, const struct gfx2_surf *s)
{
    const struct gf4_test_ops *ops = display(); u32 dl = 0; int rc; (void)ctx;
    if (!ops) return OS32_ERR_IO;
    if (head >= GF4_HEADS || !s || s->fmt != GFX2_FMT_ARGB8888 || !s->kva) return OS32_ERR_INVAL;
    struct gf4_cursor cur = { (const u32 *)s->kva, s->bytes, s->pitch, s->w, s->h, 0, 0 };
    rc = gf_v2_deadline(ops->ctx->ch, GF3_TIMEOUT_US, &dl);
    if (!rc) rc = ops->cursor_image(ops->ctx, head, &cur, dl);
    return gf_v2_rc(rc);
}
int gf_v2_cursor_move(void *ctx, u32 head, i32 x, i32 y)
{
    const struct gf4_test_ops *ops = display(); (void)ctx;
    if (!ops) return OS32_ERR_IO;
    if (head >= GF4_HEADS) return OS32_ERR_INVAL;
    return gf_v2_rc(ops->cursor_move(ops->ctx, head, x, y));
}
int gf_v2_cursor_show(void *ctx, u32 head, u32 show)
{
    const struct gf4_test_ops *ops = display(); (void)ctx;
    if (!ops) return OS32_ERR_IO;
    if (head >= GF4_HEADS || show > 1) return OS32_ERR_INVAL;
    return gf_v2_rc(ops->cursor_show(ops->ctx, head, !!show));
}
int gf_v2_dpms(void *ctx, u32 head, u32 state)
{
    const struct gf4_test_ops *ops = display(); u32 dl = 0; int rc; (void)ctx;
    if (!ops) return OS32_ERR_IO;
    if (head >= GF4_HEADS || state > 3) return OS32_ERR_INVAL;
    rc = gf_v2_deadline(ops->ctx->ch, GF3_TIMEOUT_US, &dl);
    if (!rc) rc = ops->dpms_set(ops->ctx, head, (enum gf4_dpms)state, dl);
    return gf_v2_rc(rc);
}
