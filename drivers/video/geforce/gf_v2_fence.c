/* HAL v2 to GeForce reference mapping; HAL_V2_DESIGN section 6-3. */
#include "gf_v2.h"

static int gf_v2_fail(struct gf_v2 *g, int rc)
{
    struct gf_v2_ring *r = &g->ring;
    if (r->count)
        g->failed_upto = r->e[(r->head + GF_V2_RING - 1) % GF_V2_RING].v2;
    return rc;
}

int gf_v2_fence_emit(struct gf_v2 *g, u32 v2, u32 dl)
{
    struct gf_v2_ring *r = &g->ring;
    struct gf2_fence f;
    int rc;
    if (r->count == GF_V2_RING) {
        rc = gf2_kick(g->ch);
        if (!rc) rc = gf2_fence_wait(g->ch, r->e[r->tail].f, dl);
        if (rc) return gf_v2_fail(g, rc);
        g->completed = r->e[r->tail].v2;
        r->tail = (r->tail + 1) % GF_V2_RING;
        r->count--;
    }
    rc = gf2_fence_emit(g->ch, &f, dl);
    if (rc) return gf_v2_fail(g, rc);
    r->e[r->head] = (struct gf_v2_ring_e){v2, f};
    r->head = (r->head + 1) % GF_V2_RING;
    r->count++;
    return 0;
}

int gf_v2_fence_state(struct gf_v2 *g, u32 *completed, u32 *failed_upto)
{
    struct gf_v2_ring *r = &g->ring;
    while (r->count) {
        int done, rc = gf2_fence_poll(g->ch, r->e[r->tail].f, &done);
        if (rc) return gf_v2_fail(g, rc);
        if (!done) break;
        g->completed = r->e[r->tail].v2;
        r->tail = (r->tail + 1) % GF_V2_RING;
        r->count--;
    }
    *completed = g->completed;
    *failed_upto = g->failed_upto;
    return 0;
}

int gf_v2_fence_wait(struct gf_v2 *g, u32 v2, u32 dl)
{
    struct gf_v2_ring *r = &g->ring;
    if (!v2 || (g->completed && (i32)(g->completed - v2) >= 0)) return 0;
    for (u32 i = 0; i < r->count; i++) {
        u32 at = (r->tail + i) % GF_V2_RING;
        if (r->e[at].v2 == v2) {
            int rc = gf2_kick(g->ch);
            if (!rc) rc = gf2_fence_wait(g->ch, r->e[at].f, dl);
            if (rc) return gf_v2_fail(g, rc);
            g->completed = v2;
            r->tail = (at + 1) % GF_V2_RING;
            r->count -= i + 1;
            return 0;
        }
    }
    return 0;
}
