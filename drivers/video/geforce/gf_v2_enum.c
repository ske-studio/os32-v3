/* HAL v2 GeForce display enumeration. */
#include "gf_v2.h"
#include "hwdev/gf/display/api.h"

u32 gf_v2_route_index(const struct gf4_ctx *c, const struct gf4_route *r)
{
    u32 i;
    for (i = 0; i < c->nroutes; i++)
        if (c->routes[i].connector == r->connector && c->routes[i].type == r->type &&
            c->routes[i].or_mask == r->or_mask) return i;
    return c->nroutes;
}

static const struct gf4_test_ops *display(void)
{
    const struct gf4_test_ops *ops = gf_v2_disp();
    return ops && ops->ctx && ops->ctx->state ? ops : 0;
}

int gf_v2_enum_head(void *ctx, u32 head, GFX2_HeadInfo *out)
{
    const struct gf4_test_ops *ops = display();
    GFX2_HeadInfo v = {0};
    (void)ctx;
    if (head >= GF4_HEADS || !out) return OS32_ERR_INVAL;
    if (!ops) return OS32_ERR_IO;
    const struct gf4_head_image *h = &ops->ctx->state->head[head];
    v.head = head;
    v.fmt_scanout = (1u << GFX2_FMT_INDEX8) | (1u << GFX2_FMT_RGB565) | (1u << GFX2_FMT_XRGB8888);
    v.max_w = v.max_h = 4096;
    if (h->enabled) {
        u32 route = gf_v2_route_index(ops->ctx, &h->route);
        v.attached = 1;
        if (route < 32) v.connector_mask = 1u << route;
        v.w = h->mode.t.hd; v.h = h->mode.t.vd; v.mode_id = h->mode.id;
    }
    *out = v;
    return 0;
}

static int paired(const struct gf4_ctx *c, const struct gf4_route *r)
{
    u32 i;
    if (r->type != GF4_ANALOG && r->type != GF4_TMDS) return 0;
    for (i = 0; i < c->nroutes; i++)
        if (c->routes[i].connector == r->connector &&
            ((r->type == GF4_ANALOG && c->routes[i].type == GF4_TMDS) ||
             (r->type == GF4_TMDS && c->routes[i].type == GF4_ANALOG))) return 1;
    return 0;
}

int gf_v2_enum_conn(void *ctx, u32 conn, GFX2_ConnectorInfo *out)
{
    const struct gf4_test_ops *ops = display();
    GFX2_ConnectorInfo v = {0};
    u8 b[128]; u32 dl = 0, i; int rc, pair;
    (void)ctx;
    if (!out) return OS32_ERR_INVAL;
    if (!ops) return OS32_ERR_IO;
    struct gf4_ctx *c = ops->ctx;
    if (conn >= c->nroutes) return OS32_ERR_INVAL;
    const struct gf4_route *r = &c->routes[conn];
    pair = paired(c, r);
    v.id = conn; v.possible_heads = r->head_mask & 3;
    if (r->type == GF4_TV) {
        v.type = c->tv_subconnector == 2 ? GFX2_CONN_TV_SVIDEO : GFX2_CONN_TV_COMPOSITE;
        v.status = !c->tv_sense_valid ? GFX2_CONN_UNKNOWN : c->tv_pin_mask ? GFX2_CONN_ON : GFX2_CONN_OFF;
    } else {
        v.type = pair ? GFX2_CONN_DVI_I : r->type == GF4_ANALOG ? GFX2_CONN_VGA : GFX2_CONN_DVI_D;
        v.status = r->type == GF4_ANALOG ? GFX2_CONN_UNKNOWN : GFX2_CONN_OFF;
        rc = gf_v2_deadline(c->ch, GF3_TIMEOUT_US, &dl);
        if (!rc) rc = gf4_ddc_probe(c, r->i2c_index, dl);
        if (!rc) rc = ops->ddc_read(c, r->i2c_index, 0, b, dl);
        if (!rc && b[0] == 0 && b[1] == 255 && b[2] == 255 && b[3] == 255 &&
            b[4] == 255 && b[5] == 255 && b[6] == 255 && b[7] == 0) {
            v.status = !pair || (!!(b[20] & 128) == (r->type == GF4_TMDS)) ? GFX2_CONN_ON : GFX2_CONN_OFF;
            if (v.status == GFX2_CONN_ON) v.edid_blocks = 1u + b[126];
        }
    }
    for (i = 0; i < c->state->nmodes; i++)
        if (c->state->mode_connector[i] == r->connector) { v.preferred_mode_id = c->state->modes[i].id; break; }
    *out = v;
    return 0;
}

int gf_v2_enum_mode(void *ctx, u32 head, u32 fmt, u32 index, GFX2_ModeInfo *out)
{
    const struct gf4_test_ops *ops = display(); u32 i, j;
    (void)ctx;
    if (!out || head >= GF4_HEADS || fmt < 1 || fmt > 10) return OS32_ERR_INVAL;
    if (!ops) return OS32_ERR_IO;
    struct gf4_ctx *c = ops->ctx;
    if (fmt > 3) return OS32_ERR_INVAL;
    for (i = 0; i < c->state->nmodes; i++) {
        const struct gf4_mode *m = &c->state->modes[i];
        if ((u32)m->fmt + 1 != fmt) continue;
        for (j = 0; j < c->nroutes; j++)
            if (c->routes[j].connector == c->state->mode_connector[i] && (c->routes[j].head_mask & (1u << head))) break;
        if (j == c->nroutes) continue;
        if (index--) continue;
        GFX2_ModeInfo v = {0};
        u32 total = (u32)m->t.ht * m->t.vt;
        if (!total) return OS32_ERR_INVAL;
        u32 n = m->t.khz * 1000u, q = n / total, rem = n % total;
        v.id = m->id; v.w = m->t.hd; v.h = m->t.vd; v.fmt = fmt;
        v.pixclk_khz = m->t.khz; v.htotal = m->t.ht; v.vtotal = m->t.vt;
        v.hsync_khz10 = m->t.khz * 10u / m->t.ht;
        v.refresh_mhz = q * 1000u + (rem * 1000u + total / 2u) / total;
        v.flags = m->t.flags & 15;
        *out = v; return 0;
    }
    return OS32_ERR_INVAL;
}

int gf_v2_edid(void *ctx, u32 conn, u32 block, u8 *out)
{
    const struct gf4_test_ops *ops = display(); u8 b[128]; u32 dl = 0; int rc; u32 i;
    (void)ctx;
    if (!out || block > 255) return OS32_ERR_INVAL;
    if (!ops) return OS32_ERR_IO;
    struct gf4_ctx *c = ops->ctx;
    if (conn >= c->nroutes) return OS32_ERR_INVAL;
    if (c->routes[conn].type == GF4_TV) return OS32_ERR_NOSYS;
    rc = gf_v2_deadline(c->ch, GF3_TIMEOUT_US, &dl);
    if (!rc) rc = ops->ddc_read(c, c->routes[conn].i2c_index, block, b, dl);
    if (rc) return gf_v2_rc(rc);
    for (i = 0; i < 128; i++) out[i] = b[i];
    return 0;
}
