/* B12: F1 irq.c:106-117 preserves FIFO/GR requests and programs the
 * VBLANK bit for each head; nouveau nvkm/engine/disp/nv04.c:30-44. */
#define IO_H
static unsigned host_flags = 0x202U, saves, restores;
static __attribute__((unused)) unsigned irq_save(void)
{ unsigned v=host_flags; host_flags &= ~0x200U; ++saves; return v; }
static __attribute__((unused)) void irq_restore(unsigned v)
{ host_flags=v; ++restores; }
#define GF4_HOST_UNIT 12
#include "gf4_display_fake.h"
#define REQUIRE(x) do { __asm__ volatile("" : : : "memory"); if (!(x)) return __LINE__; } while (0)
static struct gf4_host h;
static struct gf4_state state;
static struct gf4_host_event events[16];

#include "../../hwdev/gf/display/heads.c"
static void observe(struct gf4_host *host, const struct gf4_host_event *e)
{
    (void)host;
    if (e->op == D_IRQ) assert(!(host_flags & 0x200U));
}
static int test(void)
{
    /* Two independent head bits, as in F1 irq.c:112-117. */
    const u32 head_a = 1U << 0, head_b = 1U << 1;
    gf4_host_init(&h, &state);
    h.observe = observe;
    h.events = events; h.cevents = sizeof(events)/sizeof(events[0]);
    for (u32 fifo = 0; fifo != 2; ++fifo) {
        for (u32 graph = 0; graph != 2; ++graph) {
            for (u32 mask = 0; mask <= (head_a | head_b); ++mask) {
                gf4_host_trace_clear(&h);
                h.base.f.wanted_fifo = fifo ? U32_MAX : 0;
                h.base.f.wanted_graph = graph ? U32_MAX : 0;
                h.base.ch.heads = head_a | head_b;
                REQUIRE(gf4_heads_apply(&h.ctx, mask) == 0);
                REQUIRE(host_flags == 0x202U && saves == restores);
                REQUIRE(h.nevents == 1 && events[0].op == D_IRQ);
                REQUIRE(events[0].p[0] == &h.base.f);
                REQUIRE(events[0].a[0] == fifo && events[0].a[1] == graph && events[0].a[2] == mask);
                REQUIRE(h.base.f.wanted_heads == mask && h.base.ch.heads == mask);
                REQUIRE(h.base.f.wanted_fifo == (fifo ? U32_MAX : 0));
                REQUIRE(h.base.f.wanted_graph == (graph ? U32_MAX : 0));
            }
        }
    }
    /* F1 commits wanted before a bus error; rollback belongs to caller. */
    gf4_host_trace_clear(&h);
    h.base.f.wanted_fifo = U32_MAX; h.base.f.wanted_graph = 0;
    h.base.f.wanted_heads = head_a | head_b; h.base.ch.heads = head_a | head_b;
    gf4_host_fail_at(&h, D_IRQ, 1, GF1_EIO);
    REQUIRE(gf4_heads_apply(&h.ctx, head_b) == GF1_EIO);
    REQUIRE(host_flags == 0x202U && saves == restores);
    REQUIRE(h.nevents == 1 && events[0].op == D_IRQ);
    REQUIRE(events[0].a[0] == 1 && events[0].a[1] == 0 && events[0].a[2] == head_b);
    REQUIRE(h.base.ch.heads == (head_a | head_b));
    REQUIRE(h.base.f.wanted_heads == head_b && h.base.f.wanted_fifo == U32_MAX && !h.base.f.wanted_graph);
    gf4_host_fail_at(&h, D_IRQ, 0, 0);
    gf4_host_trace_clear(&h);
    host_flags = 2U;
    REQUIRE(gf4_heads_apply(&h.ctx, head_a | head_b) == 0);
    REQUIRE(host_flags == 2U && saves == restores);
    REQUIRE(h.base.ch.heads == (head_a | head_b) && h.base.f.wanted_heads == (head_a | head_b));
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
