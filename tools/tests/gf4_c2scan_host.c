/* UNIT14: real scan.c + V0 + F3 service/surface validation + B0 + C1 hook.
 * No dependency stubs. A2 links with -ffunction-sections/--gc-sections,
 * GF_PRODUCTION_MODULE, -nostdlib and this freestanding _start.
 * Oracle: nouveau c03c1a1 dispnv04/crtc.c:838-903 (format/pitch),
 * :1074-1087,1136-1217 (pin until completion); nouveau_bo.c:594-629
 * (stable pin identity). OS32 X1/N4 supplies the fixed VGA test geometry.
 * All expected sizes below are dimensions * bytes/pixel, independent of
 * scan.c's placement implementation; no reference checkout needed at runtime. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#include "../../hwdev/gf/internal.h"
#include "../../hwdev/gf/production.h"
#include "../../hwdev/gf/accel_service.h"

#define TEST_VRAM (16U*1024U*1024U)
#define TEST_DL 123456U
#define VGA_WIDTH 640U
#define VGA_HEIGHT 480U
#define VGA_BYTES (VGA_WIDTH*VGA_HEIGHT)
#define XGA_WIDTH 1024U
#define XGA_HEIGHT 768U
#define XGA32_BYTES (XGA_WIDTH*XGA_HEIGHT*4U)
#define TRACE_CAP 128U /* Placement/loans perform no I/O; catch any added waits. */
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_host_event events[TRACE_CAP];
static struct gf_production production;
static const struct gf3_test_ops *slot;
static struct gf3_service *service;
static struct gf_vram_reservation_node external;
static struct gf4_saved saved;
static struct gf2_chan wrong_channel;
static const struct gf4_mode vga={.id=1,
    .t={25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V},
    .fmt=GF3_INDEX8,.pitch=VGA_WIDTH};
static const struct gf4_mode xga32={.id=2,
    .t={65000,1024,1048,1184,1344,768,771,777,806,GF4_NEG_H|GF4_NEG_V},
    .fmt=GF3_XRGB8888,.pitch=XGA_WIDTH*4U};
static const struct gf4_mode vga16={.id=3,
    .t={25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V},
    .fmt=GF3_RGB565,.pitch=VGA_WIDTH*2U};
static void same(const struct gf3_surface *a,const struct gf3_surface *b)
{
    assert(a->off==b->off && a->bytes==b->bytes && a->pitch==b->pitch);
    assert(a->width==b->width && a->height==b->height && a->fmt==b->fmt);
    assert(a->reservation_id==b->reservation_id && a->generation==b->generation);
}
static void live(const struct gf_vram_reservation_node *n)
{
    struct gf_vram_reservation v;
    assert(!gf_vram_match(&production.chan,n,n->value.reservation_id,
        n->value.generation,n->value.off,n->value.bytes,&v));
    assert(v.owner==n);
}
static void init(void)
{
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=TRACE_CAP;
    production.chan=host.base.ch; production.foundation=host.base.f;
    production.chan.f=&production.foundation; production.chan.B=TEST_VRAM;
    production.foundation.imem.vram_bytes=TEST_VRAM;
    production.accel.ch=&production.chan;
    production.accel.ready=true; production.accel.epoch=production.chan.epoch;
    host.ctx.ch=&production.chan; host.ctx.accel=&production.accel;
    host.ctx.legacy=&production.display; production.display.display4=&host.ctx;
    host.ctx.accepting=false;
    assert(!gf3_service_bind(&production,&slot));
    service=gf3_service_get(&production.chan); assert(service);
    gf_hook_ctx_bind(&production.accel); gf_hook_scan_bind(false,0);
    state.nmodes=3; state.modes[0]=vga; state.modes[1]=xga32; state.modes[2]=vga16;
    state.head[0].mode=vga; state.head[0].enabled=true;
    assert(!gf4_scan_reserve_initial(&host.ctx,TEST_DL));
    assert(state.head[0].front.off==0 && state.head[0].front.bytes==VGA_BYTES);
    assert(host.ctx.test.value.bytes==VGA_BYTES);
    for (u8 h=0;h<GF4_HEADS;h++) {
        assert(!(host.ctx.cursor[h].value.off&(MEM_PAGE_SIZE-1U)));
        assert(host.ctx.cursor[h].value.bytes==64U*64U*4U); /* crtc.c:938-975. */
        live(&host.ctx.cursor[h]);
    }
}
static void prepare_rollback(void)
{
    struct gf3_surface out[2]={{.off=U32_MAX},{.off=U32_MAX}};
    /* The second reserve fails at runtime; first reserve must be returned. */
    host.ctx.scan_next[0][1].value.generation=U32_MAX;
    assert(gf4_scan_prepare(&host.ctx,0,xga32.id,out,TEST_DL)==GF2_ENOSPC);
    assert(out[0].off==U32_MAX && out[1].off==U32_MAX);
    assert(!host.ctx.scan_next[0][0].value.owner && !host.ctx.scan_next[0][1].value.owner);
    host.ctx.scan_next[0][1].value.generation=0;
    live(&host.ctx.scan[0][0]); live(&host.ctx.test);
    assert(gf4_scan_prepare(&host.ctx,GF4_HEADS,xga32.id,out,TEST_DL)==GF4_EINVAL);
    assert(gf4_scan_prepare(&host.ctx,0,U32_MAX,out,TEST_DL)==GF4_EINVAL);
}
static void loans_and_holds(void)
{
    struct gf3_surface pair[2];
    assert(!gf4_scan_prepare(&host.ctx,0,xga32.id,pair,TEST_DL));
    assert(pair[0].bytes==XGA32_BYTES && pair[1].bytes==XGA32_BYTES);
    assert(pair[0].pitch==XGA_WIDTH*4U && pair[0].fmt==GF3_XRGB8888);
    assert(pair[0].off>=VGA_BYTES && pair[1].off>=pair[0].off+XGA32_BYTES);
    struct gf3_surface old=state.head[0].front;
    saved.head[0]=state.head[0]; host.ctx.diagnostic_saved=&saved;
    host.ctx.changing=true; state.next[0]=state.head[0]; state.next[0].mode=xga32;
    host.ctx.hold[0]=(struct gf4_flip_hold){.pending=true,.old_front=old,.new_front=pair[0]};
    /* Corrupt only the test owner: no partial F3 scan/hold/bank commit. */
    host.ctx.test.value.owner=&external;
    assert(gf4_scan_changed(&host.ctx,0,&pair[0])==GF3_ESTATE);
    assert(!service->scan.bytes && host.ctx.scan_bank[0]==0 && host.ctx.hold[0].pending);
    same(&state.head[0].front,&old);
    host.ctx.test.value.owner=&host.ctx.test;
    assert(!gf4_scan_changed(&host.ctx,0,&pair[0]));
    same(&service->scan,&pair[0]); same(&state.next[0].back,&old);
    assert(service->test.fmt==GF3_INDEX8 && service->test.width==VGA_WIDTH);
    assert(service->test.height==VGA_HEIGHT && service->test.pitch==VGA_WIDTH);
    assert(service->test.bytes==VGA_BYTES && host.ctx.hold[0].pending);
    assert(host.ctx.scan_bank[0]==1 && !slot);
    same(&state.head[0].front,&old); /* B7 alone commits the front. */
    state.next[0].front=pair[0]; state.head[0]=state.next[0];
    host.ctx.changing=false; host.ctx.hold[0].pending=false;
    assert(gf4_scan_discard(&host.ctx,0,TEST_DL)==GF4_EBUSY); /* Saved old front. */
    live(&host.ctx.scan[0][0]);
    host.ctx.diagnostic_saved=0;
    host.ctx.hold[0].pending=true; /* Only hold now pins the old bank. */
    assert(gf4_scan_discard(&host.ctx,0,TEST_DL)==GF4_EBUSY);
    live(&host.ctx.scan[0][0]); host.ctx.hold[0].pending=false;
    assert(!gf4_scan_discard(&host.ctx,0,TEST_DL));
    assert(!host.ctx.scan[0][0].value.owner);
    /* Head1 notification must not touch the head0 service loan or bank. */
    state.head[1].mode=xga32;
    assert(!gf_vram_reserve(&production.chan,&external,TEST_VRAM-XGA32_BYTES,XGA32_BYTES,&external));
    struct gf3_surface other=pair[1]; other.off=external.value.off;
    other.reservation_id=external.value.reservation_id; other.generation=external.value.generation;
    assert(!gf4_scan_changed(&host.ctx,1,&other));
    same(&service->scan,&pair[0]);
    struct gf3_surface stale=pair[1]; ++stale.generation;
    assert(gf4_scan_changed(&host.ctx,1,&stale)==GF3_ESTATE);
    /* Real display validation rejects a mismatched format before the loan. */
    stale=pair[0]; stale.fmt=GF3_RGB565;
    assert(gf4_scan_changed(&host.ctx,0,&stale)<0);
    same(&service->scan,&pair[0]);
    state.head[1].enabled=false;
    assert(!gf4_scan_prepare(&host.ctx,0,vga16.id,pair,TEST_DL));
    assert(pair[0].bytes==VGA_BYTES*2U && pair[0].pitch==VGA_WIDTH*2U);
    state.head[0].mode=vga16;
    host.ctx.saved_closed=true;
    struct video_fb before,after; u32 bw,bh,aw,ah;
    geforce_backend_framebuffer_info(&before,&bw,&bh);
    assert(!gf4_scan_changed(&host.ctx,0,&pair[0]));
    same(&service->scan,&pair[0]); assert(!slot);
    geforce_backend_framebuffer_info(&after,&aw,&ah);
    assert(before.phys==after.phys && before.bytes==after.bytes &&
        before.pitch==after.pitch && before.format==after.format && bw==aw && bh==ah);
    host.ctx.saved_closed=false; state.head[0].front=pair[0];
}
static void release_and_capacity(void)
{
    assert(gf4_scan_release_all(&host.ctx,TEST_DL)==GF4_EBUSY);
    live(&host.ctx.test);
    assert(!gf3_service_surfaces(service,0,0));
    production.chan.state=GF2_OFF;
    /* Failure from V0 preserves the exact node; no invented release stub. */
    wrong_channel.f=&production.foundation;
    struct gf2_chan *ch=host.ctx.ch; host.ctx.ch=&wrong_channel;
    assert(gf4_scan_release_all(&host.ctx,TEST_DL)==GF3_ESTATE);
    host.ctx.ch=ch; live(&host.ctx.test);
    assert(!gf4_scan_release_all(&host.ctx,TEST_DL));
    assert(!host.ctx.test.value.owner && !state.head[0].front.bytes);
    live(&external); /* F4 never returns a caller-owned scan reservation. */
    assert(!gf_vram_release(&production.chan,&external.value));
    production.chan.state=GF2_RUN; state.head[0].mode=vga;
    /* Exactly enough for the rounded boot display plus two cursor images.
     * Test capacity alone is nonfatal; geometry/IDs still stay resident. */
    production.chan.B=((VGA_BYTES+MEM_PAGE_SIZE-1U)&~(MEM_PAGE_SIZE-1U))+2U*(64U*64U*4U);
    assert(!gf4_scan_reserve_initial(&host.ctx,TEST_DL));
    assert(host.ctx.last_rc==GF2_ENOSPC && !host.ctx.test.value.owner);
    assert(!gf4_scan_changed(&host.ctx,0,&state.head[0].front));
    assert(!service->test.bytes && !slot);
    assert(!gf3_service_surfaces(service,0,0));
    production.chan.state=GF2_OFF;
    assert(!gf4_scan_release_all(&host.ctx,TEST_DL));
    production.chan.state=GF2_RUN; production.chan.B=TEST_VRAM;
    host.ctx.test.value.generation=U32_MAX;
    assert(gf4_scan_reserve_initial(&host.ctx,TEST_DL)==GF2_ENOSPC);
    assert(host.ctx.last_rc==GF2_ENOSPC && !host.ctx.test.value.owner);
    host.ctx.test.value.generation=0;
    assert(!gf4_scan_reserve_initial(&host.ctx,TEST_DL));
    live(&host.ctx.test);
}
void _start(void)
{
    init(); prepare_rollback(); loans_and_holds(); release_and_capacity();
    assert(host.nevents==0); /* No clocks, waits, MMIO or publication in scan. */
    gf2_host_exit(0);
}
