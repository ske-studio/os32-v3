/* HAL v2 GeForce VRAM reservation ownership. */
#include "gf_v2.h"

int gf_v2_alloc(struct gf_v2 *g, struct gfx2_surf *s)
{
    u32 i;
    int rc;

    if (!g || !s) return OS32_ERR_INVAL;
    if (s->pool == GFX2_POOL_GART) return 0;
    if (s->pool != GFX2_POOL_VRAM) return OS32_ERR_INVAL;
    for (i = 0; i < GFX2_MAX_SURFACES; ++i)
        if (!g->node[i].value.owner) break;
    if (i == GFX2_MAX_SURFACES) return OS32_ERR_NOSPC;
    rc = gf_vram_reserve(g->ch, s, s->gpu_off, s->bytes, &g->node[i]);
    if (rc) return rc;
    s->priv[0] = g->node[i].value.reservation_id;
    s->priv[1] = g->node[i].value.generation;
    s->priv[2] = i;
    return 0;
}

int gf_v2_release(struct gf_v2 *g, struct gfx2_surf *s)
{
    struct gf_vram_reservation_node *n;
    int rc;

    if (!g || !s) return OS32_ERR_INVAL;
    if (s->pool == GFX2_POOL_GART) return 0;
    if (s->pool != GFX2_POOL_VRAM || s->priv[2] >= GFX2_MAX_SURFACES)
        return OS32_ERR_INVAL;
    n = &g->node[s->priv[2]];
    if (n->value.owner != s || n->value.reservation_id != s->priv[0] ||
        n->value.generation != s->priv[1] || !n->value.owner)
        return OS32_ERR_INVAL;
    rc = gf_vram_release(g->ch, &n->value);
    if (rc == GF3_ESTATE) return OS32_ERR_INVAL;
    if (rc) return rc;
    s->priv[0] = s->priv[1] = s->priv[2] = 0;
    return 0;
}
