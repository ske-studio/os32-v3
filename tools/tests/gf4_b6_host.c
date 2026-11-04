/* B6 UNIT=6, libc-free. MIT nouveau c03c1a1 dispnv04/cursor.c:21-62,
 * crtc.c:938-1015, hw.h:341-349,372-385, nvreg.h:158-166,257-264.
 * Oracles below are literal register/pixel examples from those fields,
 * stored here so CI never reads the external reference checkout. */
#define GF4_HOST_UNIT 6
#include "gf4_display_fake.h"

enum { TRACE_CAP=256, REG_CAP=32, DEADLINE=1000,
       CURSOR_OFF=0x1083000, RAM_BYTES=CURSOR_OFF+GF4_CURSOR_BYTES,
       SIDE=64, SMALL=32, PIXELS=SIDE*SIDE, PIXEL_BYTES=4,
       IMAGE_PITCH=SIDE*PIXEL_BYTES, POS=0x680300, CONFIG=0x600810,
       TEST_IO=-91, TEST_UNMAP=-92 };
static struct gf4_host h;
static struct gf4_state state;
static struct gf4_host_event events[TRACE_CAP];
static struct gf3_host_reg regs[REG_CAP];
static u8 vram[RAM_BYTES];
static u32 pixels[PIXELS];
static struct gf4_cursor image;

static void setup(void)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf4_host_init(&h,&state);
    h.events=events; h.cevents=TRACE_CAP;
    h.base.regs=regs; h.base.cregs=REG_CAP;
    h.base.vram=vram; h.base.vram_bytes=RAM_BYTES;
    h.base.ch.bar1=(struct recover_bar_extent){.base=0x10000000,
        .length=RAM_BYTES,.valid=true};
    state.head[0].enabled=true; state.head[0].dpms=GF4_ON;
    image=(struct gf4_cursor){pixels,sizeof(pixels),IMAGE_PITCH,SIDE,SIDE,0,0};
}
static u32 reg(u32 off)
{
    u32 value=0;
    for (u32 b=0;b<PIXEL_BYTES;++b) value|=gf4_host_reg_byte(&h,0,off+b)<<(b*8U);
    return value;
}
static void visibility(void)
{
    setup();
    state.head[0].cursor_valid=true; state.head[0].cursor_visible=true;
    h.cr[0][0x31]=0xae; /* Preserve address/double-scan bits. */
    gf3_host_set(&h.base,0,POS,0xfffeffff);
    assert(!gf4_cursor_apply(&h.ctx,0,false));
    assert(state.head[0].cursor_visible && h.cr[0][0x31]==0xae);
    assert(reg(POS)==0xfffeffff);
    assert(!gf4_cursor_apply(&h.ctx,0,true));
    assert(h.cr[0][0x31]==0xaf);
    state.head[0].blank=true;
    gf4_host_trace_clear(&h);
    assert(gf4_cursor_apply(&h.ctx,0,true)==GF4_ESTATE && !h.nevents);
    assert(!gf4_cursor_show(&h.ctx,0,true));
    assert(state.head[0].cursor_visible && !(h.cr[0][0x31]&1));
    state.head[0].blank=false; state.head[0].dpms=GF4_OFF;
    assert(!gf4_cursor_show(&h.ctx,0,true));
    assert(state.head[0].cursor_visible && !(h.cr[0][0x31]&1));
    state.head[0].cursor_valid=false;
    gf4_host_trace_clear(&h);
    assert(gf4_cursor_show(&h.ctx,0,true)==GF4_ESTATE && !h.nevents);
    assert(!gf4_cursor_show(&h.ctx,0,false) && !state.head[0].cursor_visible);
    state.head[0].cursor_valid=true; state.head[0].dpms=GF4_ON;
    gf4_host_trace_clear(&h); gf4_host_fail_at(&h,D_WRITE,2,TEST_IO);
    assert(gf4_cursor_show(&h.ctx,0,true)==TEST_IO);
    assert(!state.head[0].cursor_visible && h.ctx.last_rc==TEST_IO);
    assert(h.calls[D_WRITE]==2 && h.calls[D_READ]==1);
    gf4_host_fail_at(&h,D_WRITE,0,0);
    h.ctx.changing=true; state.next[0]=state.head[0]; state.next[0].cursor_valid=false;
    assert(gf4_cursor_apply(&h.ctx,0,true)==GF4_ESTATE);
    assert(!gf4_cursor_apply(&h.ctx,0,false));
}
static void positions(void)
{
    setup(); state.head[0].hot_x=3; state.head[0].hot_y=5;
    assert(!gf4_cursor_move(&h.ctx,0,2,3)); assert(reg(POS)==0xfffeffff);
    assert(state.head[0].x==2 && state.head[0].y==3);
    assert(!gf4_cursor_move(&h.ctx,0,-32765,32772)); assert(reg(POS)==0x7fff8000);
    gf4_host_trace_clear(&h);
    assert(gf4_cursor_move(&h.ctx,0,-32766,0)==GF4_ERANGE && !h.nevents);
    assert(gf4_cursor_move(&h.ctx,0,S32_MAX,0)==GF4_ERANGE && !h.nevents);
    state.head[0].hot_x=-S32_MAX-1;
    assert(gf4_cursor_move(&h.ctx,0,S32_MAX,0)==GF4_ERANGE && !h.nevents);
    state.head[1].enabled=true; state.head[1].hot_x=1;
    assert(!gf4_cursor_move(&h.ctx,1,0,0)); assert(reg(POS+0x2000)==0x0000ffff);
    assert(gf4_cursor_move(&h.ctx,GF4_HEADS,0,0)==GF4_EINVAL);
    ++h.ctx.epoch; gf4_host_trace_clear(&h);
    assert(gf4_cursor_move(&h.ctx,0,0,0)==GF4_ESTATE && !h.nevents);
}

static void reserve_cursor(u8 head)
{
    struct gf_vram_reservation_node *node=&h.ctx.cursor[head];
    node->value=(struct gf_vram_reservation){&h.ctx,CURSOR_OFF,GF4_CURSOR_BYTES,1,1};
    h.base.reservations=node; state.head[head].cursor=node->value;
    for (u32 i=0;i<PIXELS;++i) pixels[i]=0;
    gf2_host_memset(vram+CURSOR_OFF,0x5a,GF4_CURSOR_BYTES);
}
static u32 pixel(u32 x,u32 y)
{
    u32 value=0,off=CURSOR_OFF+(y*SIDE+x)*PIXEL_BYTES;
    for (u32 b=0;b<PIXEL_BYTES;++b) value|=(u32)vram[off+b]<<(b*8U);
    return value;
}
static void observe_upload(struct gf4_host *host,const struct gf4_host_event *e)
{
    if (e->op==D_UNMAP) {
        assert(!host->state->head[0].cursor_valid);
        assert(!(host->cr[0][0x31]&1));
        assert(pixel(0,0)==0 && pixel(1,0)==0x02010101);
    }
}
static void upload(void)
{
    setup(); reserve_cursor(0);
    /* crtc.c:954-957: independently tabulated alpha endpoints. */
    static const u32 input[]={0x00000000,0x01010101,0xfe808080,0xffffffff};
    static const u32 expected[]={0x00000000,0x02010101,0xff808080,0xffffffff};
    for (u32 i=0;i<sizeof(input)/sizeof(input[0]);++i) pixels[i]=input[i];
    /* Poison both padding regions: copying beyond 32 must be observable. */
    pixels[SMALL]=0xff123456; pixels[SMALL*SIDE]=0xffabcdef;
    image.w=image.h=SMALL; image.hot_x=3; image.hot_y=5;
    state.head[0].x=2; state.head[0].y=3; state.head[0].cursor_visible=true;
    state.head[0].mode.t.flags=GF4_DBLSCAN; h.observe=observe_upload;
    assert(!gf4_cursor_image(&h.ctx,0,&image,DEADLINE));
    for (u32 y=0;y<SIDE;++y) for (u32 x=0;x<SIDE;++x) {
        u32 want=y==0 && x<sizeof(expected)/sizeof(expected[0]) ? expected[x] : 0;
        assert(pixel(x,y)==want);
    }
    /* off=0x1083000 => CR30=0x84, CR31=0x18 (+dbl=2,+enable=1), CR2f=1. */
    assert(h.cr[0][0x30]==0x84 && h.cr[0][0x31]==0x1b && h.cr[0][0x2f]==1);
    assert(reg(CONFIG)==0x04011100 && reg(POS)==0xfffeffff);
    assert(state.head[0].cursor_valid && state.head[0].cursor_visible);
    assert(state.head[0].cursor_w==SMALL && state.head[0].hot_x==3);
    assert(h.calls[D_MAP]==1 && h.calls[D_UNMAP]==1 && !h.views);
    assert(!h.ctx.cursor_view[0].mapped);
    /* Hide before map, unmap before config, position workaround read/write. */
    u32 map=TRACE_CAP,unmap=TRACE_CAP,cfg=TRACE_CAP;
    bool hidden=false,fix=false;
    for (u32 i=0;i<h.nevents;++i) {
        const struct gf4_host_event *e=&events[i];
        if (e->op==D_MAP) { map=i; assert(hidden); }
        if (e->op==D_UNMAP) unmap=i;
        if (e->op==D_WRITE && e->a[1]==GF4_HOST_CR+1 && !(e->a[3]&1)) hidden=true;
        if (e->op==D_WRITE && e->a[1]==CONFIG) cfg=i;
        if (e->op==D_READ && e->a[1]==POS && i+1<h.nevents) {
            const struct gf4_host_event *next=&events[i+1];
            assert(next->op==D_WRITE && next->a[1]==POS); fix=true;
        }
    }
    assert(map<unmap && unmap<cfg && fix);
    /* Full 64x64, including all four corners and opaque/transparent pixels. */
    setup(); reserve_cursor(0);
    pixels[0]=0xffffffff; pixels[SIDE-1]=0x01000000;
    pixels[(SIDE-1)*SIDE]=0xfe000000; pixels[PIXELS-1]=0xffffffff;
    assert(!gf4_cursor_image(&h.ctx,0,&image,DEADLINE));
    assert(pixel(0,0)==0xffffffff && pixel(SIDE-1,0)==0x02000000);
    assert(pixel(0,SIDE-1)==0xff000000 && pixel(SIDE-1,SIDE-1)==0xffffffff);
    assert(state.head[0].cursor_valid && !state.head[0].cursor_visible);
    /* A second call maps anew: successful unmap cleared the callback too. */
    assert(!gf4_cursor_image(&h.ctx,0,&image,DEADLINE));
    assert(h.calls[D_MAP]==2 && h.calls[D_UNMAP]==2);
}
static void image_rejections(void)
{
    setup(); reserve_cursor(0); state.head[0].cursor_valid=true;
    image.bytes=1;
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==GF4_ERANGE);
    assert(!h.nevents && state.head[0].cursor_valid && pixel(0,0)==0x5a5a5a5a);
    image.bytes=sizeof(pixels); image.w=SMALL;
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==GF4_ERANGE && !h.nevents);
    image.w=SIDE; image.pitch=U32_MAX-3;
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==GF4_ERANGE && !h.nevents);
    image.pitch=IMAGE_PITCH; image.argb_pm=(const u32 *)(uptr)(U32_MAX-3);
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==GF4_ERANGE && !h.nevents);
    image.argb_pm=pixels; image.hot_x=-S32_MAX-1;
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==GF4_ERANGE && !h.nevents);
    image.hot_x=0; --state.head[0].cursor.bytes;
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==GF4_ENOSPC && !h.nevents);
    ++state.head[0].cursor.bytes; ++state.head[0].cursor.off;
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==GF4_ERANGE && !h.nevents);
    --state.head[0].cursor.off; ++state.head[0].cursor.generation;
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==GF3_ESTATE);
    assert(h.calls[D_MATCH]==1 && !h.calls[D_MAP] && !h.calls[D_WRITE]);
    assert(state.head[0].cursor_valid && pixel(0,0)==0x5a5a5a5a);
    --state.head[0].cursor.generation; gf4_host_trace_clear(&h);
    assert(gf4_cursor_image(&h.ctx,0,&image,0)==GF4_ETIME);
    assert(!h.calls[D_MAP] && !h.calls[D_WRITE] && state.head[0].cursor_valid);
}

static void failures(void)
{
    /* Exhaust every register read/write boundary, not a single happy-path
     * count: derive injection positions from the successful trace. */
    setup(); reserve_cursor(0); state.head[0].cursor_visible=true;
    assert(!gf4_cursor_image(&h.ctx,0,&image,DEADLINE));
    u32 nr=h.calls[D_READ],nw=h.calls[D_WRITE];
    for (u32 kind=0;kind<2;++kind) {
        enum gf4_host_op op=kind ? D_WRITE : D_READ;
        u32 count=kind ? nw : nr;
        for (u32 nth=1;nth<=count;++nth) {
            setup(); reserve_cursor(0); state.head[0].cursor_valid=true;
            state.head[0].cursor_visible=true;
            gf4_host_fail_at(&h,op,nth,TEST_IO);
            assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==TEST_IO);
            assert(h.ctx.last_rc==TEST_IO && !h.views && !h.ctx.cursor_view[0].mapped);
            assert(h.base.reservations==&h.ctx.cursor[0]);
            if (!h.calls[D_MAP]) {
                assert(pixel(0,0)==0x5a5a5a5a && state.head[0].cursor_valid);
                assert(h.calls[op]==nth); /* Hide failure: stop without upload. */
            } else {
                assert(!state.head[0].cursor_valid && !(h.cr[0][0x31]&1));
                gf4_host_fail_at(&h,op,0,0); gf4_host_trace_clear(&h);
                assert(gf4_cursor_show(&h.ctx,0,true)==GF4_ESTATE && !h.nevents);
                assert(!gf4_cursor_image(&h.ctx,0,&image,DEADLINE));
                assert(state.head[0].cursor_valid);
            }
        }
    }
    setup(); reserve_cursor(0); state.head[0].cursor_valid=true;
    gf4_host_fail_at(&h,D_MAP,1,TEST_IO);
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==TEST_IO);
    assert(state.head[0].cursor_valid && !h.calls[D_UNMAP]);
    assert(pixel(0,0)==0x5a5a5a5a && !h.ctx.cursor_view[0].mapped);
    /* A write can reach the device even if the adapter returns failure. */
    setup(); reserve_cursor(0); state.head[0].cursor_visible=true;
    assert(!gf4_cursor_image(&h.ctx,0,&image,DEADLINE));
    u32 enable_write=0;
    for (u32 i=0;i<h.nevents;++i) {
        const struct gf4_host_event *e=&events[i];
        if (e->op==D_WRITE && e->a[1]==GF4_HOST_CR+1 && (e->a[3]&1)) enable_write=e->nth;
    }
    assert(enable_write);
    setup(); reserve_cursor(0); state.head[0].cursor_visible=true;
    gf4_host_write_applied_error(&h,enable_write,TEST_IO);
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==TEST_IO);
    assert(!state.head[0].cursor_valid && !(h.cr[0][0x31]&1));
}
static void close_lifetime(void)
{
    setup(); reserve_cursor(0); state.head[0].cursor_visible=true;
    gf4_host_fail_at(&h,D_UNMAP,1,TEST_UNMAP);
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==TEST_UNMAP);
    struct gf_vram_view *v=&h.ctx.cursor_view[0];
    assert(v->mapped && v->close==gf4_cursor_close && v->close_owner==&h.ctx);
    assert(h.views==v && h.base.reservations==&h.ctx.cursor[0]);
    assert(!state.head[0].cursor_valid && state.head[0].cursor_visible);
    assert(!(h.cr[0][0x31]&1) && reg(CONFIG)==0);
    struct gf_vram_view saved=*v;
    gf4_host_trace_clear(&h); gf4_host_fail_at(&h,D_UNMAP,0,TEST_UNMAP);
    assert(v->close(v->close_owner,v)==TEST_UNMAP);
    assert(v->mapped && v->va==saved.va && v->map_base==saved.map_base &&
           v->map_bytes==saved.map_bytes && v->ch==saved.ch && v->next==saved.next);
    assert(v->close==saved.close && v->close_owner==saved.close_owner);
    assert(h.calls[D_UNMAP]==1 && h.nevents==1 && h.views==v);
    gf4_host_trace_clear(&h);
    assert(gf4_cursor_image(&h.ctx,0,&image,DEADLINE)==GF4_EBUSY && !h.nevents);
    gf4_host_fail_at(&h,D_UNMAP,0,0);
    assert(!v->close(v->close_owner,v)); assert(!v->mapped && !v->va && !h.views);
    u32 unmaps=h.calls[D_UNMAP];
    assert(!gf4_cursor_close(&h.ctx,v) && h.calls[D_UNMAP]==unmaps);
    struct gf_vram_view other={0};
    assert(gf4_cursor_close(&h.ctx,&other)==GF4_EINVAL);
    assert(gf4_cursor_close(NULL,v)==GF4_EINVAL);
    assert(!gf4_cursor_image(&h.ctx,0,&image,DEADLINE));
    assert(state.head[0].cursor_valid && !h.views);
    assert(!gf4_cursor_close(&h.ctx,v) && state.head[0].cursor_valid);
}
void _start(void)
{
    visibility(); positions(); upload(); image_rejections(); failures(); close_lifetime();
    gf2_host_exit(0);
}
