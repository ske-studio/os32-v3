/* Link modes.c and timing.c; UNIT 0 supplies only their dependency stubs.
 * No libc. Oracles are independent literals from MIT nouveau c03c1a1:
 * dispnv04/tvmodesnv17.c:439-467 LD, :149-215 CTV;
 * dispnv04/crtc.c:245-256,350-413 CRTC field assignment;
 * dispnv04/tvnv17.c:310-349 validity. No external checkout at runtime. */
#define GF4_HOST_UNIT 0
#include "gf4_display_fake.h"

/* Norm indices: tvmodesnv17.c:33-45. */
enum { PAL=0, PAL_M=1, PAL_N=2, PAL_NC=3, NTSC_M=4, NTSC_J=5,
       HD480I=6, HD480P=7, HD576I=8, HD576P=9, HD720P=10, HD1080I=11 };
enum { DEPTHS=3, VRAM_BYTES=16U*1024U*1024U, EVENT_CAPACITY=256, DL=123456 };
static struct gf4_host h;
static struct gf4_state state;
static struct gf4_host_event events[EVENT_CAPACITY];
static struct gf4_route route;
static struct gf_vram_reservation_node node;
/* M:439-467 and T:228-247: clock from norm mHz, ht and logical vt,
 * with truncation at each /1000. Preferred NTSC-M 720x480. */
static const struct gf4_timing ntsc={
    (59940U*960U/1000U)*525U/1000U,720,752,872,960,480,480,493,525,
    GF4_NEG_H|GF4_NEG_V
};
static const struct gf4_timing pal={
    (50000U*960U/1000U)*597U/1000U,720,776,856,960,576,576,588,597,
    GF4_NEG_H|GF4_NEG_V
};
static const struct gf4_timing ctv720={74250,1280,1349,1357,1650,720,725,730,750,0};
/* C:245-256 encodes 1349/8+1=169,1357/8+1=170,1650/8-5=201.
 * These literals are the independently evaluated CRTC assignment. */
enum { CTV720_HS=169, CTV720_HE=170, CTV720_HT=201,
       NTSC_VDISPLAY=479, NTSC_VSYNC=479, NTSC_VEND=492 };
static void fresh(u32 norm,struct gf4_timing t)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf4_host_init(&h,&state);
    h.events=events; h.cevents=EVENT_CAPACITY;
    state.version=GF4_STATE_VERSION; state.size=sizeof(state);
    h.ctx.accepting=false; h.ctx.next_mode_id=1;
    h.ctx.limits_valid[0]=true; h.base.ch.B=VRAM_BYTES;
    route=(struct gf4_route){.type=GF4_TV,.head_mask=1,.or_mask=1,.tv_norm=norm};
    h.ctx.routes=&route; h.ctx.nroutes=1;
    h.tv_modes=(struct gf4_edid_state){.version=GF4_EDID_VERSION,
        .size=sizeof(h.tv_modes),.ntiming=1,.preferred=0};
    h.tv_modes.timing[0]=t;
}
static void same_timing(const struct gf4_timing *a,const struct gf4_timing *b)
{
    assert(a->khz==b->khz && a->hd==b->hd && a->hs==b->hs && a->he==b->he &&
        a->ht==b->ht && a->vd==b->vd && a->vs==b->vs && a->ve==b->ve &&
        a->vt==b->vt && a->flags==b->flags);
}
static void admitted(u32 norm,struct gf4_timing t)
{
    fresh(norm,t);
    assert(!gf4_detect(&h.ctx,DL) && state.nmodes==DEPTHS);
    assert(h.calls[D_TV_MODES]==1 && !h.calls[D_EDID] && !h.calls[D_WRITE]);
    const struct gf4_mode *m=NULL; u32 n=0;
    assert(!gf4_modes(&h.ctx,0,&m,&n) && n==DEPTHS);
    for (u32 i=0;i<n;i++) {
        same_timing(&m[i].t,&t);
        assert(m[i].fmt==(enum gf3_fmt)i);
    }
}
static void rejected(u32 norm,struct gf4_timing t)
{
    fresh(norm,t);
    assert(!gf4_detect(&h.ctx,DL) && !state.nmodes);
    assert(!h.calls[D_LOAD] && !h.calls[D_EDID] && !h.calls[D_WRITE]);
}
static void timing_and_surface(void)
{
    admitted(NTSC_M,ntsc);
    struct gfinit_mode out={0};
    assert(!gf4_timing_calc(&state.modes[0],&out));
    assert(out.timing.vd==NTSC_VDISPLAY && out.timing.vs==NTSC_VSYNC &&
        out.timing.ve==NTSC_VEND);
    /* 720x480 INDEX8, pitch aligned to 64, independently computed extent. */
    enum { LD_PITCH=768, LD_BYTES=LD_PITCH*480 };
    node=(struct gf_vram_reservation_node){.value={&h.ctx,0,LD_BYTES,7,3}};
    h.base.reservations=&node;
    struct gf3_surface s={0,LD_BYTES,LD_PITCH,720,480,GF3_INDEX8,7,3, 0};
    assert(!gf4_surface_check(&h.ctx,&s,&state.modes[0]));
    admitted(HD720P,ctv720);
    assert(!gf4_timing_calc(&state.modes[0],&out));
    assert(out.timing.hs==CTV720_HS && out.timing.he==CTV720_HE &&
        out.timing.ht==CTV720_HT);
    /* Preserve the non-character-aligned source through surface validation. */
    enum { HD_PITCH=1280, HD_BYTES=HD_PITCH*720 };
    node=(struct gf_vram_reservation_node){.value={&h.ctx,0,HD_BYTES,7,3}};
    h.base.reservations=&node;
    s=(struct gf3_surface){0,HD_BYTES,HD_PITCH,1280,720,GF3_INDEX8,7,3,0};
    assert(!gf4_surface_check(&h.ctx,&s,&state.modes[0]));
    struct gf4_mode bad=state.modes[0];
    bad.t.vs=bad.t.vd-1U;
    struct gfinit_mode before=out;
    assert(gf4_timing_calc(&bad,&out)==GF4_ERANGE);
    const u8 *a=(const u8 *)&out,*b=(const u8 *)&before;
    for (u32 i=0;i<sizeof(out);i++) assert(a[i]==b[i]);
    assert(gf4_surface_check(&h.ctx,&s,&bad)==GF4_ERANGE);
}
static void norms_and_limits(void)
{
    static const u32 ld60[]={PAL_M,NTSC_M,NTSC_J,HD480I};
    static const u32 ld50[]={PAL,PAL_N,PAL_NC,HD576I};
    for (u32 i=0;i<sizeof(ld60)/sizeof(ld60[0]);i++) admitted(ld60[i],ntsc);
    for (u32 i=0;i<sizeof(ld50)/sizeof(ld50[0]);i++) admitted(ld50[i],pal);
    rejected(PAL,ntsc); rejected(NTSC_M,pal);
    /* T:334-339 uses rounded Hz: [59.5,60.5) becomes 60 and fits
     * NTSC 59940 +/-600mHz; PAL uses [49.5,50.5). One million pixels
     * makes each kHz exactly one mHz, so endpoints need no product formula. */
    struct gf4_timing t=ntsc; t.ht=t.vt=1000;
    t.khz=59500; admitted(NTSC_M,t);
    t.khz=59499; rejected(NTSC_M,t);
    t.khz=60499; admitted(NTSC_M,t);
    t.khz=60500; rejected(NTSC_M,t);
    t.khz=49500; admitted(PAL,t);
    t.khz=49499; rejected(PAL,t);
    t.khz=50499; admitted(PAL,t);
    t.khz=50500; rejected(PAL,t);
    /* LD clock maximum 70000, T:331; this raster rounds to 60Hz. */
    t.vt=1167; t.khz=70000; admitted(NTSC_M,t);
    t.khz=70001; rejected(NTSC_M,t);
    t=ntsc; t.flags|=GF4_INTERLACE; rejected(NTSC_M,t);
    /* M:439-447 320x240, normalized DBLSCAN vt=526,vd=vs=480. */
    t=(struct gf4_timing){(59940U*560U/1000U)*263U/1000U*2U,
        320,344,392,560,480,480,492,526,GF4_NEG_H|GF4_NEG_V|GF4_DBLSCAN};
    admitted(NTSC_M,t);
    t=ctv720; t.khz=400000; admitted(HD720P,t);
    t.khz=400001; rejected(HD720P,t);
    t=ctv720; t.hd++; rejected(HD720P,t);
    t=ctv720; t.vd++; rejected(HD720P,t);
    t=ctv720; t.flags=GF4_INTERLACE; rejected(HD720P,t);
    t.flags=GF4_DBLSCAN; rejected(HD720P,t);
    /* M:149-170 native progressive sizes, and :197-202 normalized 1080i. */
    t=(struct gf4_timing){27000,720,735,743,858,480,490,494,525,0};
    admitted(HD480P,t);
    t=(struct gf4_timing){27000,720,730,738,864,576,581,585,625,0};
    admitted(HD576P,t);
    t=(struct gf4_timing){74250,1920,1961,2049,2200,540,542,544,562,GF4_INTERLACE};
    admitted(HD1080I,t);
    t.flags=0; rejected(HD1080I,t);
    t.flags=GF4_INTERLACE; t.vd=541; rejected(HD1080I,t);
    /* TV candidates use mode_valid, not VGA wrapped blank/sync limits
     * or EDID range restrictions. T:315-328 imposes neither. */
    t=ctv720; t.ve=749;
    admitted(HD720P,t);
    fresh(HD720P,ctv720); h.tv_modes.range_valid=true; h.tv_modes.max_khz=1;
    assert(!gf4_detect(&h.ctx,DL) && state.nmodes==DEPTHS);
}
static void failures(void)
{
    fresh(NTSC_M,ntsc);
    gf4_host_fail_at(&h,D_TV_MODES,1,GF4_ERANGE);
    assert(gf4_detect(&h.ctx,DL)==GF4_ERANGE && !state.nmodes);
    assert(!h.calls[D_LOAD] && !h.calls[D_EDID] && !h.calls[D_WRITE]);
    fresh(NTSC_M,ntsc);
    gf4_host_fail_at(&h,D_LOAD,1,GF2_EIO);
    assert(gf4_detect(&h.ctx,DL)==GF2_EIO && !state.nmodes);
    assert(h.calls[D_LOAD]==1 && !h.calls[D_WRITE] && !h.calls[D_SURFACE]);
    assert(h.ctx.ready && !h.ctx.accepting && !h.ctx.hold[0].pending &&
        !h.ctx.hold[1].pending && !h.base.reservations);
}
void _start(void)
{
    timing_and_surface(); norms_and_limits(); failures();
    __asm__ volatile("int $0x80" : : "a"(1),"b"(0));
    __builtin_unreachable();
}
