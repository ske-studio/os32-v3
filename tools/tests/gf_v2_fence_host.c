#include "drivers/video/geforce/gf_v2.h"
static struct gf_v2 g;
static struct gf2_chan ch;
static int kick_count, wait_count, wait_rc, poll_rc, poll_done;
int gf2_kick(struct gf2_chan *c) { (void)c; kick_count++; return 0; }
int gf2_fence_emit(struct gf2_chan *c, struct gf2_fence *f, u32 dl)
{ (void)dl; *f = (struct gf2_fence){c->epoch, ++c->seq}; return 0; }
int gf2_fence_wait(struct gf2_chan *c, struct gf2_fence f, u32 dl)
{ (void)c; (void)dl; wait_count++; if (f.seq != 1) return GF2_EFAULT; return wait_rc; }
int gf2_fence_poll(struct gf2_chan *c, struct gf2_fence f, int *done)
{ (void)c; (void)f; *done = poll_done; return poll_rc; }
static void die(int n) { __asm__ volatile("int $0x80" : : "a"(1), "b"(n)); __builtin_unreachable(); }
#define CHECK(x) do { if (!(x)) die(__LINE__); } while (0)
void _start(void)
{
    u32 completed, failed;
    g.ch = &ch; ch.epoch = 1;
    for (u32 i = 1; i <= GF_V2_RING; i++) CHECK(!gf_v2_fence_emit(&g, i, 9));
    wait_rc = GF2_ETIME;
    CHECK(gf_v2_fence_emit(&g, 257, 9) == GF2_ETIME);
    CHECK(kick_count == 1 && wait_count == 1 && g.ring.count == GF_V2_RING);
    CHECK(g.ring.e[g.ring.tail].v2 == 1 && g.failed_upto == 256);
    wait_rc = 0;
    CHECK(!gf_v2_fence_emit(&g, 257, 9));
    CHECK(g.completed == 1 && g.ring.count == GF_V2_RING && g.ring.e[g.ring.tail].v2 == 2);
    CHECK(!gf_v2_fence_wait(&g, 999, 9));
    poll_done = 0; CHECK(!gf_v2_fence_state(&g, &completed, &failed));
    CHECK(completed == 1 && failed == 256 && g.ring.count == GF_V2_RING);
    poll_done = 1; CHECK(!gf_v2_fence_state(&g, &completed, &failed));
    CHECK(completed == 257 && g.ring.count == 0);
    CHECK(!gf_v2_fence_emit(&g, 258, 9)); poll_rc = GF2_EFAULT;
    CHECK(gf_v2_fence_state(&g, &completed, &failed) == GF2_EFAULT);
    CHECK(g.failed_upto == 258);
    die(0);
}
