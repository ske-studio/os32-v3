/* C2-links-a, UNIT14: real F3 service/V0/surface/B0/C1 plus scan and
 * links, no dependency stubs, libc or generated/reference-checkout data.
 * A2 links real callback targets (B0-B12/C0/save/recover) and C1 hook,
 * with GF_PRODUCTION_MODULE, -m32 -nostdlib and section GC. This TU includes
 * only links.c, allowing GF4_LINKS_SOURCE to select a runtime mutant.
 * Geometry oracle: GF_FEAT4_DESIGN X1/N4 VGA test area; format cpp from
 * nouveau c03c1a1 dispnv04/crtc.c:869-895. GPIO oracle: nv10.c:61-83,
 * base.c:82-96. Explicit expected fields below are independent pin truth
 * tables, not a copy of the product's bank/shift calculation. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#include "../../hwdev/gf/accel_service.h"
#include "../../hwdev/gf/production.h"
#include "../../drivers/video/geforce/geforce_module.h"
#include "../../include/gfx_format.h"
#ifndef GF4_LINKS_SOURCE
#define GF4_LINKS_SOURCE "../../hwdev/gf/display/links.c"
#endif
#include GF4_LINKS_SOURCE

/* Test storage budgets, not hardware capacities. */
#define LINKS_TEST_TRACE 128U
#define LINKS_TEST_VRAM (16U*1024U*1024U)
#define LINKS_TEST_DL 123456U
#define LINKS_VGA_W 640U
#define LINKS_VGA_H 480U
#define LINKS_VGA_BYTES (LINKS_VGA_W*LINKS_VGA_H)
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_host_event events[LINKS_TEST_TRACE];
static struct gf3_host_reg regs[GF4_HOST_VGA_REGS];
static struct gf_production production;
static struct gf3_service *service;
static const struct gf3_test_ops *accel_slot;
static const struct gf4_test_ops *normal_slot;
static const struct gf4_restore_ops *management_slot;
static GfxBackend backend;
static GfxCounters counters;
static int height,flip,page;
static struct geforce_platform links_platform;
static GeforceModuleContext module_context={
    .platform=&links_platform,.display_state=&state,
    .backend=&backend,.counters=&counters,.height=&height,.flip=&flip,.page=&page,
    .accel_out=&accel_slot,.display_out=&normal_slot,
    .display_restore_out=&management_slot
};
const GeforceModuleContext *geforce_context=&module_context;
static struct gf4_links *links;
static const struct gf4_exec_ops *exec;
static struct gf4_saved diagnostic;
/* Backing covers the initial scan, two cursors and the fixed test face;
 * four VGA faces exceed their sum, independently of placement padding. */
static u8 window_backing[4U*LINKS_VGA_BYTES] __attribute__((aligned(MEM_PAGE_SIZE)));
static struct gf_vram_view window;
static u32 window_closes;
static const struct gf4_mode vga={.id=1,
    .t={25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V},
    .fmt=GF3_INDEX8,.pitch=LINKS_VGA_W};
static i32 unexpected_lifecycle(void *owner,u32 dl)
{
    (void)owner; (void)dl; assert(false); return GF3_ESTATE;
}
static const struct gf3_service_owner lifecycle={
    .owner=&host.ctx,.start=unexpected_lifecycle,.stop=unexpected_lifecycle,
    .recover=unexpected_lifecycle
};
static i32 window_close(void *owner,struct gf_vram_view *view)
{
    assert(owner==&host && view==&window); ++window_closes;
    return gf_vram_view_unmap(view);
}
static void same(const struct gf3_surface *a,const struct gf3_surface *b)
{
    assert(!gf2_host_memcmp(a,b,sizeof(*a)));
}
static void closed(void)
{
    assert(!normal_slot && !management_slot && !accel_slot && !host.ctx.accepting);
    assert(geforce_blit(0,0,0,0,0,0)==OS32_ERR_NOSYS);
}
static void published(const struct gf3_surface *surface)
{
    struct video_fb fb; u32 w,h;
    geforce_backend_framebuffer_info(&fb,&w,&h);
    assert(fb.phys==production.chan.bar1.base+surface->off && fb.bytes==surface->bytes);
    assert(fb.pitch==surface->pitch && w==surface->width && h==surface->height);
    u32 fmt=surface->fmt==GF3_INDEX8 ? GFX_FMT_PACKED8 :
        surface->fmt==GF3_RGB565 ? GFX_FMT_RGB565 : GFX_FMT_XRGB8888;
    assert(fb.format==fmt);
    /* Fully clipped INDEX8 draws succeed without HW; other formats must
     * not inherit the legacy 8bit drawing hook (C1/X1). */
    assert(geforce_fill_rect(surface->width,0,1,1,0)==
           (surface->fmt==GF3_INDEX8 ? 0 : OS32_ERR_NOSYS));
}
static void live(const struct gf3_surface *surface)
{
    struct gf_vram_reservation key;
    assert(!gf_vram_match(host.ctx.ch,0,surface->reservation_id,surface->generation,
                          surface->off,surface->bytes,&key));
}
static void init(void)
{
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=LINKS_TEST_TRACE;
    host.base.regs=regs; host.base.cregs=sizeof(regs)/sizeof(regs[0]);
    production.chan=host.base.ch; production.foundation=host.base.f;
    production.chan.f=&production.foundation; production.chan.B=LINKS_TEST_VRAM;
    /* Synthetic aligned BAR address, not an actual device mapping. */
    production.chan.bar1=(struct recover_bar_extent){
        .base=0x30000000U,.length=LINKS_TEST_VRAM,.valid=true};
    host.base.ch.bar1=production.chan.bar1;
    links_platform.display_phys=production.chan.bar1.base;
    host.base.vram=window_backing; host.base.vram_bytes=sizeof(window_backing);
    production.foundation.imem.vram_bytes=LINKS_TEST_VRAM;
    production.accel.ch=&production.chan; production.accel.ready=true;
    production.accel.epoch=production.chan.epoch;
    host.ctx.ch=&production.chan; host.ctx.accel=&production.accel;
    host.ctx.legacy=&production.display; production.display.display4=&host.ctx;
    host.ctx.accepting=false;
    assert(!gf3_service_bind(&production,&accel_slot));
    service=gf3_service_get(&production.chan); assert(service);
    assert(!gf3_service_owner_bind(service,&lifecycle));
    links=gf4_links_table(); exec=gf4_exec_table(); links->owner=&host.ctx;
    host.ctx.links=links; host.ctx.exec=exec;
    assert(links==gf4_links_table() && exec==gf4_exec_table());
    assert(links->failed==gf4_service_failed && links->scan_changed==gf4_scan_changed);
    assert(exec->tmds_script); /* C2-links-b now supplies the real script callback. */
    state.nmodes=1; state.modes[0]=vga;
    state.head[0].mode=vga; state.head[0].enabled=true;
    assert(!gf4_scan_reserve_initial(&host.ctx,LINKS_TEST_DL));
    struct gf3_surface test={.off=host.ctx.test.value.off,.bytes=LINKS_VGA_BYTES,
        .pitch=LINKS_VGA_W,.width=LINKS_VGA_W,.height=LINKS_VGA_H,.fmt=GF3_INDEX8,
        .reservation_id=host.ctx.test.value.reservation_id,
        .generation=host.ctx.test.value.generation};
    assert(!gf3_service_surfaces(service,&state.head[0].front,&test));
}
static void test_getters(void)
{
    struct gf4_i2c_port out={0},sentinel={0};
    gf2_host_memset(&sentinel,0xa5,sizeof(sentinel)); out=sentinel;
    assert(links->i2c_port(0,0,&out)==GF4_EINVAL);
    assert(links->i2c_port(&host.ctx,0,0)==GF4_EINVAL);
    assert(links->i2c_port(&host.ctx,U8_MAX,&out)==GF4_ENODEV);
    assert(!gf2_host_memcmp(&out,&sentinel,sizeof(out)));
    const i32 errors[]={GF4_ENODEV,GF4_EUNSUPPORTED,GF4_EDDC};
    for (u32 i=0;i<sizeof(errors)/sizeof(errors[0]);++i) {
        host.ctx.dcb.i2c_rc[0]=errors[i];
        assert(links->i2c_port(&host.ctx,0,&out)==errors[i]);
        assert(!gf2_host_memcmp(&out,&sentinel,sizeof(out)));
    }
    /* bios/i2c.c:147-155: the two BMP fallback pairs; getter must not
     * choose another pair or manufacture defaults for missing entries. */
    host.ctx.dcb.i2c_rc[0]=0;
    host.ctx.dcb.i2c[0]=(struct gf4_i2c_port){0x3f,0x3e};
    assert(!links->i2c_port(&host.ctx,0,&out));
    assert(out.drive==0x3f && out.sense==0x3e);
    host.ctx.dcb.i2c[1]=(struct gf4_i2c_port){0x37,0x36};
    host.ctx.dcb.i2c_rc[1]=0;
    assert(!links->i2c_port(&host.ctx,1,&out));
    assert(out.drive==0x37 && out.sense==0x36);
    gf4_host_trace_clear(&host);
    assert(exec->external_mode(exec->owner,0,0,LINKS_TEST_DL)==GF4_EUNSUPPORTED);
    assert(exec->external_dpms(exec->owner,0,GF4_ON,LINKS_TEST_DL)==GF4_EUNSUPPORTED);
    assert(!host.nevents);
}
static void test_admission(void)
{
    gf4_host_trace_clear(&host);
    assert(!links->admission(&host.ctx,true));
    assert(host.ctx.accepting && normal_slot && management_slot && accel_slot);
    assert(normal_slot->ctx==&host.ctx && normal_slot->display_save==gf4_diagnostic_save);
    assert(management_slot->ctx==&host.ctx && management_slot->restore==gf4_diagnostic_restore);
    assert(management_slot->saved_release==gf4_display_saved_release);
    assert(accel_slot->ctx==&production.accel && accel_slot->test_surface==&service->test);
    same(&service->scan,&state.head[0].front);
    published(&state.head[0].front);
    assert(!host.nevents); /* No drain, close, HW access or wait. */
    struct gf3_surface old=service->scan,test=service->test;
    window.close=window_close; window.close_owner=&host;
    volatile u8 *loan=0;
    assert(!gf_vram_view_map(&window,host.ctx.ch,test.off,sizeof(u32),&loan));
    assert(window.mapped && loan);
    host.ctx.hold[0]=(struct gf4_flip_hold){.pending=true,.old_front=old,.new_front=old};
    assert(!links->admission(&host.ctx,false)); closed();
    assert(production.accel.ready && host.ctx.ready && host.ctx.hold[0].pending);
    assert(window.mapped && window.va==loan && !window_closes);
    same(&service->scan,&old); same(&service->test,&test); live(&old); live(&test);
    host.ctx.hold[0].pending=false;
    /* C0 opens with next before committing. A mode/format change is a
     * stronger contrast than merely switching between two equal faces. */
    struct gf3_surface pair[2];
    struct gf4_mode rgb=vga; rgb.id=2; rgb.fmt=GF3_RGB565; rgb.pitch=LINKS_VGA_W*(16U/8U);
    state.modes[state.nmodes++]=rgb;
    assert(!gf4_scan_prepare(&host.ctx,0,rgb.id,pair,LINKS_TEST_DL));
    state.next[0]=state.head[0]; state.next[0].mode=rgb; state.next[0].front=pair[0];
    host.ctx.changing=true;
    assert(!links->admission(&host.ctx,true));
    same(&service->scan,&pair[0]); same(&state.head[0].front,&old);
    assert(service->scan.fmt==GF3_RGB565);
    published(&pair[0]);
    /* Same dimensions with 32bit pixels must likewise use next's pitch. */
    assert(!links->admission(&host.ctx,false));
    host.ctx.changing=false;
    rgb.id=3; rgb.fmt=GF3_XRGB8888; rgb.pitch=LINKS_VGA_W*(32U/8U);
    state.modes[state.nmodes++]=rgb;
    assert(!gf4_scan_discard(&host.ctx,0,LINKS_TEST_DL));
    assert(!gf4_scan_prepare(&host.ctx,0,rgb.id,pair,LINKS_TEST_DL));
    state.next[0].mode=rgb; state.next[0].front=pair[0]; host.ctx.changing=true;
    assert(!links->admission(&host.ctx,true)); same(&service->scan,&pair[0]);
    assert(service->scan.fmt==GF3_XRGB8888);
    published(&pair[0]);
    assert(!links->admission(&host.ctx,false)); closed(); host.ctx.changing=false;
    assert(!links->admission(&host.ctx,true)); same(&service->scan,&old);
    published(&old);
    /* Head B validation must finish before head A loans or slots change. */
    state.head[1]=state.head[0]; state.head[1].front.generation++;
    assert(links->admission(&host.ctx,true)<0); closed(); same(&service->scan,&old);
    state.head[1]=(struct gf4_head_image){0};
    state.head[1]=state.head[0]; state.head[1].mode.id=99; /* Distinct fixture ID. */
    assert(!links->admission(&host.ctx,true)); same(&service->scan,&old);
    state.head[1]=(struct gf4_head_image){0};
    assert(!links->admission(&host.ctx,true));
    host.ctx.saved_closed=true; host.ctx.diagnostic_saved=0;
    assert(links->admission(&host.ctx,true)==GF4_ESTATE); closed();
    host.ctx.diagnostic_saved=&diagnostic;
    assert(!links->admission(&host.ctx,true));
    host.ctx.saved_closed=false; host.ctx.diagnostic_saved=0;
    /* A stale test key fails before replacing the old scan loan. */
    u32 generation=service->test.generation; service->test.generation++;
    assert(links->admission(&host.ctx,true)<0); closed(); same(&service->scan,&old);
    service->test.generation=generation; live(&test);
    assert(!links->admission(&host.ctx,true));
    ++host.ctx.epoch;
    assert(links->admission(&host.ctx,true)==GF4_ESTATE); closed();
    --host.ctx.epoch; assert(!links->admission(&host.ctx,true));
    host.ctx.ch->fault.latched=true;
    assert(links->admission(&host.ctx,true)==GF4_ESTATE); closed();
    host.ctx.ch->fault.latched=false;
    /* No test face keeps F3 slot NULL while display remains usable. */
    assert(!gf3_service_surfaces(service,&old,0));
    assert(!links->admission(&host.ctx,true));
    assert(normal_slot && management_slot && !accel_slot);
    assert(!links->admission(&host.ctx,false)); closed();
    assert(!gf3_service_surfaces(service,&old,&test));
    /* Absent head A is allowed; it must clear the old scan loan. */
    state.head[0].enabled=false;
    assert(!links->admission(&host.ctx,true));
    assert(!service->scan.bytes && normal_slot && accel_slot);
    assert(!links->admission(&host.ctx,false)); closed();
    state.head[0].enabled=true;
    /* A changed registry generation is a real stale loan, not an event. */
    ++host.ctx.scan[0][0].value.generation;
    assert(links->admission(&host.ctx,true)<0); closed();
    --host.ctx.scan[0][0].value.generation;
    live(&old); assert(!links->admission(&host.ctx,true));
    assert(!links->admission(&host.ctx,false)); closed();
    assert(window.mapped && !window_closes);
    assert(!window_close(&host,&window)); assert(!window.mapped && window_closes==1);
}
/* GPIO truth table from gpio/nv10.c:61-83. Each row lists the exact pin
 * field for log=0,1,2,3: low dir bit4/out bit0, other banks dir bit1/out bit0.
 * Edge pins cover every bank and preservation of neighbouring pins. */
static const struct gpio_oracle {
    u8 line; u32 reg,mask,field[4];
} gpio_oracle[]={
    {0,0x600818U,0x00000011U,{0,0x00000001U,0x00000010U,0x00000011U}},
    {1,0x600818U,0x00110000U,{0,0x00010000U,0x00100000U,0x00110000U}},
    {2,0x60081cU,0x00000003U,{0,0x00000001U,0x00000002U,0x00000003U}},
    {9,0x60081cU,0x30000000U,{0,0x10000000U,0x20000000U,0x30000000U}},
    {10,0x600850U,0x00000003U,{0,0x00000001U,0x00000002U,0x00000003U}},
    {13,0x600850U,0x00003000U,{0,0x00001000U,0x00002000U,0x00003000U}}
};
static void test_gpio(void)
{
    /* Use A1's MMIO image for GPIO; publication tests above use real V0
     * and F3. No hardware/service API is replaced by a stub. */
    production.foundation.io=host.base.f.io;
    gf4_host_trace_clear(&host);
    assert(!exec->tv_gpio(exec->owner,true,LINKS_TEST_DL)); assert(!host.nevents);
    for (u32 i=0;i<sizeof(gpio_oracle)/sizeof(gpio_oracle[0]);++i) {
        const struct gpio_oracle *o=&gpio_oracle[i];
        for (u8 log=0;log<4U;++log) for (u8 on=0;on<2U;++on) {
            const u32 background=0xa5a5a5a5U; /* Arbitrary preservation pattern. */
            gf3_host_set(&host.base,0,o->reg,background);
            host.ctx.dcb.tvdac[0]=(struct gf4_gpio_value){true,o->line,{log,(u8)(3U-log)}};
            gf4_host_trace_clear(&host);
            assert(!exec->tv_gpio(exec->owner,on,LINKS_TEST_DL));
            assert(host.calls[D_READ]==1 && host.calls[D_WRITE]==1 && host.nevents==2);
            const struct gf4_host_event *write=&events[1];
            assert(write->op==D_WRITE && write->a[0]==GF_MODE_REGISTER_BAR);
            assert(write->a[1]==o->reg && write->a[2]==4);
            assert(write->a[3]==((background&~o->mask)|o->field[on ? 3U-log : log]));
        }
    }
    /* Two present lines: fail the first read/write; never touch the second.
     * Negative bus values are deliberately outside F1-F4 enums. */
    const i32 injected=-1701;
    host.ctx.dcb.tvdac[0]=(struct gf4_gpio_value){true,0,{0,1}};
    host.ctx.dcb.tvdac[1]=(struct gf4_gpio_value){true,10,{0,1}};
    struct gf3_surface old=state.head[0].front;
    for (u32 op=D_READ;op<=D_WRITE;++op) {
        gf4_host_trace_clear(&host); gf4_host_fail_at(&host,op,1,injected);
        assert(exec->tv_gpio(exec->owner,true,LINKS_TEST_DL)==injected);
        assert(host.ctx.last_rc==injected && host.calls[D_READ]==1);
        assert(host.calls[D_WRITE]==(op==D_WRITE ? 1U : 0));
        live(&old); assert(production.accel.ready); closed();
        host.fail_rc[op]=0;
    }
    gf4_host_trace_clear(&host);
    assert(!exec->tv_gpio(exec->owner,true,LINKS_TEST_DL));
    assert(host.calls[D_READ]==2 && host.calls[D_WRITE]==2);
    assert(events[1].a[1]==gpio_oracle[0].reg && events[3].a[1]==gpio_oracle[4].reg);
    host.ctx.dcb.tvdac[1].line=14; /* nv10.c:79-83 rejects lines >=14. */
    gf4_host_trace_clear(&host);
    assert(exec->tv_gpio(exec->owner,true,LINKS_TEST_DL)==GF4_EINVAL);
    assert(!host.nevents); live(&old);
}
void _start(void)
{
    init(); test_getters(); test_admission(); test_gpio(); gf2_host_exit(0);
}
