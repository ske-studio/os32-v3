#include "drivers/video/geforce/gf_v2.h"

static struct gf_v2 g;
static struct gfx2_surf s, other;
static int reserve_rc, release_rc, reserves, releases;
int gf_vram_reserve(struct gf2_chan *ch, const void *owner, u32 off, u32 bytes,
                    struct gf_vram_reservation_node *n)
{
    (void)ch; ++reserves;
    if (reserve_rc) return reserve_rc;
    n->value.owner = owner; n->value.off = off; n->value.bytes = bytes;
    n->value.reservation_id = 42; ++n->value.generation;
    return 0;
}
int gf_vram_release(struct gf2_chan *ch, const struct gf_vram_reservation *key)
{
    (void)ch; ++releases;
    if (release_rc) return release_rc;
    ((struct gf_vram_reservation *)key)->owner = 0;
    return 0;
}
#define CHECK(x, n) do { if (!(x)) return (n); } while (0)
static int test(void)
{
    s.pool = GFX2_POOL_VRAM; s.gpu_off = 4096; s.bytes = 4096;
    s.priv[0] = 7; s.priv[1] = 8; s.priv[2] = 9; s.priv[3] = 10;
    reserve_rc = -37;
    CHECK(gf_v2_alloc(&g, &s) == -37, 1);
    CHECK(s.priv[0] == 7 && s.priv[1] == 8 && s.priv[2] == 9 && s.priv[3] == 10, 2);
    reserve_rc = 0;
    CHECK(gf_v2_alloc(&g, &s) == 0, 3);
    CHECK(s.priv[0] == 42 && s.priv[1] == 1 && s.priv[2] == 0 && s.priv[3] == 10, 4);
    CHECK(g.node[0].value.owner == &s && g.node[0].value.off == 4096 && g.node[0].value.bytes == 4096, 5);
    other = s;
    CHECK(gf_v2_release(&g, &other) == OS32_ERR_INVAL && releases == 0, 6);
    release_rc = GF3_EBUSY;
    CHECK(gf_v2_release(&g, &s) == GF3_EBUSY && s.priv[0] == 42, 7);
    release_rc = GF3_ESTATE;
    CHECK(gf_v2_release(&g, &s) == OS32_ERR_INVAL, 8);
    release_rc = 0;
    CHECK(gf_v2_release(&g, &s) == 0 && s.priv[0] == 0, 9);
    CHECK(gf_v2_release(&g, &s) == OS32_ERR_INVAL, 10);
    CHECK(gf_v2_alloc(&g, &s) == 0 && s.priv[1] == 2, 11);
    s.pool = GFX2_POOL_GART;
    CHECK(gf_v2_alloc(&g, &s) == 0 && gf_v2_release(&g, &s) == 0 && reserves == 3, 12);
    return 0;
}
void _start(void) { int rc = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
