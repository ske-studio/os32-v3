#define GF3_HOST_UNIT 9
#include "gf_accel_host.h"

static struct gf3_host h;
static struct gf3_host_event events[128];
static struct gf3_host_packet packets[16];
static u32 words[64];
static struct gf_vram_reservation_node a, b;
static struct gf3_surface src = {0x1000, 0x1000, 64, 16, 16, GF3_XRGB8888, 1, 1};
static struct gf3_surface dst = {0x3000, 0x1000, 64, 16, 16, GF3_XRGB8888, 2, 1};

static void setup(void)
{
    gf3_host_init(&h);
    h.events=events; h.cevents=128; h.packets=packets; h.cpackets=16;
    h.words=words; h.cwords=64;
    a.value=(struct gf_vram_reservation){&a,0x1000,0x1000,1,1};
    b.value=(struct gf_vram_reservation){&b,0x3000,0x1000,2,1};
    a.next=&b; b.next=0; h.reservations=&a;
}
static void packet(u32 i, u32 method, u32 count, const u32 *expected)
{
    assert(i<h.npackets && h.packets[i].method==method && h.packets[i].count==count);
    assert(h.packets[i].subc==(method==0x03fc ? GF3_SUBC_GDI : GF3_SUBC_MISC));
    assert(!h.packets[i].noninc && h.packets[i].deadline==100);
    for (u32 j=0;j<count;++j) assert(words[h.packets[i].first+j]==expected[j]);
}
void _start(void)
{
    struct gf3_rect sr={2,3,8,6}, dr={1,2,4,4}, clip={2,3,3,2};
    const u32 bind[]={GF3_HANDLE_SIFM}, dma[]={GF3_HANDLE_VRAM_DMA};
    const u32 fmt[]={4,3}, zero[]={0};
    const u32 rect[]={0x00030002,0x00020003,0x00020001,0x00040004,0x00200000,0x00180000};
    const u32 image[]={0x00100010,0x01010040,0x1000,0x00300020};
    setup();
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,true,100));
    assert(h.npackets==6 && h.calls[H_SURFACE_SET]==1 && h.calls[H_FENCE]==1 && h.calls[H_WAIT]==1);
    packet(0,0,1,bind); packet(1,0x0184,1,dma); packet(2,0x0300,2,fmt);
    packet(3,0x03fc,1,zero); packet(4,0x0308,6,rect); packet(5,0x0400,4,image);
    gf3_host_trace_clear(&h);
    sr=(struct gf3_rect){0,0,4,4}; dr=(struct gf3_rect){0,0,8,8};
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,100));
    assert(words[packets[4].first+4]==0x80000 && words[packets[5].first+1]==0x10040);
    gf3_host_trace_clear(&h);
    sr.w=3; dr.w=7;
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,100));
    assert(words[packets[4].first+4]==0x6db6d); /* floor(3*2^20/7) */
    gf3_host_trace_clear(&h);
    sr.w=4; dr.w=8;
    src.fmt=GF3_YUY2; src.pitch=32;
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,100));
    assert(words[packets[2].first]==5);
    gf3_host_trace_clear(&h);
    sr.x=1;
    assert(gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,100)==GF3_ERANGE && !h.npackets);
    sr.x=0; dst.fmt=GF3_INDEX8;
    assert(gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,100)==GF3_EFORMAT && !h.npackets);
    dst.fmt=GF3_XRGB8888;
    assert(gf3_scale(&h.ctx,&src,&src,sr,dr,clip,false,100)==GF3_EFORMAT && !h.npackets);
    src.fmt=GF3_XRGB8888; src.pitch=64;
    assert(gf3_scale(&h.ctx,&src,&src,sr,dr,clip,false,100)==GF3_EUNSUPPORTED && !h.npackets);
    sr.w=0;
    assert(!gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,100) && !h.npackets);
    sr.w=4;
    gf3_host_fail_at(&h,H_EMIT,2,GF2_EIO);
    assert(gf3_scale(&h.ctx,&src,&dst,sr,dr,clip,false,100)==GF2_EIO && h.ctx.last_rc==GF2_EIO && !h.calls[H_FENCE]);
    gf2_host_exit(0);
}
