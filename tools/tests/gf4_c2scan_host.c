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
#include "../../drivers/video/geforce/geforce_module.h"

#define TEST_VRAM (16U*1024U*1024U)
#define TEST_DL 123456U
#define VGA_WIDTH 640U
#define VGA_HEIGHT 480U
/* cpp: nouveau c03c1a1 dispnv04/crtc.c:869-883 (depth/format),
 * :889-895 (pixel displacement and cpp). Geometry: OS32 X1/N4. */
#define INDEX8_CPP (8U/8U)
#define RGB565_CPP (16U/8U)
#define XRGB8888_CPP (32U/8U)
#define VGA_BYTES (VGA_WIDTH*VGA_HEIGHT*INDEX8_CPP)
#define CURSOR_BYTES GF4_CURSOR_BYTES /* crtc.c:938-975; A0 storage size. */
#define XGA_WIDTH 1024U
#define XGA_HEIGHT 768U
#define XGA32_BYTES (XGA_WIDTH*XGA_HEIGHT*XRGB8888_CPP)
#define TRACE_CAP 128U /* Placement/loans perform no I/O; catch any added waits. */
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_host_event events[TRACE_CAP];
static struct gf_production production;
static GfxBackend scan_backend;
static struct geforce_platform scan_platform;
static int scan_height;
static GeforceModuleContext scan_module={.backend=&scan_backend,.platform=&scan_platform,.height=&scan_height};
const GeforceModuleContext *geforce_context=&scan_module;
static const struct gf3_test_ops *slot;
static struct gf3_service *service;
static struct gf_vram_reservation_node external;
static struct gf4_saved saved;
static struct gf2_chan wrong_channel;
/* These are fixture mode IDs, not hardware register values. Timing is
 * VESA VGA/XGA input data; expectations use the fixture dimensions/pitch. */
static const struct gf4_mode vga={.id=1,
    .t={25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V},
    .fmt=GF3_INDEX8,.pitch=VGA_WIDTH};
static const struct gf4_mode xga32={.id=2,
    .t={65000,1024,1048,1184,1344,768,771,777,806,GF4_NEG_H|GF4_NEG_V},
    .fmt=GF3_XRGB8888,.pitch=XGA_WIDTH*XRGB8888_CPP};
static const struct gf4_mode vga16={.id=3,
    .t={25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V},
    .fmt=GF3_RGB565,.pitch=VGA_WIDTH*RGB565_CPP};
/* Resident lifecycle boundary, not a replacement for an F3 API. scan.c
 * must never invoke START/STOP/RECOVER while only changing a value loan. */
static i32 unexpected_lifecycle(void *owner,u32 dl)
{
    (void)owner; (void)dl;
    assert(false);
    return GF3_ESTATE;
}
static const struct gf3_service_owner owner_table={
    .owner=&host.ctx,.start=unexpected_lifecycle,
    .stop=unexpected_lifecycle,.recover=unexpected_lifecycle
};
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
    assert(!gf3_service_owner_bind(service,&owner_table));
    assert(service->owner==&owner_table);
    scan_platform.display_phys=production.chan.bar1.base;
    scan_module.display_state=&state;
    gf_hook_ctx_bind(&production.accel); gf_hook_scan_bind(false,0);
    const struct gf4_mode modes[]={vga,xga32,vga16};
    state.nmodes=sizeof(modes)/sizeof(modes[0]);
    for (u32 i=0;i<state.nmodes;i++) state.modes[i]=modes[i];
    state.head[0].mode=vga; state.head[0].enabled=true;
    assert(!gf4_scan_reserve_initial(&host.ctx,TEST_DL));
    assert(state.head[0].front.off==0 && state.head[0].front.bytes==VGA_BYTES);
    assert(host.ctx.test.value.bytes==VGA_BYTES);
    for (u8 h=0;h<GF4_HEADS;h++) {
        assert(!(host.ctx.cursor[h].value.off&(MEM_PAGE_SIZE-1U)));
        assert(host.ctx.cursor[h].value.bytes==CURSOR_BYTES); /* crtc.c:938-975. */
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
    assert(pair[0].pitch==XGA_WIDTH*XRGB8888_CPP && pair[0].fmt==GF3_XRGB8888);
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
    assert(pair[0].bytes==VGA_BYTES*RGB565_CPP && pair[0].pitch==VGA_WIDTH*RGB565_CPP);
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
/* X1: use the real C1 notification while state.head.front is still old.
 * HAL format encodings belong to C1: require distinct INDEX8/16/32 formats
 * without guessing new GFX_FMT values that are not in the frozen headers. */
static void published_changes(void)
{
    struct video_fb previous,fb;
    u32 w,h;
    struct gf3_surface seed={.off=external.value.off,.bytes=VGA_BYTES,
        .pitch=vga.pitch,.width=vga.t.hd,.height=vga.t.vd,.fmt=vga.fmt,
        .reservation_id=external.value.reservation_id,.generation=external.value.generation};
    /* Establish INDEX8 metadata from a live caller-owned display range. */
    gf_hook_scan_changed(0,&seed,vga.id);
    geforce_backend_framebuffer_info(&previous,&w,&h);
    host.ctx.accepting=true;
    const struct gf4_mode *modes[]={&vga16,&xga32};
    for (u32 i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
        const struct gf4_mode *m=modes[i];
        struct gf3_surface pair[2],front=state.head[0].front;
        if (m->id!=state.head[0].mode.id) {
            assert(!gf4_scan_prepare(&host.ctx,0,m->id,pair,TEST_DL));
            front=pair[0];
        }
        struct gf3_surface old=state.head[0].front;
        state.head[0].mode=*m;
        /* Seed a live INDEX8 hook: a missing notification must leave it
         * observably open. Fully clipped calls perform no drawing or I/O. */
        gf_hook_scan_bind(true,&seed);
        assert(!geforce_fill_rect(seed.width,0,1,1,0));
        assert(!gf4_scan_changed(&host.ctx,0,&front));
        same(&service->scan,&front); same(&state.head[0].front,&old);
        geforce_backend_framebuffer_info(&fb,&w,&h);
        assert(fb.phys==production.chan.bar1.base+front.off);
        assert(fb.bytes==front.bytes && fb.pitch==m->pitch);
        assert(w==m->t.hd && h==m->t.vd && fb.format!=previous.format);
        assert(geforce_fill_rect(seed.width,0,1,1,0)==OS32_ERR_NOSYS);
        assert(geforce_blit(0,0,0,0,0,0)==OS32_ERR_NOSYS);
        previous=fb; state.head[0].front=front; /* B7 commit after notice. */
    }
    host.ctx.accepting=false; gf_hook_scan_bind(false,0);
}
static void same_reservation_flip(void)
{
    struct gf4_head_image original=state.head[0];
    /* Two VGA16 images fit within the existing caller-owned XGA32 node.
     * crtc.c:889-891: same pinned BO, distinct framebuffer displacement. */
    u32 bytes=vga16.pitch*vga16.t.vd;
    assert(bytes<=external.value.bytes/2U);
    struct gf3_surface first={.off=external.value.off,.bytes=bytes,
        .pitch=vga16.pitch,.width=vga16.t.hd,.height=vga16.t.vd,.fmt=vga16.fmt,
        .reservation_id=external.value.reservation_id,.generation=external.value.generation};
    struct gf3_surface second=first; second.off+=bytes;
    state.head[0].mode=vga16; state.head[0].front=first;
    state.head[0].back=(struct gf3_surface){0};
    assert(!gf4_scan_changed(&host.ctx,0,&second));
    same(&service->scan,&second); same(&state.head[0].back,&first);
    same(&state.head[0].front,&first); live(&external);
    state.head[0].front=second; /* B7 commits; repeat notice is a no-op. */
    assert(!gf4_scan_changed(&host.ctx,0,&second));
    same(&state.head[0].back,&first);
    state.head[0]=original;
    assert(!gf4_scan_changed(&host.ctx,0,&original.front));
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
    production.chan.B=((VGA_BYTES+MEM_PAGE_SIZE-1U)&~(MEM_PAGE_SIZE-1U))+GF4_HEADS*CURSOR_BYTES;
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
    init(); prepare_rollback(); loans_and_holds();
    published_changes(); same_reservation_flip(); release_and_capacity();
    assert(host.nevents==0); /* No clocks, waits or MMIO in scan/metadata updates. */
    gf2_host_exit(0);
}
