/* Link accel_objects.c only, no libc. G1/P2-1 selects all dependencies
 * from A1; observe checks borrowed arguments before their lifetime ends.
 * Oracle: F3 section 3 / MIT xf86-video-nouveau nv_accel_common.c:185-578.
 * Literal expectations stay in-tree; no external checkout is read at runtime. */
#define GF3_HOST_UNIT 0
#include "gf_accel_host.h"

/* Deliberately literal classes, independent from product class aliases. */
static const u16 b0_classes[] = {
    0x30, 0x62, 0x12, 0x72, 0x44, 0x43, 0x4a, 0x9f, 0x3089, 0x19, 0x39, 0x8a
};
/* Handle abbreviations describe the section 3 oracle, not product enums. */
#define H(i) (0x80000100U + (i))
#define N H(0)
#define S H(1)
#define V 0x80000002U
static const struct { u8 subc; u16 method, count; u32 data[7]; } b0_expected[] = {
    {2,0,1,{S}}, {2,0x180,1,{N}}, {2,0x184,2,{V,V}},
    {6,0,1,{H(2)}}, {6,0x300,1,{0x7f800000}},
    {6,0,1,{H(3)}}, {6,0x300,1,{0xffff0000}},
    {6,0,1,{H(4)}}, {6,0x180,1,{N}}, {6,0x304,3,{1,0,1}},
    {6,0,1,{H(5)}}, {6,0x180,1,{N}},
    {3,0,1,{H(6)}}, {3,0x180,1,{N}}, {3,0x184,1,{N}},
    {3,0x198,1,{S}}, {3,0x18c,1,{H(5)}}, {3,0x188,1,{H(4)}},
    {3,0x2fc,1,{1}}, {3,0x304,1,{1}},
    {4,0,1,{H(7)}}, {4,0x180,1,{N}}, {4,0x184,1,{N}},
    {4,0x19c,1,{S}}, {4,0x188,3,{N,H(4),H(5)}},
    {4,0x2fc,1,{1}}, {4,0x120,3,{0,1,2}},
    {6,0,1,{H(8)}}, {6,0x180,7,{N,V,N,N,H(2),H(3),S}},
    {6,0x2fc,1,{0}}, {6,0x304,1,{3}},
    {6,0,1,{H(9)}}, {6,0x180,1,{N}},
    {1,0,1,{H(10)}}, {1,0x180,1,{H(12)}},
    {5,0,1,{H(11)}}, {5,0x180,1,{N}}, {5,0x188,1,{N}},
    {5,0x18c,1,{N}}, {5,0x190,1,{N}}, {5,0x194,1,{N}},
    {5,0x198,1,{N}}, {5,0x19c,1,{S}}, {5,0x2fc,1,{3}}
};
#define B0_PACKET_COUNT (sizeof(b0_expected) / sizeof(b0_expected[0]))
#define B0_CLASS_COUNT (sizeof(b0_classes) / sizeof(b0_classes[0]))
/* Notifier contract: accel.h:69; F1/F2 descriptor and alignment contract:
 * command.h:56-57 and dmaobj.c:33-46. Keep numeric oracles independent. */
#define B0_NOTIFY_BYTES 32U
#define B0_OBJECT_ALIGN 16U
#define B0_DMA_OBJECT_BYTES 16U
/* Non-display inst offset exercises the absolute notifier conversion. */
#define B0_STORAGE_OFF 0x50000U
#define B0_VRAM_BYTES (64U * 1024U * 1024U)
#define B0_RESERVED_BYTES (1024U * 1024U)
#define B0_NOTIFY_OFF (B0_VRAM_BYTES - B0_RESERVED_BYTES + B0_STORAGE_OFF)

/* Trace capacities are fixture budgets, derived from the complete setup:
 * one packet per oracle row, at most seven words per row, and room for the
 * lifecycle callbacks (including nested fault/clock events) per operation. */
static struct gf3_host b0;
static struct gf3_host_packet packets[B0_PACKET_COUNT];
static u32 words[B0_PACKET_COUNT * 7U];
static struct gf3_host_event events[(B0_PACKET_COUNT + B0_CLASS_COUNT) * 16U];
static u32 created, deleted, waited, emitted, dma_created;
static u32 deadline;

static void observe(struct gf3_host *h, const struct gf3_host_event *e)
{
    assert(h == &b0 && !h->ctx.ready);
    switch (e->op) {
    case H_ALLOC:
        assert(h->paused && waited == 1 && !h->calls[H_DMA_NEW]);
        assert(e->p[0] == &h->f && e->p[1] == &h->ctx.notify_storage);
        assert(e->a[0] == B0_NOTIFY_BYTES && e->a[1] == B0_OBJECT_ALIGN && !e->a[2]);
        break;
    case H_DMA_NEW:
        assert(h->paused && !created && h->ctx.notify_storage.size == B0_NOTIFY_BYTES);
        assert(e->p[0] == &h->ch && e->p[1] == &h->ctx.notify);
        assert(e->a[0] == H(12) && e->a[1] == 0x3d);
        assert(e->a[2] == 0 && e->a[3] == 2);
        assert(e->a[4] == B0_NOTIFY_OFF && e->a[5] == B0_NOTIFY_OFF + B0_NOTIFY_BYTES - 1U);
        if (!e->rc) dma_created = 1;
        else if (e->rc == GF2_EIO)
            assert(!h->ctx.notify.handle && h->ctx.notify.inst.size == B0_DMA_OBJECT_BYTES);
        break;
    case H_GR_NEW:
        assert(h->paused && !emitted && h->ctx.notify.handle == H(12));
        assert(created < B0_CLASS_COUNT);
        assert(e->p[0] == &h->ch && e->p[1] == &h->ctx.obj[created]);
        assert(e->a[0] == H(created) && e->a[1] == b0_classes[created]);
        if (!e->rc) ++created;
        break;
    case H_DEL:
        assert(h->paused && e->p[0] == &h->ch);
        assert(e->p[1] == (deleted < created ?
               &h->ctx.obj[created - deleted - 1] : &h->ctx.notify));
        if (!e->rc) ++deleted;
        break;
    case H_FREE:
        assert(h->paused && deleted == created + dma_created);
        assert(e->p[0] == &h->f && e->p[1] == &h->ctx.notify_storage);
        assert(!h->live_objects && !h->ctx.notify.inst.size);
        break;
    case H_PAUSE:
        assert(e->p[0] == &h->ch);
        assert(waited == (h->calls[H_EMIT] ? 3U : 1U));
        break;
    case H_RESUME:
        assert(e->p[0] == &h->ch);
        assert((created == B0_CLASS_COUNT && !deleted) || !h->live_objects);
        assert(e->rc ? h->ch.last_rc == e->rc : !h->paused);
        /* RESUME injection records last_rc only (fifo.c:232-238).
         * FAULT injection has its own state/latch assertions below. */
        assert(h->ch.state == GF2_RUN && !h->ch.fault.latched);
        break;
    case H_FENCE:
        assert(!h->paused && e->p[0] == &h->ch && e->p[1]);
        assert(e->a[0] == deadline);
        if (e->nth == 1) assert(!h->calls[H_ALLOC] && !emitted);
        else assert(emitted == B0_PACKET_COUNT);
        break;
    case H_WAIT:
        assert(!h->paused && e->p[0] == &h->ch);
        assert(e->a[0] == h->ctx.epoch && e->a[1] == h->ch.seq);
        assert(e->a[2] == deadline);
        if (!e->rc) ++waited;
        break;
    case H_EMIT: {
        assert(!h->paused && created == B0_CLASS_COUNT && waited == 1);
        assert(e->p[0] == &h->ch && emitted < B0_PACKET_COUNT);
        const u32 *data = e->p[1];
        assert(e->a[0] == b0_expected[emitted].subc);
        assert(e->a[1] == b0_expected[emitted].method && !e->a[2]);
        assert(e->a[3] == b0_expected[emitted].count && e->a[4] == deadline);
        for (u32 j = 0; j < e->a[3]; ++j)
            assert(data[j] == b0_expected[emitted].data[j]);
        ++emitted;
        break;
    }
    case H_STOP:
        assert(e->p[0] == &h->ch);
        break;
    case H_FAULT: case H_CLOCK: case H_SLEEP:
        break;
    default:
        assert(0);
    }
}

static void reset(void)
{
    gf3_host_init(&b0);
    b0.ctx = (struct gf3_ctx){0};
    b0.packets = packets; b0.cpackets = B0_PACKET_COUNT;
    b0.words = words; b0.cwords = sizeof(words) / sizeof(words[0]);
    b0.events = events; b0.cevents = sizeof(events) / sizeof(events[0]);
    b0.f.imem.vram_bytes = B0_VRAM_BYTES;
    b0.f.imem.reserved = b0.f.imem.aperture_bytes = B0_RESERVED_BYTES;
    b0.alloc_off = B0_STORAGE_OFF;
    b0.ch.epoch = 7;
    b0.observe = observe;
    created = deleted = waited = emitted = dma_created = 0;
    deadline = 123456U; /* Caller-owned absolute deadline, never renewed. */
}
static int init(void) { return gf3_init(&b0.ctx, &b0.ch, deadline); }
static int fini(void) { return gf3_fini(&b0.ctx, deadline); }
static void dead(void)
{
    assert(!b0.ctx.ready && !b0.live_objects && !b0.f.imem.used);
    assert(!b0.ctx.notify_storage.size && !b0.ctx.notify.handle);
    assert(!b0.ctx.notify.cookie && !b0.ctx.notify.inst.size && !b0.ctx.notify_off);
    for (u32 i = 0; i < B0_CLASS_COUNT; ++i)
        assert(!b0.ctx.obj[i].handle && !b0.ctx.obj[i].cookie && !b0.ctx.obj[i].inst.size);
}
static void retained(void)
{
    assert(!b0.ctx.ready && b0.live_objects == B0_CLASS_COUNT + 1);
    assert(b0.f.imem.used && b0.ctx.notify_storage.size == B0_NOTIFY_BYTES);
    assert(b0.ctx.notify.handle == H(12));
    for (u32 i = 0; i < B0_CLASS_COUNT; ++i)
        assert(b0.ctx.obj[i].handle == H(i) && b0.ctx.obj[i].cookie);
}
static void inject(enum gf3_host_op op, u32 nth, int rc)
{ gf3_host_fail_at(&b0, op, nth, rc); }

void _start(void)
{
    reset();
    assert(init() == 0 && b0.ctx.last_rc == 0);
    assert(b0.ctx.ready && b0.ctx.epoch == 7 && waited == 2);
    assert(b0.npackets == B0_PACKET_COUNT && !b0.calls[H_STOP]);
    for (u32 i = 0; i < B0_PACKET_COUNT; ++i) {
        const struct gf3_host_packet *p = &packets[i];
        assert(p->subc == b0_expected[i].subc && p->method == b0_expected[i].method);
        assert(!p->noninc && p->count == b0_expected[i].count && p->deadline == deadline);
        for (u32 j = 0; j < p->count; ++j)
            assert(words[p->first+j] == b0_expected[i].data[j]);
    }
    assert(init() == GF3_ESTATE);
    assert(fini() == 0 && waited == 3 && deleted == B0_CLASS_COUNT + 1);
    dead();
    assert(!b0.paused && !b0.calls[H_STOP]);
    u32 nevents = b0.nevents;
    assert(fini() == 0 && b0.nevents == nevents);

    for (u32 i = 1; i <= B0_CLASS_COUNT; ++i) {
        reset(); inject(H_GR_NEW, i, GF2_ENOSPC);
        assert(init() == GF2_ENOSPC && b0.ctx.last_rc == GF2_ENOSPC);
        dead();
        assert(created == i-1 && deleted == i && !b0.paused);
        assert(!b0.calls[H_STOP] && !emitted);
    }
    for (u32 i = 1; i <= B0_PACKET_COUNT; ++i) {
        reset(); inject(H_EMIT, i, GF2_EIO);
        assert(init() == GF2_EIO && b0.ctx.last_rc == GF2_EIO);
        dead();
        assert(b0.calls[H_STOP] == 1 && !deleted && emitted == i);
        assert(b0.npackets == i-1 && b0.ch.state == GF2_OFF);
    }
    for (u32 i = 1; i <= 2; ++i) {
        reset(); inject(H_WAIT, i, GF2_ETIME);
        assert(init() == GF2_ETIME && b0.calls[H_STOP] == 1); dead();
        reset(); inject(H_FENCE, i, GF2_EIO);
        assert(init() == GF2_EIO && b0.calls[H_WAIT] == i-1); dead();
    }
    reset(); inject(H_EMIT, 1, GF2_EIO); inject(H_STOP, 1, GF2_ESTOP);
    assert(init() == GF2_ESTOP && b0.ctx.last_rc == GF2_ESTOP); retained();
    assert(!deleted && !b0.calls[H_FREE]);

    reset(); inject(H_ALLOC, 1, GF1_ENOSPC);
    assert(init() == GF1_ENOSPC && !b0.paused && !emitted); dead();
    assert(!b0.calls[H_STOP] && !b0.calls[H_DMA_NEW]);
    reset(); inject(H_DMA_NEW, 1, GF2_EIO);
    assert(init() == GF2_EIO && !b0.paused && !emitted); dead();
    assert(!b0.calls[H_STOP] && !b0.calls[H_DEL] && b0.calls[H_FREE] == 1);
    reset(); inject(H_RESUME, 1, GF2_EIO);
    assert(init() == GF2_EIO && b0.calls[H_STOP] == 1 && !emitted); dead();
    reset(); inject(H_PAUSE, 1, GF2_ETIME);
    assert(init() == GF2_ETIME && b0.calls[H_STOP] == 1); dead();
    reset(); inject(H_GR_NEW, 2, GF2_ENOSPC); inject(H_DEL, 1, GF2_EIO);
    assert(init() == GF2_EIO && b0.calls[H_STOP] == 1); dead();
    reset(); inject(H_GR_NEW, 2, GF2_ENOSPC); inject(H_RESUME, 1, GF2_EIO);
    assert(init() == GF2_EIO && b0.calls[H_STOP] == 1); dead();

    /* Each teardown stage must propagate failure without releasing live
     * ownership prematurely. Deletion stops at the first failed object. */
    reset(); assert(init() == 0); inject(H_FENCE, 3, GF2_EIO);
    assert(fini() == GF2_EIO && !deleted); retained();
    reset(); assert(init() == 0); inject(H_WAIT, 3, GF2_ETIME);
    assert(fini() == GF2_ETIME && !deleted); retained();
    reset(); assert(init() == 0); inject(H_PAUSE, 2, GF2_EIO);
    assert(fini() == GF2_EIO && !deleted); retained();
    for (u32 i = 1; i <= B0_CLASS_COUNT + 1; ++i) {
        reset(); assert(init() == 0); inject(H_DEL, i, GF2_EIO);
        assert(fini() == GF2_EIO && deleted == i-1);
        assert(b0.live_objects == B0_CLASS_COUNT + 2-i);
        assert(b0.ctx.notify_storage.size && b0.f.imem.used && !b0.calls[H_FREE]);
    }
    reset(); assert(init() == 0); inject(H_RESUME, 2, GF2_EIO);
    assert(fini() == GF2_EIO); dead();
    reset(); assert(init() == 0); ++b0.ch.epoch;
    assert(fini() == GF3_ESTATE && b0.calls[H_FENCE] == 2); retained();
    --b0.ch.epoch; b0.ch.fault.latched = true;
    assert(fini() == GF2_EFAULT && b0.calls[H_FENCE] == 2); retained();
    reset(); assert(init() == 0); b0.ch.state = GF2_OFF;
    assert(fini() == GF3_ESTATE && b0.calls[H_FENCE] == 2); retained();

    reset(); inject(H_FAULT, 1, GF2_EFAULT);
    assert(init() == GF2_EFAULT && !b0.calls[H_FENCE]); dead();
    assert(b0.ch.state == GF2_FAULT && b0.ch.fault.latched);
    assert(b0.ch.last_rc == GF2_EFAULT);
    reset(); b0.now = deadline;
    assert(init() == GF2_ETIME && b0.calls[H_STOP] == 1); dead();
    reset(); b0.now = deadline-1; b0.complete_after = U32_MAX;
    assert(init() == GF2_ETIME && b0.waits == 1); dead();
    reset(); b0.ch.state = GF2_OFF;
    assert(init() == GF3_ESTATE && !b0.nevents);
    reset(); b0.ctx.obj[0].cookie = 1;
    assert(init() == GF3_ESTATE && !b0.nevents);
    reset();
    assert(gf3_init(0, &b0.ch, deadline) == GF3_EINVAL);
    assert(gf3_init(&b0.ctx, 0, deadline) == GF3_EINVAL);
    assert(gf3_fini(0, deadline) == GF3_EINVAL);
    assert(!b0.nevents);
    gf2_host_exit(0);
}
