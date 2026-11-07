#define GF5_HOST_UNIT 9
#include "gf5_fake.h"

/* Register oracle: MIT nouveau c03c1a1 nvkm/engine/gr/nv04.c:1211-1226
 * and regs.h:160. Remaining constants describe synthetic ROM/VRAM placement. */
enum { TEST_GR_STATUS=0x400700U, TEST_SYNC_STALL=0x4000U,
       TEST_BUSY=1U, TEST_PC=32U, TEST_ROM_BYTES=64U,
       TEST_COPY_BYTES=16U, TEST_VRAM_BYTES=GF_MODE_VRAM_BYTES+MEM_PAGE_SIZE };
static struct gf5_host h;
static struct gf4_state state;
static struct gf_display_context legacy;
static struct gf_init_context prepared;
static struct gf_init_program program;
static struct gf5_image image;
static struct gf5_host_event events[64];
static struct gf4_host_event display_events[8192];
static struct gf3_host_reg regs[4];
static u8 arena[MEM_PAGE_SIZE*8U] __attribute__((aligned(16)));
static u8 vram[TEST_VRAM_BYTES],ram[TEST_COPY_BYTES],rom[TEST_ROM_BYTES];
static struct gf_vram_reservation_node node;
static u32 seen_start,seen_steps,seen_wait,seen_elapsed,seen_script;
static struct gf_init_context *seen_context;
static bool leave_save;

static void observe_script(struct gf5_host *host,const struct gf5_host_event *e)
{
    if (e->op!=F5_SCRIPT) return;
    const struct gf_init_context *ctx=e->p[0];
    assert(e->a[0]==F5_CALL_EXECUTE && e->p[1]==&program);
    assert(e->a[1]==TEST_PC && !e->a[2] && !e->a[3] && e->a[4]==host->ctx.deadline);
    assert(ctx->rom==rom && ctx->rom_size==sizeof(rom) && !ctx->saved_count);
    seen_start=ctx->started_us; seen_steps=ctx->steps;
    seen_wait=ctx->wait_us; seen_elapsed=ctx->elapsed_us; ++seen_script;
    seen_context=(struct gf_init_context *)ctx;
    if (leave_save && !e->rc) seen_context->saved_count=1;
}
static void fixture(void)
{
    gf5_host_init(&h,&state);
    h.events=events; h.cevents=sizeof(events)/sizeof(events[0]);
    h.display.events=display_events;
    h.display.cevents=sizeof(display_events)/sizeof(display_events[0]);
    h.display.base.regs=regs; h.display.base.cregs=sizeof(regs)/sizeof(regs[0]);
    gf5_host_arena(&h,arena,sizeof(arena));
    h.now=100U; h.ctx.deadline=10000U;
    legacy=(struct gf_display_context){.bar1_map=vram};
    prepared=(struct gf_init_context){0}; gf5_host_init_io(&h,&prepared);
    legacy.io=prepared.io; h.ctx.display->legacy=&legacy;
    h.ctx.rom=(struct gf5_rom_view){rom,sizeof(rom),&program,TEST_PC};
    program.count=1; program.insns[0].pc=TEST_PC; program.insns[0].opcode=GF_INIT_DONE;
    seen_start=seen_steps=seen_wait=seen_elapsed=seen_script=0;
    seen_context=0; leave_save=false;
    h.display.base.vram=vram; h.display.base.vram_bytes=sizeof(vram);
    h.ctx.ch->bar1=(struct recover_bar_extent){.valid=true,.base=0x10000000U,
                                             .length=sizeof(vram)};
    node=(struct gf_vram_reservation_node){.value={&node,GF_MODE_VRAM_BYTES-8U,
                                                 sizeof(ram),7U,3U}};
    h.display.base.reservations=&node;
}
static void error_is(i32 rc)
{
    assert(h.ctx.source_rc==rc && h.ctx.last_rc==gf5_rc_map(rc));
}
static void test_map(void)
{
    static const struct { i32 lower,expected; } pairs[]={
        {0,GF5_OK},
        {GF1_EINVAL,GF5_EINVAL},{GF2_EINVAL,GF5_EINVAL},
        {GF3_EINVAL,GF5_EINVAL},{GF4_EINVAL,GF5_EINVAL},
        {GF1_ENOSPC,GF5_ENOMEM},{GF2_ENOSPC,GF5_ENOMEM},{GF4_ENOSPC,GF5_ENOMEM},
        {GF3_ERANGE,GF5_ERANGE},{GF4_ERANGE,GF5_ERANGE},
        {GF2_ESTATE,GF5_ESTATE},{GF3_ESTATE,GF5_ESTATE},{GF4_ESTATE,GF5_ESTATE},
        {GF3_EUNSUPPORTED,GF5_ENOTSUP},{GF4_EUNSUPPORTED,GF5_ENOTSUP},
        {GF4_ENODEV,GF5_ENOTSUP},{GF4_EBUSY,GF5_EBUSY},
        {GF2_ETIME,GF5_ETIME},{GF4_ETIME,GF5_ETIME},{GF_SCRIPT_ETIME,GF5_ETIME},
        {GF1_EPCI,GF5_EIO},{GF1_EMAP,GF5_EIO},{GF1_EIRQ,GF5_EIO},
        {GF1_ESTOP,GF5_EIO},{GF1_EIO,GF5_EIO},{GF2_EMAP,GF5_EIO},
        {GF2_EIO,GF5_EIO},{GF2_EFAULT,GF5_EIO},{GF2_EEXIST,GF5_EIO},
        {GF2_ECLASS,GF5_EIO},{GF2_ESTOP,GF5_EIO},{GF3_EFORMAT,GF5_EIO},
        {GF4_EFORMAT,GF5_EIO},{GF4_EPLL,GF5_EIO},{GF4_EDDC,GF5_EIO},
        {GF4_EEDID,GF5_EIO},{GF_SCRIPT_EIO,GF5_EIO},
        {GF5_EINVAL,GF5_EINVAL},{GF5_ENOTSUP,GF5_ENOTSUP},{GF5_EBIOS,GF5_EBIOS},
        {GF5_ERANGE,GF5_ERANGE},{GF5_EBUSY,GF5_EBUSY},{GF5_ETIME,GF5_ETIME},
        {GF5_EIO,GF5_EIO},{GF5_ENOMEM,GF5_ENOMEM},{GF5_ESTATE,GF5_ESTATE},
        {GF5_ENODEV,GF5_ENODEV},
        {-999,GF5_EIO},{1,GF5_EIO}
    };
    for (u32 i=0;i<sizeof(pairs)/sizeof(pairs[0]);++i)
        assert(gf5_rc_map(pairs[i].lower)==pairs[i].expected);
}
static void test_table_and_arguments(void)
{
    fixture();
    struct gf5_links *l=gf5_links_table();
    assert(l==gf5_links_table() && !l->owner);
    assert(l->drain_pause==gf5_link_drain_pause && l->unpause==gf5_link_unpause);
    assert(l->display_save==gf5_link_display_save && l->display_restore==gf5_link_display_restore);
    assert(l->memory_script==gf5_link_memory_script && l->admission==gf5_link_admission);
    assert(l->drain==gf5_link_drain && l->stop==gf5_link_stop && l->restart==gf5_link_restart);
    assert(l->copy_extent==gf5_link_copy_extent && l->alloc==gf5_link_alloc);
    assert(l->free==gf5_link_free && l->clock64==gf5_link_clock64);
    u64 now=0;
    assert(l->drain_pause(0)==GF5_EINVAL && l->unpause(0)==GF5_EINVAL);
    assert(l->admission(0,false)==GF5_EINVAL && l->drain(0,1)==GF5_EINVAL);
    assert(l->display_save(0,&image)==GF5_EINVAL && l->display_restore(0,&image)==GF5_EINVAL);
    assert(l->stop(0,1)==GF5_EINVAL && l->restart(0,1)==GF5_EINVAL);
    assert(l->memory_script(0,TEST_PC)==GF5_EINVAL);
    assert(l->copy_extent(0,&node.value,ram,false)==GF5_EINVAL);
    void *p=ram;
    assert(l->alloc(0,1,&p)==GF5_EINVAL && l->clock64(0,&now)==GF5_EINVAL);
    l->free(0,0);
    assert(p==ram && !h.nevents && !h.display.nevents);
    l->owner=&h.ctx;
    assert(l->admission(l->owner,false)==0 && !h.ctx.accepting && !h.ctx.display->accepting);
    assert(l->admission(l->owner,true)==0 && h.ctx.accepting && h.ctx.display->accepting);
    l->owner=0;
    fixture(); h.ctx.display=0;
    assert(gf5_link_display_save(&h.ctx,&image)==GF5_EINVAL);
    assert(gf5_link_display_restore(&h.ctx,&image)==GF5_EINVAL);
    assert(gf5_link_stop(&h.ctx,1)==GF5_EINVAL);
    assert(gf5_link_restart(&h.ctx,1)==GF5_EINVAL);
    assert(!h.nevents && !h.display.nevents);
}
static void test_pause(void)
{
    fixture();
    gf3_host_set(&h.display.base,0,TEST_GR_STATUS,TEST_SYNC_STALL);
    assert(gf5_link_drain_pause(&h.ctx)==0);
    assert(!h.ctx.accepting && !h.ctx.display->accepting && h.display.base.paused);
    assert(h.nevents==3 && events[0].op==F5_DRAIN && events[1].op==F5_DRAIN);
    assert(events[0].a[0]==h.ctx.deadline && events[1].a[0]==h.ctx.deadline);
    assert(events[2].op==F5_PAUSE && h.display.calls[D_READ]==1);
    h.ctx.ch->last_rc=GF2_ETIME;
    assert(gf5_link_unpause(&h.ctx)==0 && h.ctx.ch->last_rc==GF2_ETIME);
    assert(!h.display.base.paused && !h.ctx.accepting); /* B2 owns reopening. */

    const i32 failures[]={GF2_ETIME,GF2_ESTOP};
    for (u32 i=0;i<sizeof(failures)/sizeof(failures[0]);++i) {
        fixture(); gf5_host_fail_at(&h,F5_DRAIN,1,failures[i]);
        assert(gf5_link_drain_pause(&h.ctx)==failures[i]); error_is(failures[i]);
        assert(h.calls[F5_DRAIN]==1 && !h.calls[F5_PAUSE] && !h.display.calls[D_READ]);
        assert(!h.ctx.accepting && h.ctx.ch->state==GF2_RUN);
    }
    fixture(); gf5_host_fail_at(&h,F5_DRAIN,2,GF2_ETIME);
    assert(gf5_link_drain_pause(&h.ctx)==GF2_ETIME);
    assert(h.calls[F5_DRAIN]==2 && !h.calls[F5_PAUSE]);
    fixture(); gf5_host_fail_at(&h,F5_PAUSE,1,GF2_ESTOP);
    assert(gf5_link_drain_pause(&h.ctx)==GF2_ESTOP); error_is(GF2_ESTOP);
    assert(h.display.base.paused && !h.display.calls[D_READ] && !h.calls[F5_RESUME]);
    fixture(); gf4_host_fail_at(&h.display,D_ADMISSION,1,GF4_EBUSY);
    assert(gf5_link_drain_pause(&h.ctx)==GF4_EBUSY && !h.calls[F5_DRAIN]);
    assert(!h.ctx.accepting);

    fixture(); h.ctx.ch->last_rc=GF2_ETIME;
    gf5_host_fail_at(&h,F5_RESUME,1,GF2_ESTOP);
    assert(gf5_link_unpause(&h.ctx)==GF2_ESTOP); error_is(GF2_ESTOP);
    assert(h.ctx.phase==GF5_FAULT && !h.ctx.accepting && !h.ctx.display->accepting);
    assert(h.ctx.ch->state==GF2_RUN && h.ctx.ch->last_rc==GF2_ESTOP);
    fixture(); h.ctx.ch->fault.latched=true;
    assert(gf5_link_unpause(&h.ctx)==GF2_EFAULT && h.ctx.phase==GF5_FAULT);
    fixture(); h.ctx.ch->state=GF2_OFF;
    assert(gf5_link_unpause(&h.ctx)==GF2_ESTATE && !h.ctx.accepting);
}
static void slow_pause(struct gf5_host *host,const struct gf5_host_event *e)
{
    if (e->op==F5_PAUSE) host->now+=GF2_TIMEOUT_US+111U;
}
static void idle_after_wait(struct gf4_host *host,const struct gf4_host_event *e)
{
    if (e->op==D_SLEEP && e->nth==1500U)
        gf3_host_set(&host->base,0,TEST_GR_STATUS,TEST_SYNC_STALL);
}
static void test_idle(void)
{
    fixture(); h.ctx.deadline=(u32)h.now+1500U;
    gf3_host_set(&h.display.base,0,TEST_GR_STATUS,TEST_BUSY|TEST_SYNC_STALL);
    assert(gf5_link_drain_pause(&h.ctx)==GF2_ETIME); error_is(GF2_ETIME);
    assert(h.now==100U+GF2_TIMEOUT_US && h.display.calls[D_SLEEP]==2000U && !h.calls[F5_RESUME]);
    u32 sleeps=0;
    for (u32 i=0;i<h.display.nevents;++i) if (display_events[i].op==D_SLEEP) {
        assert(display_events[i].a[0]==1000U); ++sleeps;
    }
    assert(sleeps==2000U);
    /* nv04_gr_idle starts its full budget after pause, even when preceding
     * work consumed the outer operation deadline. Busy may then clear. */
    fixture(); h.observe=slow_pause; h.display.observe=idle_after_wait;
    gf3_host_set(&h.display.base,0,TEST_GR_STATUS,TEST_BUSY);
    assert(gf5_link_drain_pause(&h.ctx)==0);
    assert(h.now==100U+GF2_TIMEOUT_US+111U+1500000U);
    assert(h.display.calls[D_SLEEP]==1500U && h.display.calls[D_READ]==1501U);
    assert(h.ctx.deadline==10000U && h.display.base.paused && !h.ctx.accepting);
    /* Persistent busy still expires two seconds after the delayed pause. */
    fixture(); h.observe=slow_pause;
    gf3_host_set(&h.display.base,0,TEST_GR_STATUS,TEST_BUSY);
    assert(gf5_link_drain_pause(&h.ctx)==GF2_ETIME); error_is(GF2_ETIME);
    assert(h.now==100U+2U*GF2_TIMEOUT_US+111U && h.display.calls[D_SLEEP]==2000U);
    fixture(); h.ctx.deadline=(u32)h.now+GF2_TIMEOUT_US+1000U;
    gf3_host_set(&h.display.base,0,TEST_GR_STATUS,TEST_BUSY);
    assert(gf5_link_drain_pause(&h.ctx)==GF2_ETIME);
    assert(h.now==100U+GF2_TIMEOUT_US && h.display.calls[D_SLEEP]==2000U);
    fixture(); gf4_host_fail_at(&h.display,D_READ,1,-77);
    assert(gf5_link_drain_pause(&h.ctx)==-77); error_is(-77);
    assert(!h.display.calls[D_SLEEP] && !h.calls[F5_RESUME]);
    fixture(); gf3_host_set(&h.display.base,0,TEST_GR_STATUS,TEST_BUSY);
    gf4_host_fail_at(&h.display,D_SLEEP,1,-78);
    assert(gf5_link_drain_pause(&h.ctx)==-78); error_is(-78);
    assert(h.display.calls[D_READ]==1 && !h.calls[F5_RESUME]);
    fixture(); h.now=U32_MAX-499U; h.ctx.deadline=1000U;
    gf3_host_set(&h.display.base,0,TEST_GR_STATUS,TEST_BUSY);
    assert(gf5_link_drain_pause(&h.ctx)==GF2_ETIME);
    assert((u32)h.now==GF2_TIMEOUT_US-500U && h.display.calls[D_SLEEP]==2000U);
}
static void off_with_error(struct gf5_host *host,const struct gf5_host_event *e)
{
    if (e->op==F5_STOP) host->ctx.ch->state=GF2_OFF;
}
static void test_display(void)
{
    fixture();
    assert(gf5_link_display_save(&h.ctx,&image)==0);
    assert(events[0].op==F5_SAVE && events[0].p[0]==h.ctx.display);
    assert(events[0].p[1]==&image.display && events[0].a[0]==h.ctx.deadline);
    assert(h.ctx.display->saved_closed && !h.ctx.display->accepting);
    assert(gf5_link_stop(&h.ctx,1234U)==0 && h.ctx.ch->state==GF2_OFF);
    assert(events[1].a[0]==1234U);
    u32 epoch=h.ctx.ch->epoch;
    assert(gf5_link_restart(&h.ctx,4321U)==0 && h.ctx.ch->epoch==epoch+1U);
    assert(events[2].a[0]==4321U && !h.ctx.accepting);
    assert(h.ctx.deadline==(u32)h.now+GF2_TIMEOUT_US);
    assert(h.ctx.display->deadline==h.ctx.deadline);
    assert(gf5_link_display_restore(&h.ctx,&image)==0);
    assert(events[3].op==F5_RESTORE && events[3].p[1]==&image.display);
    assert(events[3].a[0]==(u32)h.now+GF2_TIMEOUT_US && h.ctx.display->saved_closed);
    assert(h.ctx.display->deadline==events[3].a[0]);
    assert(gf5_link_admission(&h.ctx,true)==GF4_ESTATE && !h.ctx.accepting);
    fixture(); h.observe=off_with_error; h.ctx.image=&image;
    gf5_host_fail_at(&h,F5_STOP,1,GF2_ESTOP);
    assert(gf5_link_stop(&h.ctx,h.ctx.deadline)==GF2_ESTOP); error_is(GF2_ESTOP);
    assert(h.ctx.ch->state==GF2_OFF && h.ctx.image==&image && !h.calls[F5_RESTART]);
    assert(h.display.base.reservations==&node && !h.calls[F5_FREE]);
    fixture(); gf5_host_fail_at(&h,F5_SAVE,1,GF4_ETIME);
    assert(gf5_link_display_save(&h.ctx,&image)==GF4_ETIME); error_is(GF4_ETIME);
    fixture(); gf5_host_fail_at(&h,F5_RESTORE,1,GF4_EPLL);
    assert(gf5_link_display_restore(&h.ctx,&image)==GF4_EPLL); error_is(GF4_EPLL);
    fixture(); gf5_host_fail_at(&h,F5_RESTART,1,GF1_EMAP);
    assert(gf5_link_restart(&h.ctx,777U)==GF1_EMAP); error_is(GF1_EMAP);
    assert(events[0].a[0]==777U && !h.calls[F5_RESTORE]);
}
static void test_restore_deadline(void)
{
    fixture(); h.now+=GF2_TIMEOUT_US;
    assert(!gf5_link_display_restore(&h.ctx,&image));
    assert(events[0].op==F5_RESTORE);
    assert(events[0].a[0]==(u32)h.now+GF2_TIMEOUT_US);
    assert(h.ctx.display->deadline==events[0].a[0]);
    fixture();
    gf4_host_fail_at(&h.display,D_CLOCK,1,-1701);
    assert(gf5_link_display_restore(&h.ctx,&image)==GF2_EIO);
    error_is(GF2_EIO); assert(!h.calls[F5_RESTORE]);
    fixture();
    assert(!gf5_link_stop(&h.ctx,h.ctx.deadline));
    gf4_host_fail_at(&h.display,D_CLOCK,1,-1701);
    assert(gf5_link_restart(&h.ctx,h.ctx.deadline)==GF2_EIO);
    error_is(GF2_EIO); assert(h.calls[F5_RESTART]==1);
}
static void test_memory(void)
{
    fixture(); h.observe=observe_script;
    assert(gf5_link_memory_script(&h.ctx,TEST_PC)==0);
    assert(seen_script==1 && seen_start==100U && !seen_steps && !seen_wait && !seen_elapsed);
    assert(h.calls[F5_SCRIPT]==1 && h.calls[F5_CLEANUP]==1 && !h.calls[F5_ALLOC]);
    assert(!legacy.init && h.ctx.rom.memory_program==&program && program.count==1);
    fixture(); h.observe=observe_script;
    legacy.init=&prepared; prepared.rom=rom; prepared.rom_size=sizeof(rom);
    prepared.started_us=20U; prepared.steps=7U; prepared.wait_us=9U; prepared.elapsed_us=11U;
    assert(gf5_link_memory_script(&h.ctx,TEST_PC)==0);
    assert(seen_start==20U && seen_steps==7U && seen_wait==9U && seen_elapsed==11U);
    assert(!h.display.calls[D_CLOCK] && h.calls[F5_CLEANUP]==1);
    assert(prepared.rom==rom && prepared.started_us==20U && !prepared.saved_count);
    for (u32 n=0;n<2;++n) {
        fixture(); h.observe=observe_script;
        i32 rc=n ? GF_SCRIPT_ETIME : GF_SCRIPT_EIO;
        gf5_host_fail_at(&h,F5_SCRIPT,1,rc);
        gf5_host_fail_at(&h,F5_CLEANUP,1,GF_SCRIPT_EIO);
        assert(gf5_link_memory_script(&h.ctx,TEST_PC)==rc); error_is(rc);
        assert(h.calls[F5_SCRIPT]==1 && h.calls[F5_CLEANUP]==1 && !h.calls[F5_ALLOC]);
        assert(h.ctx.rom.bytes==rom && h.ctx.rom.memory_program==&program);
    }
    fixture(); gf5_host_fail_at(&h,F5_CLEANUP,1,GF_SCRIPT_EIO);
    assert(gf5_link_memory_script(&h.ctx,TEST_PC)==GF_SCRIPT_EIO); error_is(GF_SCRIPT_EIO);
    assert(h.calls[F5_CLEANUP]==1 && h.calls[F5_SCRIPT]==1);
    fixture(); h.observe=observe_script; leave_save=true;
    gf5_host_fail_at(&h,F5_CLEANUP,1,GF_SCRIPT_EIO);
    assert(gf5_link_memory_script(&h.ctx,TEST_PC)==GF_SCRIPT_EIO);
    assert(seen_context && seen_context->saved_count==1);
    assert(gf5_link_memory_script(&h.ctx,TEST_PC)==GF5_ESTATE);
    assert(h.calls[F5_SCRIPT]==1 && h.calls[F5_CLEANUP]==1);
    gf5_host_fail_at(&h,F5_CLEANUP,0,0);
    /* Inspect the retained transaction after the adapter has returned;
     * this explicit fixture cleanup is not an automatic product retry. */
    assert(seen_context->io.restore(seen_context->io.opaque,seen_context)==0);
    assert(!seen_context->saved_count);
    fixture(); gf4_host_fail_at(&h.display,D_CLOCK,1,-79);
    assert(gf5_link_memory_script(&h.ctx,TEST_PC)==-79); error_is(-79);
    assert(!h.calls[F5_SCRIPT] && !h.calls[F5_CLEANUP]);
    fixture(); h.ctx.rom=(struct gf5_rom_view){0};
    assert(gf5_link_memory_script(&h.ctx,0)==GF5_ENOTSUP);
    fixture(); h.ctx.rom.memory_pc=0; h.ctx.rom.memory_program=0;
    assert(gf5_link_memory_script(&h.ctx,0)==0 && !h.nevents && !h.display.nevents);
    fixture();
    assert(gf5_link_memory_script(&h.ctx,TEST_PC+1U)==GF5_EBIOS && !h.nevents);
    fixture(); legacy.init=&prepared; prepared.saved_count=1;
    assert(gf5_link_memory_script(&h.ctx,TEST_PC)==GF5_ESTATE && prepared.saved_count==1);
    assert(!h.calls[F5_SCRIPT] && !h.calls[F5_CLEANUP]);
}
static void test_resources(void)
{
    fixture(); void *p=0;
    assert(gf5_link_alloc(&h.ctx,MEM_PAGE_SIZE,&p)==0 && p && h.live_allocs==1);
    gf5_link_free(&h.ctx,p); assert(!h.live_allocs && h.calls[F5_FREE]==1);
    p=ram; gf5_host_fail_at(&h,F5_ALLOC,2,GF5_ENOMEM);
    assert(gf5_link_alloc(&h.ctx,1,&p)==GF5_ENOMEM && p==ram && !h.live_allocs);
    assert(gf5_link_alloc(&h.ctx,U32_MAX,&p)==GF5_ENOMEM && p==ram);
    assert(gf5_link_alloc(&h.ctx,0,&p)==GF5_EINVAL && p==ram);
    u64 now=0;
    h.now=((u64)3U<<32)|123U;
    assert(gf5_link_clock64(&h.ctx,&now)==0 && now==h.now);
    gf5_host_fail_at(&h,F5_CLOCK64,2,-80);
    assert(gf5_link_clock64(&h.ctx,&now)==-80 && now==h.now); error_is(-80);
}
static void test_copy(void)
{
    fixture();
    for (u32 i=0;i<sizeof(ram);++i) { ram[i]=(u8)(i+1U); vram[node.value.off+i]=0; }
    assert(gf5_link_copy_extent(&h.ctx,&node.value,ram,true)==0);
    for (u32 i=0;i<sizeof(ram);++i) assert(vram[node.value.off+i]==i+1U);
    assert(h.display.calls[D_MAP]==1 && h.display.calls[D_UNMAP]==1 && !h.display.views);
    assert(h.display.base.reservations==&node && !h.ctx.ch->mapped);
    gf2_host_memset(ram,0,sizeof(ram));
    assert(gf5_link_copy_extent(&h.ctx,&node.value,ram,false)==0);
    for (u32 i=0;i<sizeof(ram);++i) assert(ram[i]==i+1U);
    fixture(); gf5_host_fail_at(&h,F5_MATCH,1,GF3_ESTATE);
    assert(gf5_link_copy_extent(&h.ctx,&node.value,ram,false)==GF3_ESTATE);
    assert(!h.display.calls[D_MAP] && h.display.base.reservations==&node);
    fixture(); node.value.off=GF_MODE_VRAM_BYTES; node.value.bytes=sizeof(ram);
    gf4_host_fail_at(&h.display,D_MAP,1,GF2_EMAP);
    assert(gf5_link_copy_extent(&h.ctx,&node.value,ram,false)==GF2_EMAP);
    assert(!h.display.calls[D_UNMAP] && !h.display.views);
    fixture(); gf4_host_fail_at(&h.display,D_UNMAP,1,GF2_EMAP);
    assert(gf5_link_copy_extent(&h.ctx,&node.value,ram,true)==GF2_EMAP); error_is(GF2_EMAP);
    struct gf_vram_view *view=h.display.views;
    assert(view && view->mapped && view->close && view->close_owner==&h.ctx);
    assert(h.display.base.reservations==&node && !h.calls[F5_STOP]);
    assert(gf5_link_copy_extent(&h.ctx,&node.value,ram,true)==GF5_EBUSY);
    assert(h.display.views==view && view->mapped);
    gf4_host_fail_at(&h.display,D_UNMAP,0,0);
    assert(view->close(view->close_owner,view)==0 && !h.display.views);
    assert(gf5_link_copy_extent(&h.ctx,&node.value,ram,false)==0);
    fixture(); node.value.off++;
    assert(gf5_link_copy_extent(&h.ctx,&node.value,ram,false)==GF5_ERANGE && !h.nevents);
}
void _start(void)
{
    test_map(); test_table_and_arguments(); test_pause(); test_idle();
    test_display(); test_restore_deadline(); test_memory(); test_resources(); test_copy();
    gf2_host_exit(0);
}
