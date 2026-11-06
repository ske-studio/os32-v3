/* H3: real tile/state/central reservation lifetime, no shadow loan registry.
 * Oracles: MIT nouveau c03c1a1 fb/base.c:201-202, fb/nv44.c:30-46,
 * gr/nv44.c:31-46, nouveau_bo.c:75-145,178-188; X nv_accel_common.c:94-100.
 * Explicit pitch/rounded-size pairs below are generator fixed points.
 * F2/display/clock remain the existing UNIT6 boundaries. Production startup
 * and leave ordering belong to H2; this host tests their tile boundaries. */
#define GF5_HOST_UNIT 6
#define GF5_HOST_REAL_VRAM
#define GF5_HOST_REAL_TILE
#include "gf5_fake.h"
#include "gf_guard_fake.h"
/* Exercise the exact GR transfer used by start/recover; unrelated GR entry
 * points are discarded by section GC, without replacement definitions. */
#include "../../hwdev/gf/gr.c"
#define ALIGN 65536U
#define SPACE (16U*ALIGN)
#define CAP 512U
static struct gf5_host h;
static struct gf4_state state;
static struct gf5_host_event events[CAP];
static struct gf4_host_event io_events[CAP];
static struct gf3_host_reg regs[GF5_TILE_COUNT*6U];
static struct gf_vram_reservation_node nodes[2];
static struct gf_vram_borrow view;
static struct gf_guard_host guard;
static u8 vram[SPACE];
static u8 arena[SPACE+65536U] __attribute__((aligned(GF5_HOST_ALLOC_ALIGN)));
static bool initialized;

int gf_guard_host_bind(struct gf_guard_host *g,struct gf3_host *base,struct gf4_host *display)
{
    if (!g || !base) return -1;
    g->base=base; g->display=display; return 0;
}
int gf_guard_host_fail_at(struct gf_guard_host *g,enum gf_guard_host_op op,
                         u32 nth,i32 rc,bool after_write)
{
    if (!g || (unsigned)op>=GF_GUARD_OP_COUNT || (nth && rc>=0) ||
        (after_write && op!=GF_GUARD_WRITE)) return -1;
    g->fail[op]=(struct gf_guard_host_failure){nth,rc,after_write}; return 0;
}
void gf_guard_host_trace_clear(struct gf_guard_host *g)
{
    assert(g); gf2_host_memset(g->calls,0,sizeof(g->calls));
    gf5_host_trace_clear(&h);
}
int gf_guard_host_tiles_seed(struct gf_guard_host *g,const struct gf5_tile_saved slots[GF5_TILE_COUNT])
{
    if (!g || !g->base || !slots || !g->base->ch.f ||
        g->base->ch.state!=GF2_OFF || h.ctx.accepting) return -1;
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        gf3_host_set(g->base,0,0x100600+i*0x10,slots[i].raw_addr);
        gf3_host_set(g->base,0,0x100604+i*0x10,slots[i].raw_limit);
        gf3_host_set(g->base,0,0x100608+i*0x10,slots[i].raw_pitch);
    }
    return 0;
}
static int read_io(void *owner,u32 bar,u32 off,u32 width,u32 *out)
{
    u32 n=++guard.calls[GF_GUARD_READ];
    if (guard.fail[GF_GUARD_READ].nth==n) return guard.fail[GF_GUARD_READ].rc;
    return gf4_host_read(owner,bar,off,width,out);
}
static int write_io(void *owner,u32 bar,u32 off,u32 width,u32 value)
{
    struct gf_guard_host_failure *f=&guard.fail[GF_GUARD_WRITE];
    bool fail=f->nth==++guard.calls[GF_GUARD_WRITE];
    if (fail && !f->after_write) return f->rc;
    i32 rc=gf4_host_write(owner,bar,off,width,value);
    return fail ? f->rc : rc;
}
static u32 reg(u32 off)
{
    u32 v=0; assert(!gf4_host_read(&h.display,0,off,4,&v)); return v;
}
static void observe(struct gf5_host *host,const struct gf5_host_event *e)
{
    if (e->rc) return;
    if (e->op==F5_CALC && e->a[0]==F5_CALL_READ)
        *(struct gf5_clock *)e->p[1]=(struct gf5_clock){300000,300000,200000};
    if (e->op==F5_CALC && e->a[0]==F5_CALL_CALC)
        *(struct gf5_plan *)e->p[2]=(struct gf5_plan){.actual=*(const struct gf5_clock *)e->p[1]};
    if (e->op==F5_VOLT && e->a[0]==F5_CALL_GET) *(u32 *)e->p[1]=1200000;
    /* Stop/restart must preserve both the resident node address and key. */
    if (e->op==F5_STOP || e->op==F5_RESTART) {
        struct gf5_tile_slot *s=&host->ctx.tile_slot[0];
        assert(s->state==GF_TILE_OWNED && s->loan[s->current].key.owner==nodes[0].value.owner);
        assert(gf_vram_release(host->ctx.ch,&nodes[0].value)==GF3_EBUSY);
        if (e->op==F5_RESTART) {
            assert(!copy_tiles(host->ctx.ch));
            assert(reg(0x400900)==(ALIGN|1U) && reg(0x400908)==1024);
        }
    }
}
static void stop(void)
{
    h.ctx.accepting=false; h.display.ctx.accepting=false;
    h.ctx.ch->state=GF2_OFF;
}
static void clean(void)
{
    gf2_host_memset(guard.fail,0,sizeof(guard.fail));
    gf2_host_memset(h.fail_rc,0,sizeof(h.fail_rc));
    gf2_host_memset(h.display.fail_rc,0,sizeof(h.display.fail_rc));
    gf_guard_host_trace_clear(&guard);
    if (h.ctx.image) gf5_state_free(&h.ctx,h.ctx.image);
    if (view.key.reservation_id) assert(!gf_vram_borrow_put(h.ctx.ch,&view));
    stop(); assert(!gf5_tiles_retire(&h.ctx));
    for (u32 i=0;i<2;++i) if (nodes[i].value.owner)
        assert(!gf_vram_release(h.ctx.ch,&nodes[i].value));
}
static void setup(void)
{
    if (initialized) clean();
    gf5_host_init(&h,&state); initialized=true;
    h.events=events; h.cevents=CAP; h.ctx.deadline=GF2_TIMEOUT_US;
    h.display.events=io_events; h.display.cevents=CAP;
    h.display.base.regs=regs; h.display.base.cregs=sizeof(regs)/sizeof(*regs);
    h.display.base.vram=vram; h.display.base.vram_bytes=SPACE;
    h.display.base.f.imem.vram_bytes=SPACE; h.ctx.ch->B=SPACE;
    h.ctx.ch->state=GF2_OFF; h.ctx.ch->f=0; h.ctx.accepting=false;
    guard=(struct gf_guard_host){0};
    assert(!gf_guard_host_bind(&guard,&h.display.base,&h.display));
    assert(!h.ctx.ch->f); /* bind does not enter or manufacture loans */
    h.ctx.ch->f=&h.display.base.f; /* isolated post-devinit boundary */
    h.ctx.ch->f->io.read=read_io; h.ctx.ch->f->io.write=write_io;
    assert(!gf_vram_registry_init(h.ctx.ch));
    gf5_host_arena(&h,arena,sizeof(arena));
    assert(!gf5_tiles_init(&h.ctx));
    h.ctx.ch->state=GF2_RUN; h.ctx.accepting=true;
    assert(!gf_vram_reserve(h.ctx.ch,&nodes[0],ALIGN,8U*ALIGN,&nodes[0]));
    assert(!gf_vram_reserve(h.ctx.ch,&nodes[1],9U*ALIGN,4U*ALIGN,&nodes[1]));
    h.display.base.reservations=&nodes[1];
    gf_guard_host_trace_clear(&guard);
}
static struct gf5_tile tile(u32 n)
{
    return (struct gf5_tile){.base=nodes[n].value.off,.bytes=ALIGN,.pitch=1024,
        .reservation_id=nodes[n].value.reservation_id,.generation=nodes[n].value.generation};
}
static void pitch_boundaries(void)
{
    static const struct {u32 pitch,bytes; bool valid;} cases[]={
        {0,0,false},{1,0,false},{1023,0,false},{1024,65536,true},
        {2048,131072,true},{8192,524288,true},{9216,0,false}};
    for (u32 i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        setup(); struct gf5_tile t=tile(0); t.pitch=cases[i].pitch; t.bytes=1;
        assert(gf5_tile_set(&h.ctx,0,&t)==(cases[i].valid ? 0 : GF5_EINVAL));
        if (cases[i].valid) assert(h.ctx.tile[0].bytes==cases[i].bytes);
        else assert(!guard.calls[GF_GUARD_WRITE] && !h.calls[F5_PAUSE]);
    }
    setup(); struct gf5_tile t=tile(0); t.bytes=U32_MAX;
    assert(gf5_tile_set(&h.ctx,0,&t)==GF5_EINVAL);
    t=tile(1); t.pitch=8192; /* rounded 512KiB exceeds its 256KiB reservation */
    assert(gf5_tile_set(&h.ctx,0,&t)==GF5_EINVAL);
    t=tile(0); t.base++; assert(gf5_tile_set(&h.ctx,0,&t)==GF5_EINVAL);
    assert(!h.calls[F5_PAUSE]);
}
static void slots_and_replacement(void)
{
    setup();
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        struct gf5_tile t=tile(i<8 ? 0 : 1);
        t.base+=(i<8 ? i : i-8)*ALIGN;
        u32 slot=99;
        assert(!gf5_tile_find(&h.ctx,&slot) && slot==i);
        assert(!gf5_tile_set(&h.ctx,i,&t));
    }
    u32 slot=99;
    assert(gf5_tile_find(&h.ctx,&slot)==GF5_EBUSY && slot==99);
    assert(!gf5_tile_clear(&h.ctx,5));
    assert(!gf5_tile_find(&h.ctx,&slot) && slot==5);
    struct gf5_tile invalid=tile(0);
    assert(gf5_tile_set(&h.ctx,GF5_TILE_COUNT,&invalid)==GF5_EINVAL);
    setup();
    struct gf5_tile a=tile(0), b=tile(1);
    assert(!gf5_tile_set(&h.ctx,0,&a));
    assert(!gf5_tile_set(&h.ctx,0,&b));
    assert(!gf_vram_release(h.ctx.ch,&nodes[0].value));
    assert(gf_vram_release(h.ctx.ch,&nodes[1].value)==GF3_EBUSY);
    assert(!gf5_tile_clear(&h.ctx,0));
    assert(!gf_vram_release(h.ctx.ch,&nodes[1].value));
}
static void init_failures(void)
{
    /* All 36 writes (including apply-then-error) and all 12 flushes. */
    for (u32 op=GF_GUARD_READ;op<=GF_GUARD_WRITE;++op)
    for (u32 after=0;after<=(op==GF_GUARD_WRITE);++after)
    for (u32 nth=1;nth<=(op==GF_GUARD_WRITE ? 36U : 12U);++nth) {
        setup(); clean();
        struct gf5_tile_saved seed[GF5_TILE_COUNT];
        for (u32 i=0;i<GF5_TILE_COUNT;++i)
            seed[i]=(struct gf5_tile_saved){GF_TILE_FREE,0x10001+i*ALIGN,0x1ffff+i*ALIGN,1024};
        assert(!gf_guard_host_tiles_seed(&guard,seed));
        gf_guard_host_trace_clear(&guard);
        assert(!gf_guard_host_fail_at(&guard,op,nth,GF2_ETIME,after));
        assert(gf5_tiles_init(&h.ctx)==GF5_ETIME && h.ctx.source_rc==GF2_ETIME);
        for (u32 i=0;i<GF5_TILE_COUNT;++i) assert(h.ctx.tile_slot[i].state==GF_TILE_UNKNOWN);
        assert(!h.ctx.accepting && h.ctx.phase==GF5_FAULT && h.ctx.ch->state==GF2_OFF);
        assert(!gf_guard_host_fail_at(&guard,op,0,0,false));
        gf_guard_host_trace_clear(&guard);
        assert(!gf5_tiles_init(&h.ctx));
        assert(guard.calls[GF_GUARD_WRITE]==36 && guard.calls[GF_GUARD_READ]==12);
        for (u32 i=0;i<GF5_TILE_COUNT;++i) {
            assert(h.ctx.tile_slot[i].state==GF_TILE_FREE);
            assert(!reg(0x100600+i*16) && !reg(0x100604+i*16) && !reg(0x100608+i*16));
        }
        assert(!h.calls[F5_PAUSE]);
        assert(!copy_tiles(h.ctx.ch));
        for (u32 i=0;i<GF5_TILE_COUNT;++i)
            assert(!reg(0x400900+i*16) && !reg(0x400904+i*16) && !reg(0x400908+i*16));
    }
}
static void lifetime_failures(void)
{
    /* Replacement/clear: each PFB/GR write, flush, pause, unpause, admission. */
    for (u32 clear=0;clear<2;++clear) for (u32 stage=0;stage<10;++stage)
    for (u32 after=0;after<=(stage<6);++after) {
        setup(); struct gf5_tile old=tile(0), next=tile(1);
        assert(!gf5_tile_set(&h.ctx,0,&old));
        struct gf5_tile_slot *s=&h.ctx.tile_slot[0];
        struct gf_vram_borrow *loan=&s->loan[s->current];
        gf_guard_host_trace_clear(&guard);
        if (stage<6) assert(!gf_guard_host_fail_at(&guard,GF_GUARD_WRITE,stage+1,GF2_ETIME,after));
        if (stage==6) assert(!gf_guard_host_fail_at(&guard,GF_GUARD_READ,1,GF2_ETIME,false));
        if (stage>=7) gf5_host_fail_at(&h,stage==7 ? F5_PAUSE : stage==8 ? F5_RESUME : F5_ADMIT,1,GF2_ETIME);
        assert((clear ? gf5_tile_clear(&h.ctx,0) : gf5_tile_set(&h.ctx,0,&next))==GF5_ETIME);
        assert(h.ctx.source_rc==GF2_ETIME && h.ctx.phase==GF5_FAULT && !h.ctx.accepting);
        assert(s->state==GF_TILE_UNKNOWN && loan->key.reservation_id==old.reservation_id);
        assert(gf_vram_release(h.ctx.ch,&nodes[0].value)==GF3_EBUSY);
        if (!clear) assert(gf_vram_release(h.ctx.ch,&nodes[1].value)==GF3_EBUSY);
        u32 slot=99; assert(!gf5_tile_find(&h.ctx,&slot) && slot==1);
        assert(gf5_tile_clear(&h.ctx,0)==GF5_ESTATE);
        /* Retire itself fails after a write; both loans must still be pinned. */
        gf2_host_memset(h.fail_rc,0,sizeof(h.fail_rc));
        gf2_host_memset(guard.fail,0,sizeof(guard.fail)); stop();
        gf_guard_host_trace_clear(&guard);
        assert(!gf_guard_host_fail_at(&guard,GF_GUARD_WRITE,6,GF2_ETIME,true));
        assert(gf5_tiles_retire(&h.ctx)==GF5_ETIME && s->state==GF_TILE_UNKNOWN);
        assert(gf_vram_release(h.ctx.ch,&nodes[0].value)==GF3_EBUSY);
        if (!clear) assert(gf_vram_release(h.ctx.ch,&nodes[1].value)==GF3_EBUSY);
        assert(!h.calls[F5_PAUSE] && !h.calls[F5_RESUME]);
        assert(!gf_guard_host_fail_at(&guard,GF_GUARD_WRITE,0,0,false));
        assert(!gf5_tiles_retire(&h.ctx) && s->state==GF_TILE_FREE);
        assert(!gf_vram_release(h.ctx.ch,&nodes[0].value));
        assert(!gf_vram_release(h.ctx.ch,&nodes[1].value));
    }
}
static void view_and_order(void)
{
    setup(); struct gf5_tile t=tile(0);
    assert(!gf5_tile_set(&h.ctx,11,&t)); /* last slot is usable */
    const u32 offsets[]={0x1006b4,0x1006b8,0x1006b0,0x1006b0,0x4009b8,0x4009b4,0x4009b0};
    const u32 values[]={2*ALIGN-1U,1024,ALIGN|1U,0,1024,2*ALIGN-1U,ALIGN|1U};
    for (u32 i=0;i<7;++i) {
        assert(io_events[i].op==(i==3 ? D_READ : D_WRITE));
        assert(io_events[i].a[1]==offsets[i] && io_events[i].a[2]==4);
        if (i!=3) assert(io_events[i].a[3]==values[i]);
    }
    assert(!gf_vram_borrow_get(h.ctx.ch,&view,&nodes[0].value,ALIGN,ALIGN,GF_BORROW_VIEW));
    gf_guard_host_trace_clear(&guard);
    assert(gf5_tile_clear(&h.ctx,11)==GF5_EBUSY && !h.calls[F5_PAUSE]);
    assert(gf5_tile_set(&h.ctx,11,&t)==GF5_EBUSY);
    assert(gf_vram_release(h.ctx.ch,&nodes[0].value)==GF3_EBUSY);
    assert(!gf_vram_borrow_put(h.ctx.ch,&view));
    assert(!gf5_tile_clear(&h.ctx,11));
    assert(!gf_vram_release(h.ctx.ch,&nodes[0].value));
}
static void roundtrip(void)
{
    setup(); struct gf5_tile t=tile(0);
    assert(!gf5_tile_set(&h.ctx,0,&t)); h.observe=observe;
    for (u32 pass=0;pass<3;++pass) {
        gf_guard_host_trace_clear(&guard);
        struct gf5_tile_slot *s=&h.ctx.tile_slot[0];
        struct gf_vram_borrow *old=&s->loan[s->current];
        struct gf5_image *image=0;
        assert(!gf5_state_save(&h.ctx,&image));
        assert(image && image->tile_saved[0].state==GF_TILE_OWNED);
        assert(image->tile_saved[0].raw_addr==(ALIGN|1U) && image->tile_saved[0].raw_limit==2*ALIGN-1U);
        assert(image->tile_saved[0].raw_pitch==1024);
        assert(gf5_tiles_retire(&h.ctx)==GF5_EBUSY); /* saved image must finish first */
        assert(s->loan[s->current].key.reservation_id==t.reservation_id && &s->loan[s->current]==old);
        assert(!gf5_state_restore(&h.ctx,image));
        assert(s->state==GF_TILE_OWNED && &s->loan[s->current]!=old && !old->key.reservation_id);
        assert(s->loan[s->current].key.owner==nodes[0].value.owner);
        assert(s->loan[s->current].key.generation==t.generation);
        assert(reg(0x100600)==(ALIGN|1U) && reg(0x400900)==(ALIGN|1U));
        assert(reg(0x100608)==1024 && reg(0x400908)==1024);
        assert(gf_vram_release(h.ctx.ch,&nodes[0].value)==GF3_EBUSY);
        gf5_state_free(&h.ctx,image);
    }
    /* UNKNOWN save and corrupted/reused saved identity fail before restart. */
    h.ctx.tile_slot[0].state=GF_TILE_UNKNOWN;
    struct gf5_image *out=(void *)&state;
    gf_guard_host_trace_clear(&guard);
    assert(gf5_state_save(&h.ctx,&out)==GF5_ESTATE && out==(void *)&state && !h.nevents);
    h.ctx.tile_slot[0].state=GF_TILE_OWNED;
    struct gf5_image *image=0; assert(!gf5_state_save(&h.ctx,&image));
    ++h.ctx.tile_slot[0].raw_pitch; gf_guard_host_trace_clear(&guard);
    assert(gf5_state_restore(&h.ctx,image)==GF5_ESTATE && !h.calls[F5_RESTART]);
    --h.ctx.tile_slot[0].raw_pitch;
    gf5_state_free(&h.ctx,image); stop(); assert(!gf5_tiles_retire(&h.ctx));
    assert(!gf_vram_release(h.ctx.ch,&nodes[0].value));
    assert(!reg(0x100600) && !reg(0x100604) && !reg(0x100608));
    setup(); /* a new session starts with FREE slots and fresh reservation keys */
    assert(h.ctx.tile_slot[0].state==GF_TILE_FREE && !h.ctx.tile_live[0]);
}
void _start(void)
{
    pitch_boundaries(); slots_and_replacement(); init_failures(); lifetime_failures(); view_and_order(); roundtrip();
    clean(); gf2_host_exit(0);
}
