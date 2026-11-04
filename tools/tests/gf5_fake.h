/* F5 shared freestanding fixture, GF_FEAT4_DESIGN A1/G1 and F5 final A0.
 * Include in exactly one host test TU. Link the selected product TUs.
 * UNIT 0..6 = B0..B6, 7 = real integration (no external stubs),
 * 8 = ROM (allocation callbacks), 9 = links (F2/F4/clock boundaries).
 * F4 UNIT14 and F3 UNIT12 provide helpers, never overlapping B stubs.
 * Borrow events, MMIO/read scripts, ROM, reservation nodes, VRAM and arena.
 * Configure display.events/cevents as well as F5 events before any access.
 * observe runs after a boundary's side effects and before returning to the
 * product. For calculation/GPIO/read stubs, it seeds successful outputs
 * through e->p[]; no product formula is repeated here. Failed outputs stay
 * untouched. Clear trace resets counts, not injection, time or ownership.
 * F4's D_READ/D_WRITE/D_SLEEP trace observes indexed MMIO and shared time;
 * F5 events observe only F5 boundaries. Assertions use the inherited asm
 * barrier and cannot disappear under IPA. No libc or private test stubs. */
#ifndef TOOLS_TESTS_GF5_FAKE_H
#define TOOLS_TESTS_GF5_FAKE_H
#if !defined(GF5_HOST_UNIT) || GF5_HOST_UNIT < 0 || GF5_HOST_UNIT > 9
#error "GF5_HOST_UNIT must select B0..B6, real integration, ROM, or links (0..9)"
#endif
#if defined(GF4_HOST_UNIT) && GF4_HOST_UNIT != 14
#error "GF5 requires GF4_HOST_UNIT=14 (shared helpers only)"
#endif
#ifndef GF4_HOST_UNIT
#define GF4_HOST_UNIT 14
#endif
#include "gf4_display_fake.h"
#include "../../hwdev/gf/feature5.h"
#include "../../hwdev/gf/internal.h"
#include "../../include/sys.h"
#if GF5_HOST_UNIT == 9
#include "../../mm/pgalloc.h"
#endif

/* Frozen event schema: 12 scalar and 4 pointer arguments. MATCH needs four
 * scalars/three pointers, as does CALC; pointer capacity is rounded to four. */
#define GF5_HOST_VALUE_ARGS 12U
#define GF5_HOST_POINTER_ARGS 4U
/* Arena payload alignment, including u64 and all F5/F4 snapshot metadata.
 * Headers are internal host bookkeeping, not a product snapshot ceiling. */
#define GF5_HOST_ALLOC_ALIGN 16U
#define GF5_HOST_ALLOC_HEADER 16U

enum gf5_host_op {
    F5_ADMIT,F5_DRAIN,F5_PAUSE,F5_RESUME,F5_STOP,F5_RESTART,F5_SAVE,F5_RESTORE,
    F5_SCRIPT,F5_ALLOC,F5_FREE,F5_COPY,F5_NEXT,F5_MATCH,F5_CLOCK64,
    F5_GPIO_FIND,F5_GPIO_GET,F5_GPIO_SET,F5_PERF,F5_VOLT,F5_FAN,F5_CALC,
    F5_PROG,F5_RAM,F5_POINT,F5_CLEANUP,F5_OP_COUNT
};
/* a[0] distinguishes same-family stub operations; remaining a[] and p[]
 * follow the public signature (ctx first). For CALC: p[1]=request,p[2]=out;
 * READ: p[1]=out; GET/COUNT use their public output pointer. */
enum gf5_host_call {
    F5_CALL_READ,F5_CALL_CALC,F5_CALL_GET,F5_CALL_SET,F5_CALL_ID,
    F5_CALL_COUNT,F5_CALL_SELECT,F5_CALL_MODE,F5_CALL_CSTATE,F5_CALL_TICK,
    F5_CALL_THERM,F5_CALL_PREPARE,F5_CALL_EXECUTE,F5_CALL_THERM_RESUME
};
struct gf5_host_event {
    enum gf5_host_op op; u32 nth,a[GF5_HOST_VALUE_ARGS];
    const void *p[GF5_HOST_POINTER_ARGS]; i32 rc;
};
struct gf5_host {
    struct gf4_host display;
    struct gf5 ctx; u64 now;
    struct gf5_host_event *events;
    u32 cevents,nevents,calls[F5_OP_COUNT],fail_at[F5_OP_COUNT];
    i32 fail_rc[F5_OP_COUNT];
    u8 *arena; u32 arena_bytes,arena_used,live_allocs;
    void (*observe)(struct gf5_host *,const struct gf5_host_event *);
};
static struct gf5_host *gf5_host_current;
static inline struct gf5_host_event *gf5_host_begin(struct gf5_host *h,enum gf5_host_op op)
{
    assert(h && (unsigned)op<F5_OP_COUNT && h->events && h->nevents<h->cevents);
    struct gf5_host_event *e=&h->events[h->nevents++];
    gf2_host_memset(e,0,sizeof(*e)); e->op=op; e->nth=++h->calls[op];
    if (h->fail_rc[op] && (!h->fail_at[op] || h->fail_at[op]==e->nth))
        e->rc=h->fail_rc[op];
    return e;
}
static inline i32 gf5_host_end(struct gf5_host *h,struct gf5_host_event *e)
{
    if (h->observe) h->observe(h,e);
    return e->rc;
}
static inline void gf5_host_trace_clear(struct gf5_host *h)
{
    assert(h); h->nevents=0; gf2_host_memset(h->calls,0,sizeof(h->calls));
    gf4_host_trace_clear(&h->display);
}
static inline void gf5_host_fail_at(struct gf5_host *h,enum gf5_host_op op,u32 nth,i32 rc)
{
    /* free and fan_tick are void, with no fallible product return. */
    assert(h && (unsigned)op<F5_OP_COUNT && op!=F5_FREE && rc<=0);
    h->fail_at[op]=nth; h->fail_rc[op]=rc;
}
static inline struct gf5_host *gf5_host_context(struct gf5 *g)
{
    assert(gf5_host_current && g==&gf5_host_current->ctx);
    return gf5_host_current;
}
/* links.owner uses the same resident gf5 as real C2-links. */
static inline struct gf5_host *gf5_host_owner(void *owner)
{
    return gf5_host_context(owner);
}
/* Script I/O borrows the F4 indexed MMIO and full shared clock. These
 * callbacks use its opaque unchanged, so read/write/timing remain usable.
 * POINT and CLEANUP have independent counts/injection via the usual API;
 * p[0]=opaque, CLEANUP p[1]=ctx, a[0]=saved_count on entry. Successful fake
 * cleanup consumes saves, failure retains them. No register replay here. */
static inline int gf5_host_point(void *opaque)
{
    struct gf5_host *h=gf5_host_current; assert(h && opaque==&h->display);
    struct gf5_host_event *e=gf5_host_begin(h,F5_POINT);
    e->p[0]=opaque;
    assert(!e->rc || e->rc==GF_SCRIPT_EIO);
    return gf5_host_end(h,e);
}
static inline int gf5_host_cleanup(void *opaque,struct gf_init_context *ctx)
{
    struct gf5_host *h=gf5_host_current; assert(h && opaque==&h->display && ctx);
    struct gf5_host_event *e=gf5_host_begin(h,F5_CLEANUP);
    e->p[0]=opaque; e->p[1]=ctx; e->a[0]=ctx->saved_count;
    assert(!e->rc || e->rc==GF_SCRIPT_EIO);
    if (!e->rc) ctx->saved_count=0;
    return gf5_host_end(h,e);
}
static inline void gf5_host_init_io(struct gf5_host *h,struct gf_init_context *ctx)
{
    assert(h && h==gf5_host_current && ctx);
    ctx->io=h->display.base.f.io;
    ctx->io.point=gf5_host_point; ctx->io.restore=gf5_host_cleanup;
}
struct gf5_host_allocation { u32 span,bytes,live,reserved; };
static inline void gf5_host_arena(struct gf5_host *h,u8 *arena,u32 bytes)
{
    assert(h && !h->live_allocs && arena && bytes &&
        !((uptr)arena&(GF5_HOST_ALLOC_ALIGN-1U)));
    assert((uptr)arena<=U32_MAX-(bytes-1U));
    assert(sizeof(struct gf5_host_allocation)==GF5_HOST_ALLOC_HEADER);
    h->arena=arena; h->arena_bytes=bytes; h->arena_used=0;
}
static inline i32 gf5_host_alloc(void *owner,u32 bytes,void **out)
{
    struct gf5_host *h=gf5_host_owner(owner); assert(out);
    struct gf5_host_event *e=gf5_host_begin(h,F5_ALLOC);
    e->p[0]=owner; e->p[1]=out; e->a[0]=bytes;
    u32 mask=GF5_HOST_ALLOC_ALIGN-1U,span=0,off=0;
    if (!e->rc && !bytes) e->rc=GF5_EINVAL;
    if (!e->rc && (!h->arena || bytes>U32_MAX-GF5_HOST_ALLOC_HEADER-mask)) e->rc=GF5_ENOMEM;
    if (!e->rc) {
        span=(bytes+GF5_HOST_ALLOC_HEADER+mask)&~mask;
        for (;off<h->arena_used;) {
            struct gf5_host_allocation *a=(void *)(h->arena+off);
            assert(a->span && a->span<=h->arena_used-off);
            /* Coalesce adjacent freed spans, without an allocation table. */
            while (!a->live && a->span<h->arena_used-off) {
                struct gf5_host_allocation *next=(void *)(h->arena+off+a->span);
                if (next->live) break;
                assert(next->span && next->span<=h->arena_used-off-a->span);
                a->span+=next->span;
            }
            if (!a->live && a->span>=span) break;
            off+=a->span;
        }
        if (off==h->arena_used && span>h->arena_bytes-off) e->rc=GF5_ENOMEM;
    }
    if (!e->rc) {
        struct gf5_host_allocation *a=(void *)(h->arena+off);
        u32 available=off==h->arena_used ? span : a->span;
        if (available>span) {
            struct gf5_host_allocation *rest=(void *)(h->arena+off+span);
            *rest=(struct gf5_host_allocation){available-span,0,0,0};
        }
        *a=(struct gf5_host_allocation){span,bytes,1,0};
        if (off==h->arena_used) h->arena_used+=span;
        ++h->live_allocs; *out=h->arena+off+GF5_HOST_ALLOC_HEADER;
        e->p[2]=*out;
    }
    return gf5_host_end(h,e);
}
static inline void gf5_host_free(void *owner,void *allocation)
{
    struct gf5_host *h=gf5_host_owner(owner); assert(allocation);
    struct gf5_host_event *e=gf5_host_begin(h,F5_FREE);
    e->p[0]=owner; e->p[1]=allocation; e->rc=0;
    bool found=false;
    for (u32 off=0;off<h->arena_used;) {
        struct gf5_host_allocation *a=(void *)(h->arena+off);
        assert(a->span && a->span<=h->arena_used-off);
        if (h->arena+off+GF5_HOST_ALLOC_HEADER==allocation) {
            assert(a->live && h->live_allocs); a->live=0; --h->live_allocs;
            e->a[0]=a->bytes; found=true; break;
        }
        off+=a->span;
    }
    assert(found); (void)gf5_host_end(h,e);
}
static inline i32 gf5_host_clock64(void *owner,u64 *us)
{
    struct gf5_host *h=gf5_host_owner(owner); assert(us);
    struct gf5_host_event *e=gf5_host_begin(h,F5_CLOCK64);
    e->p[0]=owner; e->p[1]=us;
    if (!e->rc) *us=h->now;
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_admission(void *owner,bool open)
{
    struct gf5_host *h=gf5_host_owner(owner);
    struct gf5_host_event *e=gf5_host_begin(h,F5_ADMIT);
    e->p[0]=owner; e->a[0]=open;
    if (!e->rc) e->rc=gf4_host_admission(&h->display,open);
    if (!e->rc) h->ctx.accepting=open;
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_drain(void *owner,u32 dl)
{
    struct gf5_host *h=gf5_host_owner(owner);
    struct gf5_host_event *e=gf5_host_begin(h,F5_DRAIN);
    e->p[0]=owner; e->a[0]=dl;
    if (!e->rc && (i32)((u32)h->now-dl)>=0) e->rc=GF2_ETIME;
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_drain_pause(void *owner)
{
    struct gf5_host *h=gf5_host_owner(owner);
    struct gf5_host_event *e=gf5_host_begin(h,F5_PAUSE);
    e->p[0]=owner; e->a[0]=h->ctx.deadline;
    /* Even a failed pause may have closed HW. Never fake reopening. */
    h->ctx.accepting=h->display.ctx.accepting=h->display.scan_hook_open=false;
    h->display.base.paused=true;
    if (!e->rc && (i32)((u32)h->now-h->ctx.deadline)>=0) e->rc=GF2_ETIME;
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_unpause(void *owner)
{
    struct gf5_host *h=gf5_host_owner(owner);
    struct gf5_host_event *e=gf5_host_begin(h,F5_RESUME); e->p[0]=owner;
    if (!e->rc) h->display.base.paused=false;
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_memory_script(void *owner,u32 pc)
{
    struct gf5_host *h=gf5_host_owner(owner);
    struct gf5_host_event *e=gf5_host_begin(h,F5_SCRIPT);
    e->p[0]=owner; e->p[1]=h->ctx.rom.memory_program;
    e->a[0]=pc; e->a[1]=h->ctx.deadline;
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_display_save(void *owner,struct gf5_image *image)
{
    struct gf5_host *h=gf5_host_owner(owner); assert(image);
    struct gf5_host_event *e=gf5_host_begin(h,F5_SAVE);
    e->p[0]=owner; e->p[1]=image; e->a[0]=h->ctx.deadline;
    if (!e->rc) {
        struct gf4_saved *s=&image->display;
        *s=(struct gf4_saved){.version=GF4_SAVED_VERSION,.size=sizeof(*s),
            .wanted_fifo=h->display.base.f.wanted_fifo,
            .wanted_graph=h->display.base.f.wanted_graph,
            .wanted_heads=h->display.base.f.wanted_heads};
        gf2_host_memcpy(s->head,h->display.state->head,sizeof(s->head));
        gf2_host_memcpy(s->hold,h->display.ctx.hold,sizeof(s->hold));
        h->display.ctx.saved_closed=true;
        h->ctx.accepting=h->display.ctx.accepting=h->display.scan_hook_open=false;
        h->display.base.f.wanted_heads=0;
    }
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_display_restore(void *owner,const struct gf5_image *image)
{
    struct gf5_host *h=gf5_host_owner(owner); assert(image);
    struct gf5_host_event *e=gf5_host_begin(h,F5_RESTORE);
    e->p[0]=owner; e->p[1]=image; e->a[0]=h->ctx.deadline;
    if (!e->rc) {
        const struct gf4_saved *s=&image->display;
        if (s->version!=GF4_SAVED_VERSION || s->size!=sizeof(*s)) e->rc=GF4_EINVAL;
        else {
            gf2_host_memcpy(h->display.state->head,s->head,sizeof(s->head));
            /* display/save.c:267,328: cancel pending flips on restore. */
            for (u8 head=0;head<GF4_HEADS;++head) {
                if (s->hold[head].pending)
                    h->display.state->head[head].front=s->hold[head].old_front;
                h->display.ctx.hold[head]=(struct gf4_flip_hold){0};
            }
            h->display.base.f.wanted_fifo=s->wanted_fifo;
            h->display.base.f.wanted_graph=s->wanted_graph;
            h->display.base.f.wanted_heads=s->wanted_heads;
            /* Low-level F4 restore does not open admission/saved_closed. */
        }
    }
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_stop(void *owner,u32 dl)
{
    struct gf5_host *h=gf5_host_owner(owner);
    struct gf5_host_event *e=gf5_host_begin(h,F5_STOP);
    e->p[0]=owner; e->a[0]=dl;
    h->ctx.accepting=h->display.ctx.accepting=h->display.scan_hook_open=false;
    if (!e->rc) {
        h->display.base.ch.state=GF2_OFF;
        h->display.base.ctx.ready=h->display.ctx.ready=false;
        /* Actual nodes and borrowed image remain resident across OFF. */
    }
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_restart(void *owner,u32 dl)
{
    struct gf5_host *h=gf5_host_owner(owner);
    struct gf5_host_event *e=gf5_host_begin(h,F5_RESTART);
    e->p[0]=owner; e->a[0]=dl;
    if (!e->rc && h->display.base.ch.state!=GF2_OFF) e->rc=GF4_ESTATE;
    if (!e->rc) {
        struct gf2_chan *ch=&h->display.base.ch;
        ++ch->epoch; ch->seq=0; ch->state=GF2_RUN;
        ch->fault=(struct gf2_fault){0};
        h->display.base.ctx.epoch=h->display.ctx.epoch=ch->epoch;
        h->display.base.ctx.ready=h->display.ctx.ready=true;
        /* Slots/admission/saved_closed/keys remain closed/unchanged. */
    }
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_match(struct gf5_host *h,struct gf2_chan *ch,const void *owner,
                                 u32 id,u32 generation,u32 off,u32 bytes,struct gf_vram_reservation *out)
{
    struct gf5_host_event *e=gf5_host_begin(h,F5_MATCH);
    e->p[0]=ch; e->p[1]=owner; e->p[2]=out;
    e->a[0]=id; e->a[1]=generation; e->a[2]=off; e->a[3]=bytes;
    if (!e->rc) e->rc=gf3_host_match(&h->display.base,ch,owner,id,generation,off,bytes,out);
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_next(struct gf5_host *h,struct gf2_chan *ch,u32 *cursor,
                                struct gf_vram_reservation *out)
{
    struct gf5_host_event *e=gf5_host_begin(h,F5_NEXT);
    e->p[0]=ch; e->p[1]=cursor; e->p[2]=out;
    if (!e->rc && (!ch || !cursor || !out)) e->rc=GF3_EINVAL;
    if (!e->rc && ch!=&h->display.base.ch) e->rc=GF3_ESTATE;
    if (!e->rc) {
        e->a[0]=*cursor;
        const struct gf_vram_reservation *best=0;
        for (struct gf_vram_reservation_node *n=h->display.base.reservations;n;n=n->next) {
            const struct gf_vram_reservation *v=&n->value;
            if (v->owner && v->reservation_id>*cursor &&
                (!best || v->reservation_id<best->reservation_id)) best=v;
        }
        if (best) { *out=*best; *cursor=best->reservation_id; e->rc=1; }
    }
    return gf5_host_end(h,e);
}
static inline i32 gf5_host_copy_extent(void *owner,const struct gf_vram_reservation *key,
                                       u8 *ram,bool to_vram)
{
    struct gf5_host *h=gf5_host_owner(owner); assert(key && ram);
    struct gf3_host *base=&h->display.base;
    struct gf5_host_event *e=gf5_host_begin(h,F5_COPY);
    e->p[0]=owner; e->p[1]=key; e->p[2]=ram;
    e->a[0]=to_vram; e->a[1]=key->off; e->a[2]=key->bytes;
    struct gf_vram_reservation live;
    if (!e->rc) e->rc=gf3_host_match(base,h->ctx.ch,key->owner,key->reservation_id,
                                    key->generation,key->off,key->bytes,&live);
    if (!e->rc && (!base->vram || ((key->off|key->bytes)&3U) ||
        key->off>base->vram_bytes || key->bytes>base->vram_bytes-key->off)) e->rc=GF3_ERANGE;
    if (!e->rc) {
        if (to_vram) gf2_host_memcpy(base->vram+key->off,ram,key->bytes);
        else gf2_host_memcpy(ram,base->vram+key->off,key->bytes);
    }
    return gf5_host_end(h,e);
}
static inline void gf5_host_init(struct gf5_host *h,struct gf4_state *state)
{
    assert(h && state); gf2_host_memset(h,0,sizeof(*h));
    gf4_host_init(&h->display,state); h->display.now=&h->now;
    gf5_host_current=h;
    h->ctx.ch=&h->display.base.ch; h->ctx.accel=&h->display.base.ctx;
    h->ctx.display=&h->display.ctx; h->ctx.phase=GF5_RUN; h->ctx.accepting=true;
    h->ctx.links=(struct gf5_links){
        .drain_pause=gf5_host_drain_pause,.unpause=gf5_host_unpause,
        .display_save=gf5_host_display_save,.display_restore=gf5_host_display_restore,
        .memory_script=gf5_host_memory_script,.owner=&h->ctx,
        .admission=gf5_host_admission,.drain=gf5_host_drain,
        .stop=gf5_host_stop,.restart=gf5_host_restart,.copy_extent=gf5_host_copy_extent,
        .alloc=gf5_host_alloc,.free=gf5_host_free,.clock64=gf5_host_clock64
    };
}

/* Only dependency definitions below vary with UNIT. UNIT7 exports none. */
#if GF5_HOST_UNIT <= 6 || GF5_HOST_UNIT == 8
/* Boundary classification for isolated B/ROM tests; UNIT9 links the real
 * gf5_rc_map from C2-links. Unknown values deliberately map to EIO. */
i32 gf5_rc_map(i32 rc)
{
    switch (rc) {
    case 0: case GF5_EINVAL: case GF5_ENOTSUP: case GF5_EBIOS:
    case GF5_ERANGE: case GF5_EBUSY: case GF5_ETIME: case GF5_EIO:
    case GF5_ENOMEM: case GF5_ESTATE: return rc;
    case GF1_EINVAL: case GF2_EINVAL: case GF3_EINVAL: case GF4_EINVAL: return GF5_EINVAL;
    case GF1_ENOSPC: case GF2_ENOSPC: case GF4_ENOSPC: return GF5_ENOMEM;
    case GF3_ERANGE: case GF4_ERANGE: return GF5_ERANGE;
    case GF2_ESTATE: case GF3_ESTATE: case GF4_ESTATE: return GF5_ESTATE;
    case GF3_EUNSUPPORTED: case GF4_EUNSUPPORTED: case GF4_ENODEV: return GF5_ENOTSUP;
    case GF4_EBUSY: return GF5_EBUSY;
    case GF2_ETIME: case GF4_ETIME: case GF_SCRIPT_ETIME: return GF5_ETIME;
    default: return GF5_EIO;
    }
}
#endif
#if GF5_HOST_UNIT == 5 || GF5_HOST_UNIT == 6
int gf_vram_match(struct gf2_chan *ch,const void *owner,u32 id,u32 generation,
                  u32 off,u32 bytes,struct gf_vram_reservation *out)
{
    return gf5_host_match(gf5_host_current,ch,owner,id,generation,off,bytes,out);
}
#endif
#if GF5_HOST_UNIT == 6
int gf_vram_next(struct gf2_chan *ch,u32 *cursor,struct gf_vram_reservation *out)
{
    return gf5_host_next(gf5_host_current,ch,cursor,out);
}
#endif
#if GF5_HOST_UNIT == 3
i32 gf5_gpio_find(struct gf5 *g,u8 tag,u8 *line,u8 log[2])
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_GPIO_FIND);
    e->p[0]=g; e->p[1]=line; e->p[2]=log; e->a[0]=tag;
    return gf5_host_end(h,e);
}
i32 gf5_gpio_get(struct gf5 *g,u8 line,const u8 log[2],bool *out)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_GPIO_GET);
    e->p[0]=g; e->p[1]=log; e->p[2]=out; e->a[0]=line;
    return gf5_host_end(h,e);
}
i32 gf5_gpio_set(struct gf5 *g,u8 line,const u8 log[2],bool value)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_GPIO_SET);
    e->p[0]=g; e->p[1]=log; e->a[0]=line; e->a[1]=value;
    return gf5_host_end(h,e);
}
#endif
#if GF5_HOST_UNIT == 0 || GF5_HOST_UNIT == 6
i32 gf5_clock_calc(struct gf5 *g,const struct gf5_clock *clock,struct gf5_plan *out)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_CALC);
    e->p[0]=g; e->p[1]=clock; e->p[2]=out; e->a[0]=F5_CALL_CALC;
    return gf5_host_end(h,e);
}
i32 gf5_clock_read(struct gf5 *g,struct gf5_clock *out)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_CALC);
    e->p[0]=g; e->p[1]=out; e->a[0]=F5_CALL_READ;
    return gf5_host_end(h,e);
}
i32 gf5_clock_prog(struct gf5 *g,const struct gf5_plan *plan)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_PROG); e->p[0]=g; e->p[1]=plan;
    return gf5_host_end(h,e);
}
i32 gf5_ram_prog(struct gf5 *g,const struct gf5_plan *plan)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_RAM); e->p[0]=g; e->p[1]=plan;
    return gf5_host_end(h,e);
}
i32 gf5_volt_get(struct gf5 *g,u32 *out)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_VOLT);
    e->p[0]=g; e->p[1]=out; e->a[0]=F5_CALL_GET;
    return gf5_host_end(h,e);
}
i32 gf5_volt_set(struct gf5 *g,u32 uv)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_VOLT);
    e->p[0]=g; e->a[0]=F5_CALL_SET; e->a[1]=uv;
    return gf5_host_end(h,e);
}
i32 gf5_volt_id(struct gf5 *g,u8 id,u8 min_id,i32 condition)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_VOLT);
    e->p[0]=g; e->a[0]=F5_CALL_ID; e->a[1]=id; e->a[2]=min_id; e->a[3]=(u32)condition;
    return gf5_host_end(h,e);
}
i32 gf5_fan_cstate(struct gf5 *g,u8 percent,i32 dir)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_FAN);
    e->p[0]=g; e->a[0]=F5_CALL_CSTATE; e->a[1]=percent; e->a[2]=(u32)dir;
    return gf5_host_end(h,e);
}
#endif
#if GF5_HOST_UNIT == 6
i32 gf5_therm_resume(struct gf5 *g,const struct gf5_fan_saved *saved)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_FAN);
    e->p[0]=g; e->p[1]=saved; e->a[0]=F5_CALL_THERM_RESUME;
    return gf5_host_end(h,e);
}
i32 gf5_fan_get(struct gf5 *g,u32 *percent)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_FAN);
    e->p[0]=g; e->p[1]=percent; e->a[0]=F5_CALL_GET;
    return gf5_host_end(h,e);
}
i32 gf5_fan_mode_set(struct gf5 *g,enum gf5_fan_mode mode)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_FAN);
    e->p[0]=g; e->a[0]=F5_CALL_MODE; e->a[1]=mode;
    if (!e->rc) g->fan.mode=mode;
    return gf5_host_end(h,e);
}
void gf5_fan_tick(struct gf5 *g,u64 now)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_FAN);
    e->p[0]=g; e->a[0]=F5_CALL_TICK; e->a[1]=(u32)now; e->a[2]=(u32)(now>>32);
    e->rc=0; (void)gf5_host_end(h,e);
}
i32 gf5_therm_read(struct gf5 *g,struct gf5_thermal *out)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_FAN);
    e->p[0]=g; e->p[1]=out; e->a[0]=F5_CALL_THERM;
    return gf5_host_end(h,e);
}
i32 gf5_perf_count(struct gf5 *g,u32 *out)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_PERF);
    e->p[0]=g; e->p[1]=out; e->a[0]=F5_CALL_COUNT;
    return gf5_host_end(h,e);
}
i32 gf5_perf_get(struct gf5 *g,u32 index,struct gf5_perf_entry *out)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_PERF);
    e->p[0]=g; e->p[1]=out; e->a[0]=F5_CALL_GET; e->a[1]=index;
    return gf5_host_end(h,e);
}
i32 gf5_perf_select(struct gf5 *g,u8 index)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_PERF);
    e->p[0]=g; e->a[0]=F5_CALL_SELECT; e->a[1]=index;
    return gf5_host_end(h,e);
}
i32 gf5_tile_find(struct gf5 *g,u32 *slot)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_PROG);
    e->p[0]=g; e->p[1]=slot; e->a[0]=F5_CALL_GET;
    return gf5_host_end(h,e);
}
i32 gf5_tile_set(struct gf5 *g,u32 slot,const struct gf5_tile *tile)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_PROG);
    e->p[0]=g; e->p[1]=tile; e->a[0]=F5_CALL_SET; e->a[1]=slot;
    return gf5_host_end(h,e);
}
i32 gf5_tile_clear(struct gf5 *g,u32 slot)
{
    struct gf5_host *h=gf5_host_context(g);
    struct gf5_host_event *e=gf5_host_begin(h,F5_PROG);
    e->p[0]=g; e->a[0]=F5_CALL_SET; e->a[1]=slot;
    return gf5_host_end(h,e);
}
#endif
#if GF5_HOST_UNIT == 9
/* Real links calls these F2/F4 boundaries. No F5 adapter or rc_map stub. */
i32 gf_init_script_execute(struct gf_init_context *ctx,
                            const struct gf_init_program *program,u32 pc,
                            u8 head,u8 or_mask,u32 dl)
{
    struct gf5_host *h=gf5_host_current; assert(h && ctx && program);
    struct gf5_host_event *e=gf5_host_begin(h,F5_SCRIPT);
    e->p[0]=ctx; e->p[1]=program; e->a[0]=F5_CALL_EXECUTE;
    e->a[1]=pc; e->a[2]=head; e->a[3]=or_mask; e->a[4]=dl;
    assert(!e->rc || e->rc==GF_SCRIPT_EIO || e->rc==GF_SCRIPT_ETIME);
    ctx->last_rc=e->rc;
    /* The runner owns failure cleanup; the adapter owns success cleanup.
     * Retain the original script rc even when cleanup itself fails. */
    if (e->rc) {
        assert(ctx->io.restore);
        (void)ctx->io.restore(ctx->io.opaque,ctx);
    }
    return gf5_host_end(h,e);
}
int gf_vram_match(struct gf2_chan *ch,const void *owner,u32 id,u32 generation,
                  u32 off,u32 bytes,struct gf_vram_reservation *out)
{
    return gf5_host_match(gf5_host_current,ch,owner,id,generation,off,bytes,out);
}
int gf_vram_view_map(struct gf_vram_view *view,struct gf2_chan *ch,u32 off,u32 len,volatile u8 **out)
{
    return gf4_host_view_map(&gf5_host_current->display,view,ch,off,len,out);
}
int gf_vram_view_unmap(struct gf_vram_view *view)
{
    return gf4_host_view_unmap(&gf5_host_current->display,view);
}
/* Existing identity-mapped page allocator boundary, backed only by the
 * borrowed arena. One extra page holds alignment and host ownership data;
 * this is fixture overhead and never enters the product's size oracle. */
struct gf5_host_pages { void *allocation; u32 owner,pages; };
u32 pgalloc_alloc_phys(u32 owner,int n)
{
    struct gf5_host *h=gf5_host_current; assert(h);
    if (!owner || n<=0 || (u32)n>(U32_MAX-MEM_PAGE_SIZE)/MEM_PAGE_SIZE) return 0;
    void *raw=0;
    if (gf5_host_alloc(&h->ctx,((u32)n+1U)*MEM_PAGE_SIZE,&raw)) return 0;
    struct gf5_host_pages *meta=raw;
    *meta=(struct gf5_host_pages){raw,owner,(u32)n};
    return ((uptr)raw+sizeof(*meta)+MEM_PAGE_SIZE-1U)&~(MEM_PAGE_SIZE-1U);
}
int pgalloc_free_n_owner(u32 owner,u32 pfn,int n)
{
    struct gf5_host *h=gf5_host_current; assert(h);
    if (!owner || n<=0 || pfn>U32_MAX/MEM_PAGE_SIZE) return 0;
    for (u32 off=0;off<h->arena_used;) {
        struct gf5_host_allocation *a=(void *)(h->arena+off);
        assert(a->span && a->span<=h->arena_used-off);
        if (a->live && a->bytes>=MEM_PAGE_SIZE) {
            struct gf5_host_pages *meta=(void *)(h->arena+off+GF5_HOST_ALLOC_HEADER);
            u32 phys=((uptr)meta+sizeof(*meta)+MEM_PAGE_SIZE-1U)&~(MEM_PAGE_SIZE-1U);
            if (phys==pfn*MEM_PAGE_SIZE) {
                if (meta->allocation!=meta || meta->owner!=owner || meta->pages!=(u32)n) return 0;
                gf5_host_free(&h->ctx,meta->allocation); return 1;
            }
        }
        off+=a->span;
    }
    return 0;
}
int sys_time_now(u32 *lo,u32 *hi)
{
    struct gf5_host *h=gf5_host_current; assert(h && lo && hi);
    struct gf5_host_event *e=gf5_host_begin(h,F5_CLOCK64);
    e->p[0]=lo; e->p[1]=hi;
    if (!e->rc) { *lo=(u32)h->now; *hi=(u32)(h->now>>32); }
    return gf5_host_end(h,e);
}
int gf2_fence_emit(struct gf2_chan *ch,struct gf2_fence *out,u32 dl)
{
    struct gf5_host *h=gf5_host_current; assert(h && ch==h->ctx.ch && out);
    struct gf5_host_event *e=gf5_host_begin(h,F5_DRAIN);
    e->p[0]=ch; e->p[1]=out; e->a[0]=dl; e->a[1]=F5_CALL_SET;
    i32 rc=gf4_host_fence_state(&h->display,ch);
    if (rc) e->rc=rc;
    else if (e->rc!=GF2_EFAULT && e->rc!=GF2_ESTATE) {
        u32 seq=++ch->seq;
        if (!e->rc) *out=(struct gf2_fence){ch->epoch,seq};
    }
    return gf5_host_end(h,e);
}
int gf2_fence_wait(struct gf2_chan *ch,struct gf2_fence fence,u32 dl)
{
    struct gf5_host *h=gf5_host_current; assert(h && ch==h->ctx.ch);
    struct gf5_host_event *e=gf5_host_begin(h,F5_DRAIN);
    e->p[0]=ch; e->a[0]=dl; e->a[1]=F5_CALL_GET; e->a[2]=fence.epoch; e->a[3]=fence.seq;
    for (u32 n=0;!e->rc;++n) {
        u32 now;
        e->rc=gf4_host_fence_state(&h->display,ch);
        if (e->rc) break;
        if (fence.epoch!=ch->epoch) { e->rc=GF2_EFAULT; break; }
        if (gf4_host_clock(&h->display,&now)) { e->rc=GF2_EIO; break; }
        if ((i32)(now-dl)>=0) { e->rc=GF2_ETIME; break; }
        if (h->display.base.complete_after!=U32_MAX && n>=h->display.base.complete_after) break;
        u32 delay=dl-now;
        if (delay>GF2_WAIT_MAX_US) delay=GF2_WAIT_MAX_US;
        if (gf4_host_wait(&h->display,delay)) { e->rc=GF2_EIO; break; }
    }
    return gf5_host_end(h,e);
}
int gf2_fifo_pause(struct gf2_chan *ch)
{
    struct gf5_host *h=gf5_host_current; assert(h && ch==h->ctx.ch);
    struct gf5_host_event *e=gf5_host_begin(h,F5_PAUSE); e->p[0]=ch;
    h->display.base.paused=true;
    if (e->rc) ch->last_rc=e->rc;
    return gf5_host_end(h,e);
}
void gf2_fifo_resume(struct gf2_chan *ch)
{
    struct gf5_host *h=gf5_host_current; assert(h && ch==h->ctx.ch);
    struct gf5_host_event *e=gf5_host_begin(h,F5_RESUME); e->p[0]=ch;
    if (e->rc) ch->last_rc=e->rc;
    else h->display.base.paused=false;
    /* A void resume may fail without changing ch.state to FAULT. */
    (void)gf5_host_end(h,e);
}
i32 gf4_service_suspend(struct gf4_ctx *c,u32 dl)
{
    struct gf5_host *h=gf5_host_current; assert(h && c==h->ctx.display);
    return gf5_host_stop(&h->ctx,dl);
}
i32 gf4_service_restart(struct gf4_ctx *c,u32 dl)
{
    struct gf5_host *h=gf5_host_current; assert(h && c==h->ctx.display);
    return gf5_host_restart(&h->ctx,dl);
}
i32 gf4_display_save(struct gf4_ctx *c,struct gf4_saved *out,u32 dl)
{
    struct gf5_host *h=gf5_host_current; assert(h && c==h->ctx.display && out);
    /* Avoid a second snapshot on the stack; write the real F4 type. */
    struct gf5_host_event *e=gf5_host_begin(h,F5_SAVE);
    e->p[0]=c; e->p[1]=out; e->a[0]=dl;
    if (!e->rc) {
        *out=(struct gf4_saved){.version=GF4_SAVED_VERSION,.size=sizeof(*out),
            .wanted_fifo=h->display.base.f.wanted_fifo,
            .wanted_graph=h->display.base.f.wanted_graph,
            .wanted_heads=h->display.base.f.wanted_heads};
        gf2_host_memcpy(out->head,h->display.state->head,sizeof(out->head));
        gf2_host_memcpy(out->hold,c->hold,sizeof(out->hold));
        c->saved_closed=true; c->accepting=false; h->display.scan_hook_open=false;
    }
    return gf5_host_end(h,e);
}
i32 gf4_display_restore(struct gf4_ctx *c,const struct gf4_saved *saved,u32 dl)
{
    struct gf5_host *h=gf5_host_current; assert(h && c==h->ctx.display && saved);
    struct gf5_host_event *e=gf5_host_begin(h,F5_RESTORE);
    e->p[0]=c; e->p[1]=saved; e->a[0]=dl;
    if (!e->rc) {
        gf2_host_memcpy(h->display.state->head,saved->head,sizeof(saved->head));
        /* display/save.c:267,328: cancel pending flips on restore. */
        for (u8 head=0;head<GF4_HEADS;++head) {
            if (saved->hold[head].pending)
                h->display.state->head[head].front=saved->hold[head].old_front;
            c->hold[head]=(struct gf4_flip_hold){0};
        }
        h->display.base.f.wanted_fifo=saved->wanted_fifo;
        h->display.base.f.wanted_graph=saved->wanted_graph;
        h->display.base.f.wanted_heads=saved->wanted_heads;
    }
    return gf5_host_end(h,e);
}
#endif
#endif
