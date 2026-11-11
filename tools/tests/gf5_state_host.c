/* B6: retained reservations, transactional ownership and resume order.
 * Oracle: GF_FEAT5_DESIGN section 7/final addendum and F4 G3;
 * nouveau c03c1a1 nouveau_drm.c:944-991,1007-1028, dispnv04/disp.c:57-159.
 * The constants below describe synthetic contents, not hardware measurements.
 * U5: GF_GUARD_THERM_DESIGN sections 6/9, live heat survives snapshots.
 * PLL fixtures follow MIT nouveau c03c1a1 clk/nv40.c:39-78 (27MHz,
 * M/N, optional second stage, then P). Recreate from the documented
 * factors below; CI needs no reference checkout or measured GPU values.
 * No refs tree, libc, allocation service or local stub. */
#define GF5_HOST_UNIT 6
#include "gf5_fake.h"

#define MIB (1024U*1024U)
/* One cursor plus an extent larger than the old hypothetical 8MiB ceiling. */
#define LARGE_BYTES (9U*MIB)
#define CURSOR_BYTES GF4_CURSOR_BYTES
#define VRAM_BYTES (LARGE_BYTES+CURSOR_BYTES)
#define ARENA_BYTES (2U*VRAM_BYTES+64U*1024U) /* metadata and allocator alignment */
#define TRACE_CAP 512U /* two enumeration passes, 12 tiles and finite callbacks */
#define BOOT_CORE_KHZ 300000U
#define BOOT_MEM_KHZ 200000U
#define BOOT_UV 1200000U
#define FAN_CSTATE 40U
#define SAVED_DUTY 43U
#define LOWER_ERROR GF1_ESTOP
static struct gf5_host h;
static struct gf4_state display;
static struct gf5_host_event trace[TRACE_CAP];
static struct gf4_host_event dtrace[TRACE_CAP];
static u8 arena[ARENA_BYTES] __attribute__((aligned(GF5_HOST_ALLOC_ALIGN)));
static u8 vram[VRAM_BYTES];
static struct gf_vram_reservation_node nodes[2];
static struct gf5_fan_trip trips[2];
static u32 expected_deadline, current_uv;
static bool check_deadline, reenter, bad_epoch, nested_error, stop_off_error;
static struct gf5_clock captured_clock;
static struct gf5_clock_raw captured_raw;
static struct gf5_plan captured_plan;

static u8 pattern(u32 i) { return (u8)(i^(i>>8)^0xa5U); }
static void observe(struct gf5_host *host,const struct gf5_host_event *e)
{
    struct gf5 *g=&host->ctx;
    if (check_deadline) {
        if (e->op==F5_CLOCK64) {
            /* GF_FEAT4_DESIGN.md:380: one deadline per outer operation.
             * The callback observes the clock result before state.c assigns
             * it. Rejected operations and failed clocks retain the old one,
             * including when the caller subsequently frees the image. */
            assert(host->calls[F5_CLOCK64]==1);
            assert(g->deadline==expected_deadline);
            if (!e->rc) expected_deadline=(u32)host->now+GF2_TIMEOUT_US;
        } else assert(g->deadline==expected_deadline);
    }
    if (e->op==F5_STOP) {
        assert(g->image && g->image->complete);
        assert(!g->accepting && g->phase==GF5_SAVING);
        if (stop_off_error) host->display.base.ch.state=GF2_OFF;
    }
    if (e->op==F5_RESTART && bad_epoch) host->display.base.ch.epoch=g->image->saved_epoch;
    if (e->rc) {
        if (nested_error && e->op==F5_RAM) {
            g->source_rc=LOWER_ERROR; g->last_rc=e->rc;
        }
        return;
    }
    if (e->op==F5_CALC && e->a[0]==F5_CALL_READ)
        *(struct gf5_clock *)e->p[1]=captured_clock;
    if (e->op==F5_CALC && e->a[0]==F5_CALL_CALC) {
        const struct gf5_clock *clock=e->p[1];
        assert(clock==&g->image->clock);
        assert(!gf2_host_memcmp(clock,&captured_clock,sizeof(*clock)));
        *(struct gf5_plan *)e->p[2]=captured_plan;
    }
    if (e->op==F5_RAM) {
        assert(!gf2_host_memcmp(e->p[1],&captured_plan,sizeof(captured_plan)));
        assert(!host->calls[F5_FAN] && !host->calls[F5_VOLT] && !host->calls[F5_PROG]);
    }
    if (e->op==F5_CALC && e->a[0]==F5_CALL_RAW_SAVE)
        *(struct gf5_clock_raw *)e->p[1]=captured_raw;
    if (e->op==F5_PROG && e->a[0]==F5_CALL_RAW_RESTORE) {
        const struct gf5_clock_raw *raw=e->p[1];
        assert(raw==&g->image->clock_raw);
        assert(!gf2_host_memcmp(raw,&captured_raw,sizeof(*raw)));
    }
    if (e->op==F5_FAN && e->a[0]==F5_CALL_GET) *(u32 *)e->p[1]=SAVED_DUTY;
    if (e->op==F5_VOLT && e->a[0]==F5_CALL_GET) *(u32 *)e->p[1]=current_uv;
    if (e->op==F5_RAM || e->op==F5_PROG ||
        (e->op==F5_FAN && e->a[0]!=F5_CALL_GET) ||
        (e->op==F5_COPY && e->a[0]) || e->op==F5_RESTORE) {
        assert(g->phase==GF5_RESTORING && g->busy && !g->accepting);
        assert(host->calls[F5_CLOCK64]==1);
    }
    if (e->op==F5_FAN && e->a[0]==F5_CALL_THERM_RESUME) {
        const struct gf5_fan_saved *saved=e->p[1];
        assert(saved==&g->image->fan && saved->duty==SAVED_DUTY);
        assert(saved->trip!=trips && saved->trip[0].duty==FAN_CSTATE);
        assert(saved->sensor_setup==0xffU && saved->active[2]);
        /* B3 implements policy/timer restoration. B6 must pass the complete
         * saved object without stealing the resident trip array. */
        assert(g->fan.trip==trips);
    }
    if (reenter && e->op==F5_DRAIN) {
        struct gf5_image *unchanged=0;
        assert(gf5_state_save(g,&unchanged)==GF5_EBUSY && !unchanged);
        assert(gf5_state_restore(g,g->image)==GF5_EBUSY);
    }
}
static void reset(void)
{
    gf2_host_memset(&display,0,sizeof(display));
    gf5_host_init(&h,&display);
    h.events=trace; h.cevents=TRACE_CAP;
    h.display.events=dtrace; h.display.cevents=TRACE_CAP;
    h.observe=observe;
    gf5_host_arena(&h,arena,sizeof(arena));
    h.display.base.vram=vram; h.display.base.vram_bytes=sizeof(vram);
    for (u32 i=0;i<sizeof(vram);++i) vram[i]=pattern(i);
    /* Deliberately reverse node order: NEXT sorts IDs, not list addresses. */
    nodes[0]=(struct gf_vram_reservation_node){
        .value={&nodes[0],LARGE_BYTES,CURSOR_BYTES,2,7},.next=&nodes[1]};
    nodes[1]=(struct gf_vram_reservation_node){
        .value={&nodes[1],0,LARGE_BYTES,1,4}};
    h.display.base.reservations=nodes;
    trips[0]=(struct gf5_fan_trip){50,3,FAN_CSTATE};
    trips[1]=(struct gf5_fan_trip){80,5,100};
    h.ctx.fan=(struct gf5_fan_saved){.mode=GF5_FAN_MANUAL,.method=GF5_FAN_PWM,
        .kind=GF5_FAN_TRIP,.cstate=FAN_CSTATE,.duty=SAVED_DUTY,
        .ntrip=2,.trip=trips,.sensor_setup=0xffU,.active={false,false,true,false}};
    h.ctx.tile_live[0]=true;
    h.ctx.tile[0]=(struct gf5_tile){.base=0,.bytes=LARGE_BYTES,.pitch=1024,
        .reservation_id=1,.generation=4};
    h.ctx.tile_slot[0]=(struct gf5_tile_slot){.state=GF_TILE_OWNED,
        .raw_addr=1,.raw_limit=LARGE_BYTES-1U,.raw_pitch=1024,.current=0,
        .loan={{.key=nodes[1].value,.off=0,.bytes=LARGE_BYTES,.kind=GF_BORROW_TILE}}};
    display.head[0].enabled=true; display.head[0].dpms=GF4_ON;
    display.head[0].cursor=nodes[0].value;
    display.head[0].cursor_visible=true;
    display.head[0].palette[17]=91;
    h.display.base.f.wanted_heads=1;
    h.now=((u64)3<<32)+(U32_MAX-GF2_TIMEOUT_US/2U);
    current_uv=BOOT_UV; check_deadline=false; reenter=false;
    bad_epoch=nested_error=stop_off_error=false;
    captured_clock=(struct gf5_clock){BOOT_CORE_KHZ,BOOT_CORE_KHZ,BOOT_MEM_KHZ};
    /* Distinct synthetic solver output verifies calc -> RAM plan forwarding. */
    captured_plan=(struct gf5_plan){.mc=0x1234U,.mn=0x5678U,.actual=captured_clock};
    captured_raw=(struct gf5_clock_raw){0xc002001cU,0x14032809U,0xc000001cU,0x3c0bc003U};
}
static struct gf5_image *save(void)
{
    struct gf5_image *image=0;
    expected_deadline=h.ctx.deadline; check_deadline=true;
    assert(gf5_state_save(&h.ctx,&image)==0 && image);
    assert(h.ctx.phase==GF5_SAVED && !h.ctx.busy && !h.ctx.accepting);
    assert(h.ctx.image==image && image->complete && h.live_allocs==1);
    assert(image->clock_available || h.calls[F5_CALC]==2);
    assert(image->nextent==2 && image->bytes>8U*MIB);
    assert(image->bytes==sizeof(*image)+sizeof(struct gf5_extent)*2U+
           sizeof(trips)+VRAM_BYTES);
    assert(image->extent[0].key.reservation_id==1 && image->extent[1].key.reservation_id==2);
    assert(image->fan.trip!=trips && image->fan.trip[1].temp==80);
    assert(h.calls[F5_COPY]==2 && h.calls[F5_STOP]==1);
    assert(image->extent[1].host_copy[CURSOR_BYTES-1]==pattern(VRAM_BYTES-1));
    return image;
}
static void begin_restore(void)
{
    /* Advance from the configured timeout so restore needs a fresh deadline. */
    h.now+=GF2_TIMEOUT_US/2U;
    gf5_host_trace_clear(&h);
}
static u32 first(enum gf5_host_op op)
{
    for (u32 i=0;i<h.nevents;++i) if (trace[i].op==op) return i;
    assert(false); return 0;
}
static u32 call(enum gf5_host_op op,u32 kind)
{
    for (u32 i=0;i<h.nevents;++i) if (trace[i].op==op && trace[i].a[0]==kind) return i;
    assert(false); return 0;
}
static void roundtrip(void)
{
    reset();
    for (u32 pass=0;pass<3;++pass) {
        struct gf5_image *image=save();
        assert(first(F5_DRAIN)<first(F5_SAVE) && first(F5_SAVE)<first(F5_COPY));
        assert(first(F5_COPY)<first(F5_STOP));
        u32 epoch=image->saved_epoch;
        gf2_host_memset(vram,0,sizeof(vram));
        display.head[0].palette[17]=0; display.head[0].cursor_visible=false;
        trips[0].duty=99;
        begin_restore();
        /* Alternate rising/falling voltage to exercise both sides of core. */
        current_uv=pass&1U ? BOOT_UV+100000U : BOOT_UV-100000U;
        assert(gf5_state_restore(&h.ctx,image)==0);
        assert(h.ctx.phase==GF5_RUN && !h.ctx.busy && h.ctx.accepting);
        assert(!h.display.ctx.saved_closed && h.display.scan_hook_open);
        assert(h.display.base.ch.epoch==epoch+1U); /* start alone advances */
        assert(h.ctx.image==image && h.live_allocs==1);
        for (u32 i=0;i<sizeof(vram);++i) assert(vram[i]==pattern(i));
        assert(display.head[0].palette[17]==91 && display.head[0].cursor_visible);
        assert(first(F5_MATCH)<first(F5_RESTART));
        assert(first(F5_RESTART)<call(F5_FAN,F5_CALL_CSTATE));
        assert(h.calls[F5_CALC]==1 && h.calls[F5_RAM]==1);
        assert(first(F5_CALC)<first(F5_RAM) && first(F5_RAM)<first(F5_FAN));
        u32 volt=call(F5_VOLT,F5_CALL_SET), core=first(F5_PROG);
        assert((pass&1U) ? core<volt : volt<core);
        assert(call(F5_FAN,F5_CALL_THERM_RESUME)<first(F5_COPY));
        assert(first(F5_COPY)<first(F5_RESTORE) && first(F5_RESTORE)<first(F5_ADMIT));
        assert(h.calls[F5_PROG]==1+GF5_TILE_COUNT && h.calls[F5_ADMIT]==1);
        assert(h.calls[F5_PAUSE]==0 && h.calls[F5_RESUME]==0); /* B2/B5 own these */
        assert(h.calls[F5_ALLOC]==0 && h.calls[F5_NEXT]==0);
        assert(nodes[0].value.generation==7 && nodes[1].value.generation==4);
        gf5_state_free(&h.ctx,image);
        assert(!h.live_allocs && !h.ctx.image);
        trips[0].duty=FAN_CSTATE;
        gf5_host_trace_clear(&h);
    }
}
static void diagnostic_conflict(void)
{
    /* F4 save.c:138 rejects an active diagnostic snapshot; F5 section 7
     * treats this as a duplicate request before starting the transaction. */
    reset();
    struct gf4_saved diagnostic={0}, before=diagnostic;
    struct gf5_image retained={.version=GF5_IMAGE_VERSION,.complete=true};
    struct gf5_image previous=retained, *out=&retained;
    bool scan_open=h.display.scan_hook_open;
    h.ctx.image=&retained;
    h.display.ctx.diagnostic_saved=&diagnostic;
    assert(gf5_state_save(&h.ctx,&out)==GF5_EBUSY);
    assert(h.ctx.last_rc==GF5_EBUSY && h.ctx.source_rc==GF4_EBUSY);
    assert(h.ctx.phase==GF5_RUN && !h.ctx.busy && h.ctx.accepting);
    assert(h.display.ctx.accepting && h.display.scan_hook_open==scan_open);
    assert(h.ctx.image==&retained && out==&retained);
    assert(!gf2_host_memcmp(&retained,&previous,sizeof(retained)));
    assert(h.display.ctx.diagnostic_saved==&diagnostic);
    assert(!gf2_host_memcmp(&diagnostic,&before,sizeof(diagnostic)));
    assert(!h.nevents && !h.display.nevents && !h.live_allocs);
    /* Closing the diagnostic session permits the same save to complete. */
    h.display.ctx.diagnostic_saved=0;
    struct gf5_image *image=save();
    begin_restore();
    assert(!gf5_state_restore(&h.ctx,image));
    assert(h.ctx.phase==GF5_RUN && h.ctx.accepting);
    gf5_state_free(&h.ctx,image);
    assert(!h.ctx.image && !h.live_allocs);
}
static void preallocation_failure(void)
{
    reset();
    struct gf5_image *out=(void *)&display;
    gf5_host_fail_at(&h,F5_ALLOC,1,GF1_ENOSPC);
    assert(gf5_state_save(&h.ctx,&out)==GF5_ENOMEM && out==(void *)&display);
    assert(h.ctx.source_rc==GF1_ENOSPC && h.ctx.phase==GF5_RUN && h.ctx.accepting);
    assert(!h.ctx.image && !h.live_allocs && !h.calls[F5_DRAIN] && !h.calls[F5_STOP]);
    reset();
    gf5_host_fail_at(&h,F5_ALLOC,1,GF1_ENOSPC);
    gf5_host_fail_at(&h,F5_ADMIT,2,GF4_ESTATE);
    assert(gf5_state_save(&h.ctx,&out)==GF5_ENOMEM && out==(void *)&display);
    assert(h.ctx.source_rc==GF1_ENOSPC && h.ctx.phase==GF5_FAULT && !h.ctx.accepting);
    assert(!h.live_allocs && !h.calls[F5_STOP]);
    reset(); nodes[0].value.off++;
    assert(gf5_state_save(&h.ctx,&out)==GF5_ERANGE && out==(void *)&display);
    assert(!h.calls[F5_ALLOC] && !h.calls[F5_SAVE] && !h.calls[F5_STOP]);
    reset(); nodes[1].value.bytes=U32_MAX-3U;
    assert(gf5_state_save(&h.ctx,&out)==GF5_ENOMEM);
    assert(!h.calls[F5_ALLOC] && h.ctx.phase==GF5_RUN);
    reset(); gf5_host_fail_at(&h,F5_NEXT,1,GF3_ESTATE);
    assert(gf5_state_save(&h.ctx,&out)==GF5_ESTATE);
    assert(out==(void *)&display && !h.live_allocs && !h.calls[F5_STOP]);
}
static void save_failures(void)
{
    static const enum gf5_host_op failures[]={F5_DRAIN,F5_SAVE,F5_COPY,F5_STOP};
    for (u32 i=0;i<sizeof(failures)/sizeof(failures[0]);++i) {
        reset(); struct gf5_image *image=0;
        enum gf5_host_op op=failures[i];
        gf5_host_fail_at(&h,op,1,LOWER_ERROR);
        stop_off_error=op==F5_STOP;
        assert(gf5_state_save(&h.ctx,&image)==GF5_EIO && image);
        assert(h.ctx.source_rc==LOWER_ERROR && h.ctx.image==image && h.live_allocs==1);
        assert(h.ctx.phase==GF5_FAULT && !h.ctx.busy && !h.ctx.accepting);
        assert(image->complete==(op==F5_STOP));
        if (op!=F5_STOP) assert(!h.calls[F5_STOP]);
        if (op==F5_DRAIN) assert(!h.calls[F5_SAVE] && !h.calls[F5_COPY]);
        if (op==F5_SAVE) assert(!h.calls[F5_COPY]);
        if (op==F5_COPY) assert(h.calls[F5_COPY]==1);
        assert(gf5_state_restore(&h.ctx,image)==GF5_ESTATE);
        gf5_state_free(&h.ctx,image);
        assert(!h.ctx.image && !h.live_allocs);
    }
    /* Successful allocation but second enumeration fails before ownership
     * publication: free privately and preserve the caller's output. */
    reset(); struct gf5_image *out=(void *)&display;
    gf5_host_fail_at(&h,F5_NEXT,4,GF3_ESTATE);
    assert(gf5_state_save(&h.ctx,&out)==GF5_ESTATE && out==(void *)&display);
    assert(!h.live_allocs && !h.ctx.image && h.ctx.phase==GF5_RUN);
}
static void restore_failures(void)
{
    static const struct { enum gf5_host_op op; u32 nth; } failures[]={
        {F5_MATCH,1},{F5_RESTART,1},{F5_CALC,1},{F5_RAM,1},
        {F5_FAN,1},{F5_VOLT,1},{F5_VOLT,2},{F5_PROG,1},
        {F5_FAN,3},{F5_PROG,2},{F5_COPY,2},{F5_RESTORE,1},{F5_ADMIT,1}
    };
    for (u32 i=0;i<sizeof(failures)/sizeof(failures[0]);++i) {
        reset(); struct gf5_image *image=save(); begin_restore();
        enum gf5_host_op op=failures[i].op;
        nested_error=op==F5_RAM;
        if (op==F5_CALC || op==F5_RAM || op==F5_PROG) current_uv=BOOT_UV+100000U;
        gf5_host_fail_at(&h,op,failures[i].nth,LOWER_ERROR);
        assert(gf5_state_restore(&h.ctx,image)==GF5_EIO);
        assert(h.ctx.source_rc==LOWER_ERROR && h.ctx.phase==GF5_FAULT);
        assert(h.ctx.image==image && image->complete && h.live_allocs==1);
        assert(!h.ctx.accepting && !h.ctx.busy && !h.calls[F5_FREE]);
        if (op!=F5_ADMIT) assert(!h.calls[F5_ADMIT]);
        if (op!=F5_ADMIT && op!=F5_RESTORE) assert(!h.calls[F5_RESTORE]);
        if (op!=F5_COPY && op!=F5_RESTORE && op!=F5_ADMIT) assert(!h.calls[F5_COPY]);
        if (op==F5_RAM) assert(h.calls[F5_FAN]==1 && h.calls[F5_VOLT]==2 && !h.calls[F5_PROG]);
        gf5_state_free(&h.ctx,image); assert(!h.live_allocs && !h.ctx.image);
    }

    /* Lower failures do not replace a successful core result or suppress fan. */
    for (u32 primary=0;primary<2;++primary) {
        reset(); struct gf5_image *image=save(); begin_restore();
        current_uv=BOOT_UV+100000U;
        if (primary) gf5_host_fail_at(&h,F5_PROG,1,GF5_ETIME);
        gf5_host_fail_at(&h,F5_VOLT,2,LOWER_ERROR);
        gf5_host_fail_at(&h,F5_FAN,2,LOWER_ERROR);
        assert(gf5_state_restore(&h.ctx,image)==(primary ? GF5_ETIME : 0));
        assert(h.calls[F5_FAN]>=2 && h.calls[F5_VOLT]==2);
        if (primary) assert(h.ctx.source_rc==GF5_ETIME);
        gf5_state_free(&h.ctx,image); assert(!h.live_allocs);
    }

}
static void identities_and_guards(void)
{
    reset(); struct gf5_image *image=save(); begin_restore();
    ++nodes[0].value.generation;
    assert(gf5_state_restore(&h.ctx,image)==GF5_ESTATE);
    assert(!h.calls[F5_RESTART] && !h.calls[F5_COPY] && h.ctx.image==image);
    gf5_state_free(&h.ctx,image);
    reset(); image=save(); begin_restore(); nodes[1].value.off+=4;
    assert(gf5_state_restore(&h.ctx,image)==GF5_ESTATE && h.ctx.source_rc==GF3_ERANGE);
    assert(!h.calls[F5_RESTART]); gf5_state_free(&h.ctx,image);
    reset(); image=save(); begin_restore(); bad_epoch=true;
    assert(gf5_state_restore(&h.ctx,image)==GF5_ESTATE && !h.calls[F5_RAM]);
    gf5_state_free(&h.ctx,image);
    reset(); image=save(); image->complete=false; begin_restore();
    assert(gf5_state_restore(&h.ctx,image)==GF5_ESTATE && !h.nevents);
    image->complete=true; image->version++;
    assert(gf5_state_restore(&h.ctx,image)==GF5_ESTATE && !h.nevents);
    gf5_state_free(&h.ctx,image);
    reset(); gf5_host_fail_at(&h,F5_CLOCK64,1,GF2_EIO); image=0;
    assert(gf5_state_save(&h.ctx,&image)==GF5_EIO && !image);
    assert(h.ctx.phase==GF5_RUN && !h.calls[F5_ALLOC] && h.ctx.source_rc==GF2_EIO);
    reset(); image=save(); begin_restore();
    gf5_host_fail_at(&h,F5_CLOCK64,1,GF2_EIO);
    assert(gf5_state_restore(&h.ctx,image)==GF5_EIO && h.ctx.phase==GF5_SAVED);
    assert(!h.calls[F5_RESTART] && h.live_allocs==1); gf5_state_free(&h.ctx,image);
    reset(); reenter=true; image=save(); gf5_state_free(&h.ctx,image);
    reset(); h.ctx.busy=true; image=0;
    assert(gf5_state_save(&h.ctx,&image)==GF5_EBUSY && !h.nevents && !image);
    h.ctx.busy=false;
    assert(gf5_state_restore(&h.ctx,(void *)&display)==GF5_ESTATE && !h.nevents);
    gf5_state_free(&h.ctx,0);
}

static void clock_equal(struct gf5_clock a, struct gf5_clock b)
{
    assert(a.core_khz==b.core_khz && a.shader_khz==b.shader_khz && a.mem_khz==b.mem_khz);
}
static void heat_equal(struct gf5_heat_state expected)
{
    const struct gf5_heat_state *live=&h.ctx.heat;
    assert(live->down_active==expected.down_active);
    assert(live->critical_paused==expected.critical_paused);
    assert(live->cool_samples==expected.cool_samples && live->epoch==expected.epoch);
    clock_equal(live->entry_clock,expected.entry_clock);
    clock_equal(live->ceiling,expected.ceiling);
}
static struct gf5_heat_state live_heat(void)
{
    return (struct gf5_heat_state){.down_active=true,.cool_samples=2,
        .epoch=h.display.base.ch.epoch,.entry_clock={400000,400000,250000},
        .ceiling=captured_clock};
}
static void heat_image_retained(struct gf5_image *image)
{
    assert(h.ctx.image==image && image->complete && h.live_allocs==1);
    clock_equal(image->clock,captured_clock);
    assert(!h.ctx.busy && !h.ctx.accepting && !h.calls[F5_FREE]);
}
static void heat_no_program(void)
{
    assert(!h.calls[F5_RAM] && !h.calls[F5_PROG] && !h.calls[F5_VOLT]);
    assert(!h.calls[F5_FAN] && !h.calls[F5_COPY] && !h.calls[F5_RESTORE]);
    assert(!h.calls[F5_ADMIT] && !h.calls[F5_PAUSE] && !h.calls[F5_RESUME]);
    struct gf5_host_activity a=gf5_host_activity(&h);
    assert(!a.reads && !a.writes && !a.pauses && !a.resumes);
}
static void heat_saved_clock_limit(void)
{
    /* An image made before throttling must not undo a newer, lower ceiling.
     * Isolate each domain and the shader==0 (shared core) interpretation. */
    for (u32 domain=0;domain<4;++domain) {
        reset();
        if (domain==3) captured_clock.shader_khz=0;
        struct gf5_image *image=save();
        struct gf5_heat_state live=live_heat();
        if (domain==0) --live.ceiling.core_khz;
        else if (domain==2) --live.ceiling.mem_khz;
        else live.ceiling.shader_khz=BOOT_CORE_KHZ-1U;
        h.ctx.heat=live; begin_restore();
        assert(gf5_state_restore(&h.ctx,image)==GF5_EBUSY);
        assert(h.ctx.last_rc==GF5_EBUSY && h.ctx.source_rc==GF5_EBUSY);
        assert(h.ctx.phase==GF5_SAVED && !h.nevents && !h.calls[F5_RESTART]);
        heat_no_program(); heat_image_retained(image); heat_equal(live);
        gf5_state_free(&h.ctx,image);
        assert(!h.ctx.image && !h.live_allocs); heat_equal(live);
    }
}
static void heat_roundtrip(void)
{
    for (u32 higher=0;higher<2;++higher) {
        reset();
        h.ctx.heat=live_heat();
        struct gf5_heat_state before=h.ctx.heat;
        struct gf5_image *image=save(); heat_equal(before);
        /* A new live policy arrived while SAVED. No image may restore the
         * old entry ceiling, cooling count, flags or diagnostic rc. */
        struct gf5_heat_state live=live_heat();
        live.entry_clock=(struct gf5_clock){350000,350000,220000};
        live.cool_samples=1;
        if (higher) {
            ++live.ceiling.core_khz; ++live.ceiling.shader_khz; ++live.ceiling.mem_khz;
        }
        h.ctx.heat=live; begin_restore();
        assert(gf5_state_restore(&h.ctx,image)==0);
        assert(h.ctx.phase==GF5_RUN && h.ctx.accepting && !h.ctx.busy);
        assert(h.calls[F5_RAM]==1 && h.calls[F5_CALC]==1 && h.calls[F5_PROG]==1+GF5_TILE_COUNT);
        assert(h.calls[F5_RESTORE]==1 && h.calls[F5_ADMIT]==1);
        assert(h.display.base.ch.epoch==image->saved_epoch+1U);
        live.epoch=h.display.base.ch.epoch; heat_equal(live);
        gf5_state_free(&h.ctx,image);
        assert(!h.ctx.image && !h.live_allocs); heat_equal(live);
    }
}
static void heat_closed_operations(void)
{
    for (u32 stop=0;stop<2;++stop) {
        reset(); struct gf5_heat_state live=live_heat();
        live.critical_paused=!stop; h.ctx.stop->state=stop;
        h.ctx.heat=live;
        struct gf5_image *out=(void *)&display;
        assert(gf5_state_save(&h.ctx,&out)==GF5_ESTATE && out==(void *)&display);
        assert(!h.nevents && !h.live_allocs && !h.ctx.image && h.ctx.phase==GF5_RUN);
        heat_equal(live);
        reset(); struct gf5_image *image=save();
        live.epoch=h.display.base.ch.epoch; h.ctx.heat=live; h.ctx.stop->state=stop; begin_restore();
        assert(gf5_state_restore(&h.ctx,image)==GF5_ESTATE);
        assert(!h.nevents && h.ctx.phase==GF5_SAVED);
        heat_no_program(); heat_image_retained(image); heat_equal(live);
        gf5_state_free(&h.ctx,image);
        assert(!h.live_allocs && !h.ctx.image); heat_equal(live);
    }
}
/* nouveau volt/base.c:241-251 and therm/fan.c:246-255: unknown
 * optional state does not prevent the mandatory display/VRAM round trip. */
static void optional_capture(void)
{
    static const enum gf5_host_op missing[]={F5_VOLT,F5_FAN,F5_CALC};
    for (u32 i=0;i<3;++i) {
        reset();
        gf5_host_fail_at(&h,missing[i],1,GF5_EINVAL);
        struct gf5_image *image=0;
        assert(gf5_state_save(&h.ctx,&image)==0 && image && image->complete);
        assert(image->volt_available==(i!=0));
        assert(image->fan_available==(i!=1));
        assert(image->clock_available==(i!=2));
        if (i==1) assert(!image->fan.trip && !image->fan.ntrip);
        begin_restore();
        assert(gf5_state_restore(&h.ctx,image)==0 && h.ctx.accepting);
        assert(h.calls[F5_COPY]==2 && h.calls[F5_RESTORE]==1);
        if (i==0) assert(!h.calls[F5_VOLT]);
        if (i==1) assert(!h.calls[F5_FAN]);
        if (i==2) assert(!h.calls[F5_CALC] && !h.calls[F5_RAM] &&
                          h.calls[F5_PROG]==GF5_TILE_COUNT);
        gf5_state_free(&h.ctx,image);
    }
    reset();
    /* A successful read of a source with frequency zero is available. */
    captured_clock=(struct gf5_clock){0,0,BOOT_MEM_KHZ};
    struct gf5_image *image=save();
    assert(image->clock_available && !image->clock.core_khz);
    gf5_state_free(&h.ctx,image);
    reset();
    gf5_host_fail_at(&h,F5_CALC,2,GF5_EIO); /* raw read fails after frequency read */
    image=save();
    assert(!image->clock_available && image->volt_available && image->fan_available);
    gf5_state_free(&h.ctx,image);
}
/* Boot-target is a memory operation: old A epoch is intentionally different.
 * Normal save/restore still own all device and reservation transitions. */
static void boot_target(void)
{
    reset();
    struct gf5_image *a=save(), *b=0;
    begin_restore();
    assert(!gf5_state_restore(&h.ctx,a));
    captured_clock.core_khz/=2;
    captured_clock.mem_khz/=2;
    captured_raw.nc^=1U; captured_raw.nn^=2U;
    captured_raw.sc^=4U; captured_raw.mux^=8U;
    h.ctx.fan.cstate=70; trips[0].duty=70;
    check_deadline=false;
    assert(!gf5_state_save(&h.ctx,&b));
    assert(a!=b && a->saved_epoch!=b->saved_epoch && h.live_allocs==2);
    struct gf5_image expected=*b;
    expected.clock=a->clock; expected.clock_raw=a->clock_raw;
    expected.requested_uv=a->requested_uv; expected.vid=a->vid;
    expected.fan=a->fan; expected.fan.trip=b->fan.trip;
    expected.fan_available=a->fan_available;
    expected.volt_available=a->volt_available;
    expected.clock_available=a->clock_available;
    struct gf5_image original_a=*a;
    struct gf5_fan_saved live_fan=h.ctx.fan;
    struct gf5_heat_state live_heat=h.ctx.heat;
    gf5_host_trace_clear(&h);
    assert(!gf5_state_boot_target(&h.ctx,b,a));
    assert(!h.nevents && !gf2_host_memcmp(b,&expected,sizeof(expected)));
    assert(!gf2_host_memcmp(a,&original_a,sizeof(*a)));
    assert(!gf2_host_memcmp(&h.ctx.fan,&live_fan,sizeof(live_fan)));
    assert(!gf2_host_memcmp(&h.ctx.heat,&live_heat,sizeof(live_heat)));
    assert(b->fan.trip!=a->fan.trip && b->fan.trip[0].duty==FAN_CSTATE);
    a->fan.trip[0].duty=99;
    assert(b->fan.trip[0].duty==FAN_CSTATE);
    a->fan.trip[0].duty=FAN_CSTATE;
    struct gf5_image before=*b;
    /* Each rejection leaves every destination byte intact. */
    for (u32 i=0;i<10;++i) {
        i32 want=GF5_ESTATE;
        switch (i) {
        case 0: h.ctx.busy=true; want=GF5_EBUSY; break;
        case 1: h.ctx.phase=GF5_SAVING; want=GF5_EBUSY; break;
        case 2: h.ctx.phase=GF5_RESTORING; want=GF5_EBUSY; break;
        case 3: h.ctx.phase=GF5_RUN; break;
        case 4: a->complete=false; break;
        case 5: b->complete=false; break;
        case 6: a->version=0; break;
        case 7: b->version=0; break;
        case 8: ++a->fan.ntrip; break;
        case 9: h.ctx.image=a; break;
        }
        struct gf5_image unchanged=*b;
        assert(gf5_state_boot_target(&h.ctx,b,a)==want);
        assert(!gf2_host_memcmp(b,&unchanged,sizeof(*b)) && !h.nevents);
        *a=original_a; *b=before; h.ctx.image=b;
        h.ctx.busy=false; h.ctx.phase=GF5_SAVED;
    }
    assert(gf5_state_boot_target(0,b,a)==GF5_EINVAL);
    assert(gf5_state_boot_target(&h.ctx,0,a)==GF5_EINVAL);
    assert(gf5_state_boot_target(&h.ctx,b,0)==GF5_EINVAL);
    assert(gf5_state_boot_target(&h.ctx,b,b)==GF5_EINVAL);
    a->fan_available=a->volt_available=a->clock_available=false;
    a->fan.ntrip=0; /* no count comparison or fan mutation when unavailable */
    struct gf5_fan_saved old_fan=b->fan;
    assert(!gf5_state_boot_target(&h.ctx,b,a));
    assert(!b->fan_available && !b->volt_available && !b->clock_available);
    assert(!gf2_host_memcmp(&b->fan,&old_fan,sizeof(old_fan)));
    *a=original_a;
    /* Zero shader remains available and must not suppress the raw target. */
    a->clock.shader_khz=0;
    assert(!gf5_state_boot_target(&h.ctx,b,a) && b->clock_available);
    assert(!b->clock.shader_khz);
    h.ctx.heat.down_active=true; h.ctx.heat.ceiling.core_khz=1;
    assert(gf5_state_restore(&h.ctx,b)==GF5_EBUSY);
    h.ctx.heat.down_active=false;
    captured_clock=a->clock; captured_raw=a->clock_raw;
    begin_restore();
    assert(!gf5_state_restore(&h.ctx,b));
    gf5_state_free(&h.ctx,a); gf5_state_free(&h.ctx,b);
    assert(!h.ctx.image && !h.live_allocs);
}
void _start(void)
{
    boot_target();
    diagnostic_conflict();
    optional_capture();
    roundtrip(); preallocation_failure(); save_failures(); restore_failures(); identities_and_guards();
    heat_saved_clock_limit(); heat_roundtrip(); heat_closed_operations();
    gf2_host_exit(0);
}
