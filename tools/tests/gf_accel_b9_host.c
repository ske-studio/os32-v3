#define GF3_HOST_UNIT 9
#include "gf_accel_host.h"

/* Independent SIFM expectations: xf86-video-nouveau/src/nv04_xv_blit.c:
 * 83-93, 99-154; hwdefs/nv01_2d.xml.h:890-934. */
enum { TEST_BIND = 0x0000, TEST_DMA_IMAGE = 0x0184,
       TEST_FORMAT = 0x0300, TEST_GDI_COLOR1 = 0x03fc,
       TEST_CLIP_POINT = 0x0308, TEST_IMAGE = 0x0400,
       TEST_XRGB_FORMAT = 4, TEST_YUY2_FORMAT = 5, TEST_SRCCOPY = 3,
       TEST_ORIGIN_CENTER = 0x00010000, TEST_BILINEAR = 0x01000000,
       TEST_FRAC_BITS = 20, TEST_POINT_FRAC_BITS = 4, TEST_FIELD_BITS = 16,
       TEST_DEADLINE = 100, TEST_EMITS = 6 };
/* XY and dimensions pack the second component into the upper 16 bits;
 * source coordinates are 12.4 fixed point and ratios are 12.20. */
#define TEST_PAIR(lo, hi) ((u32)(lo) | ((u32)(hi) << TEST_FIELD_BITS))
#define TEST_RATIO(n, d) (((u32)(n) << TEST_FRAC_BITS) / (d))

static struct gf3_host h;
static struct gf3_host_event events[128];
static struct gf3_host_packet packets[16];
static u32 words[64];
static struct gf_vram_reservation_node a, b;
static struct gf3_surface src = {0x1000, 0x1000, 64, 16, 16, GF3_XRGB8888, 1, 1, 0};
static struct gf3_surface dst = {0x3000, 0x1000, 64, 16, 16, GF3_XRGB8888, 2, 1, 0};

static void observe(struct gf3_host *host, const struct gf3_host_event *e)
{
    if (e->op == H_SURFACE_SET) {
        assert(e->p[0] == &host->ctx && e->p[1] == &dst && e->p[2] == &dst);
        assert(!e->a[0] && e->a[1] == TEST_DEADLINE);
        assert(!host->calls[H_EMIT]);
    }
    if (e->op == H_EMIT)
        assert(host->calls[H_SURFACE_SET] == 1);
}
static void setup(void)
{
    gf3_host_init(&h);
    h.events=events; h.cevents=128; h.packets=packets; h.cpackets=16;
    h.words=words; h.cwords=64; h.observe=observe;
    a.value=(struct gf_vram_reservation){&a,0x1000,0x1000,1,1};
    b.value=(struct gf_vram_reservation){&b,0x3000,0x1000,2,1};
    a.next=&b; b.next=0; h.reservations=&a;
}
static void packet(u32 i, u32 method, u32 count, const u32 *expected)
{
    assert(i<h.npackets && h.packets[i].method==method && h.packets[i].count==count);
    assert(h.packets[i].subc==(method==TEST_GDI_COLOR1 ? GF3_SUBC_GDI : GF3_SUBC_MISC));
    assert(!h.packets[i].noninc && h.packets[i].deadline==TEST_DEADLINE);
    for (u32 j=0;j<count;++j) assert(words[h.packets[i].first+j]==expected[j]);
}
void _start(void)
{
    struct gf3_rect sr={2,3,8,6}, dr={1,2,4,4}, clip={2,3,3,2};
    const u32 bind[]={GF3_HANDLE_SIFM}, dma[]={GF3_HANDLE_VRAM_DMA};
    const u32 fmt[]={TEST_XRGB_FORMAT,TEST_SRCCOPY}, zero[]={0};
    const u32 rect[]={TEST_PAIR(clip.x,clip.y),TEST_PAIR(clip.w,clip.h),
                      TEST_PAIR(dr.x,dr.y),TEST_PAIR(dr.w,dr.h),
                      TEST_RATIO(sr.w,dr.w),TEST_RATIO(sr.h,dr.h)};
    const u32 image[]={TEST_PAIR(src.width,src.height),
                       src.pitch | TEST_ORIGIN_CENTER | TEST_BILINEAR, src.off,
                       TEST_PAIR(sr.x << TEST_POINT_FRAC_BITS, sr.y << TEST_POINT_FRAC_BITS)};
    setup();
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,true,TEST_DEADLINE));
    assert(h.npackets==6 && h.calls[H_SURFACE_SET]==1 && h.calls[H_FENCE]==1 && h.calls[H_WAIT]==1);
    packet(0,TEST_BIND,1,bind); packet(1,TEST_DMA_IMAGE,1,dma); packet(2,TEST_FORMAT,2,fmt);
    packet(3,TEST_GDI_COLOR1,1,zero); packet(4,TEST_CLIP_POINT,6,rect); packet(5,TEST_IMAGE,4,image);
    gf3_host_trace_clear(&h);
    sr=(struct gf3_rect){0,0,4,4}; dr=(struct gf3_rect){0,0,8,8};
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,TEST_DEADLINE));
    assert(words[packets[4].first+4]==TEST_RATIO(sr.w,dr.w) && words[packets[5].first+1]==(src.pitch | TEST_ORIGIN_CENTER));
    gf3_host_trace_clear(&h);
    sr.w=3; dr.w=7;
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,TEST_DEADLINE));
    assert(words[packets[4].first+4]==TEST_RATIO(sr.w,dr.w));
    gf3_host_trace_clear(&h);
    sr.w=4; dr.w=8;
    src.fmt=GF3_YUY2; src.pitch=64;
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,TEST_DEADLINE));
    assert(words[packets[2].first]==TEST_YUY2_FORMAT);
    gf3_host_trace_clear(&h);
    sr.x=1;
    assert(gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,TEST_DEADLINE)==GF3_ERANGE && !h.npackets);
    sr.x=0; dst.fmt=GF3_INDEX8;
    assert(gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,TEST_DEADLINE)==GF3_EFORMAT && !h.npackets);
    dst.fmt=GF3_XRGB8888;
    assert(gf3_scale(&h.ctx,&src,&src,sr,dr,clip,false,TEST_DEADLINE)==GF3_EFORMAT && !h.npackets);
    src.fmt=GF3_XRGB8888; src.pitch=64;
    assert(gf3_scale(&h.ctx,&src,&src,sr,dr,clip,false,TEST_DEADLINE)==GF3_EUNSUPPORTED && !h.npackets);
    sr.w=0;
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,TEST_DEADLINE) && !h.npackets);
    sr.w=4;
    /* Each dependency failure must stop submissions and propagate its rc. */
    for (u32 stage=0; stage<TEST_EMITS+4; ++stage) {
        enum gf3_host_op op;
        u32 nth=1, emits=0, fences=0, waits=0;
        setup();
        if (stage==0) op=H_FAULT;
        else if (stage==1) op=H_SURFACE_SET;
        else if (stage<TEST_EMITS+2) {
            op=H_EMIT; nth=stage-1; emits=nth;
        } else if (stage==TEST_EMITS+2) {
            op=H_FENCE; emits=TEST_EMITS; fences=1;
        } else {
            op=H_WAIT; emits=TEST_EMITS; fences=1; waits=1;
        }
        gf3_host_fail_at(&h,op,nth,GF2_EIO);
        assert(gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,TEST_DEADLINE)==GF2_EIO);
        assert(h.ctx.last_rc==GF2_EIO && h.calls[op]==nth);
        assert(h.calls[H_EMIT]==emits && h.calls[H_FENCE]==fences && h.calls[H_WAIT]==waits);
        assert(h.npackets==emits-(op==H_EMIT));
    }
    gf2_host_exit(0);
}
