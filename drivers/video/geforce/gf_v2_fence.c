#include "gf_v2.h"

void gf_v2_fence_reset(struct gf_v2 *g)
{
    struct gf_v2_ring *r = &g->ring;
    if (r->count) g->failed_upto = r->e[(r->head + GF_V2_RING - 1) % GF_V2_RING].v2;
    *r = (struct gf_v2_ring){0};
}
