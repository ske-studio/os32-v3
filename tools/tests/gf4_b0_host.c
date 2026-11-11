/* Link hwdev/gf/display/modes.c; freestanding UNIT 0, no libc.
 * Independent oracles: GF_FEAT4_DESIGN §3 VGA fallback / final B0 card;
 * MIT nouveau c03c1a1 dispnv04/crtc.c:245-413 (CR field limits),
 * :838-903 (pitch and base). Expected VGA byte extents below are literal
 * width * height * cpp, not values read from the product under test. */
#define GF4_HOST_UNIT 0
#include "gf4_display_fake.h"
#include "kprintf.h"
#include <stdarg.h>

i32 gf4_edid_modes(const u8 [128],u32,struct gf4_edid_state *);

#define B0_DEPTHS 3U /* §3: INDEX8, RGB565, XRGB8888. */
#define B0_MIXED_TIMINGS 3U /* The explicit xga/svga/vga list below. */
#define B0_DL 123456U
#define B0_VRAM (16U*1024U*1024U)
#define B0_VGA8_BYTES 307200U
#define B0_VGA16_BYTES 614400U
#define B0_VGA32_BYTES 1228800U
/* Room for every route's full timing list and both head PLL attempts. */
#define B0_EVENTS (32U*(GF4_TIMINGS+1U)*3U*2U+256U)
static struct gf4_host h;
static struct gf4_state state;
static struct gf4_host_event events[B0_EVENTS];
static struct gf4_route routes[32];
static struct gf_vram_reservation_node node;
static u8 block[GF4_HOST_EDID_BYTES];
static const struct gf4_timing vga={25175,640,656,752,800,480,490,492,525,3};
/* VESA DMT 800x600@60 and 1024x768@60, in generator-normalized units. */
static const struct gf4_timing svga={40000,800,840,968,1056,600,601,605,628,0};
static const struct gf4_timing xga={65000,1024,1048,1184,1344,768,771,777,806,3};
static bool reject_head0,reject_port1,slow_reads,dead_port0;
static u32 timing_logs,vram_logs,pll_logs,ddc_logs,parser_logs,probe_logs;
void __cdecl kprintf(u8 attr,const char *fmt,...)
{
    (void)attr;
    va_list ap;
    va_start(ap,fmt);
    if (fmt[6]=='r') {
        for (u32 i=0;i<6U;i++) (void)va_arg(ap,u32);
        const char *reason=va_arg(ap,const char *);
        i32 rc=va_arg(ap,i32);
        assert(rc<0);
        if (reason[0]=='t') timing_logs++;
        else if (reason[0]=='V') vram_logs++;
        else if (reason[0]=='P') pll_logs++;
        else assert(false);
    } else {
        bool parser=false;
        for (u32 i=0;fmt[i];i++) if (fmt[i]=='p' && fmt[i+1]=='a') parser=true;
        bool probe=false;
        for (u32 i=0;fmt[i];i++) if (fmt[i]=='p' && fmt[i+1]=='r') probe=true;
        if (parser) parser_logs++; else if (probe) probe_logs++; else ddc_logs++;
    }
    va_end(ap);
}
static void observe(struct gf4_host *host,const struct gf4_host_event *e)
{
    assert(host==&h);
    if (dead_port0 && e->op==D_EDID && e->p[0]!=&h.ctx) {
        struct gf4_edid_state *out=(struct gf4_edid_state *)e->p[1];
        out->base_valid=true; out->ntiming=1; out->timing[0]=svga;
        out->extensions=2;
    }
    if (e->op==D_LOAD) {
        const struct gf4_route *r=e->p[1];
        assert(r && r->head<2U);
        assert(e->p[3]==&h.ctx.limits[r->head]);
        if (reject_head0 && !r->head)
            ((struct gf4_host_event *)e)->rc=GF4_EPLL;
    }
    if (reject_port1 && e->op==D_I2C_PORT && e->a[0]==1U)
        ((struct gf4_host_event *)e)->rc=GF4_ENODEV;
    if (e->op==D_PROBE) {
        assert(e->p[0]==&h.ctx && e->a[2]==(u32)*h.now+GF2_TIMEOUT_US);
    }
    if (e->op==D_EDID && e->p[0]==&h.ctx) {
        assert(h.calls[D_PROBE]);
        assert(e->a[2]==(u32)*h.now+GF2_TIMEOUT_US);
        if (slow_reads) {
            *h.now+=GF2_TIMEOUT_US-1U;
            if (dead_port0 && !e->a[0]) {
                ++*h.now;
                ((struct gf4_host_event *)e)->rc=GF4_ETIME;
            }
        }
    }
}
static void fresh(u32 nroutes)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf4_host_init(&h,&state);
    h.events=events; h.cevents=B0_EVENTS; h.observe=observe;
    h.ctx.accepting=false; h.ctx.next_mode_id=1;
    h.ctx.limits_valid[0]=h.ctx.limits_valid[1]=true;
    h.base.ch.B=B0_VRAM;
    state.version=GF4_STATE_VERSION; state.size=sizeof(state);
    state.edid=(struct gf4_edid_state){.version=GF4_EDID_VERSION,
        .size=sizeof(state.edid),.base_valid=true};
    h.ddc_block=block; h.ctx.routes=routes; h.ctx.nroutes=nroutes;
    for (u32 i=0;i<nroutes;i++) routes[i]=(struct gf4_route){
        .connector=(u8)i,.type=GF4_ANALOG,.head_mask=3,.or_mask=1
    };
    reject_head0=reject_port1=slow_reads=dead_port0=false;
    timing_logs=vram_logs=pll_logs=ddc_logs=parser_logs=probe_logs=0;
}
static void expect_vga(const struct gf4_mode *m,u32 n)
{
    static const u32 pitches[]={640,1280,2560};
    assert(n==B0_DEPTHS);
    for (u32 i=0;i<n;i++) {
        assert(m[i].id && m[i].fmt==(enum gf3_fmt)i && m[i].pitch==pitches[i]);
        assert(m[i].t.khz==25175 && m[i].t.hd==640 && m[i].t.hs==656 &&
            m[i].t.he==752 && m[i].t.ht==800 && m[i].t.vd==480 &&
            m[i].t.vs==490 && m[i].t.ve==492 && m[i].t.vt==525 && m[i].t.flags==3);
    }
}
static void fallback_and_ids(void)
{
    fresh(1);
    const struct gf4_mode *m=NULL;
    u32 n=777;
    assert(gf4_modes(&h.ctx,0,&m,&n)==GF4_ESTATE && !m && n==777);
    assert(!gf4_detect(&h.ctx,B0_DL));
    assert(!gf4_modes(&h.ctx,0,&m,&n));
    expect_vga(m,n);
    assert(m[0].id==1 && m[2].id==3);
    u32 calls=h.nevents;
    assert(!gf4_modes(&h.ctx,0,&m,&n) && h.nevents==calls);
    assert(gf4_modes(&h.ctx,99,&m,&n)==GF4_ENODEV && n==3);
    assert(!gf4_detect(&h.ctx,B0_DL));
    assert(!gf4_modes(&h.ctx,0,&m,&n) && m[0].id==4);
    h.ctx.next_mode_id=U32_MAX;
    assert(gf4_detect(&h.ctx,B0_DL)==GF4_ENOSPC && !state.nmodes && !h.ctx.next_mode_id);
    assert(gf4_modes(&h.ctx,0,&m,&n)==GF4_ESTATE && n==3);
    assert(gf4_detect(&h.ctx,B0_DL)==GF4_ENOSPC && !state.nmodes);
}
static void candidates_and_heads(void)
{
    fresh(2);
    /* Deliberately unsorted routes; duplicate connector, two PLL heads. */
    routes[0].connector=9; routes[1].connector=2;
    state.edid.ntiming=3; state.edid.preferred=1;
    state.edid.timing[0]=svga; state.edid.timing[1]=xga; state.edid.timing[2]=vga;
    reject_head0=true;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==2U*B0_MIXED_TIMINGS*B0_DEPTHS);
    for (u32 i=0;i<state.nmodes;i++) assert(state.mode_connector[i]==(i<B0_MIXED_TIMINGS*B0_DEPTHS?2U:9U));
    assert(state.modes[0].t.hd==1024 && state.modes[3].t.hd==800 && state.modes[6].t.hd==640);
    assert(h.calls[D_LOAD]==2U*2U*B0_MIXED_TIMINGS*B0_DEPTHS); /* Every candidate tries both heads. */
    fresh(2);
    routes[1].connector=0; routes[0].max_khz=20000;
    state.edid.ntiming=1; state.edid.timing[0]=svga;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==2U*B0_DEPTHS);
    assert(h.calls[D_LOAD]==2U*B0_DEPTHS); /* Second route, zero encoder ceiling. */
    fresh(2); routes[1].connector=0;
    routes[0].i2c_index=1; routes[1].i2c_index=2; reject_port1=true;
    assert(!gf4_detect(&h.ctx,B0_DL) && h.calls[D_I2C_PORT]==2);
    bool read_second=false;
    for (u32 i=0;i<h.nevents;i++) if (events[i].op==D_EDID && events[i].p[0]==&h.ctx) {
        assert(events[i].a[0]==2); read_second=true;
    }
    assert(read_second);
    fresh(1); routes[0].i2c_index=0xff;
    assert(!gf4_detect(&h.ctx,B0_DL) && h.ctx.last_rc==GF4_ENODEV && !h.calls[D_EDID]);
    expect_vga(state.modes,state.nmodes);
    fresh(1); h.ctx.limits_valid[0]=h.ctx.limits_valid[1]=false;
    assert(!gf4_detect(&h.ctx,B0_DL) && !state.nmodes && !h.calls[D_LOAD]);
    fresh(1); h.base.ch.B=B0_VGA16_BYTES;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==2);
    assert(state.modes[0].fmt==GF3_INDEX8 && state.modes[1].fmt==GF3_RGB565);
    fresh(1); h.base.ch.B=B0_VGA8_BYTES-1U;
    assert(!gf4_detect(&h.ctx,B0_DL) && !state.nmodes);
}
static void failures(void)
{
    fresh(1);
    gf4_host_fail_at(&h,D_EDID,1,GF4_EDDC);
    assert(!gf4_detect(&h.ctx,B0_DL) && h.ctx.last_rc==GF4_EDDC);
    expect_vga(state.modes,state.nmodes);
    assert(h.calls[D_EDID]==1); /* No parser/extension after failed read. */
    fresh(1); gf4_host_fail_at(&h,D_EDID,2,GF4_EEDID);
    assert(!gf4_detect(&h.ctx,B0_DL) && h.ctx.last_rc==GF4_EEDID);
    expect_vga(state.modes,state.nmodes);
    fresh(1); state.edid.ntiming=1; state.edid.timing[0]=svga; state.edid.extensions=1;
    gf4_host_fail_at(&h,D_EDID,3,GF4_EDDC);
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==2U*B0_DEPTHS && h.ctx.last_rc==GF4_EDDC);
    assert(h.calls[D_EDID]==3 && state.modes[0].t.hd==800);
    fresh(1); gf4_host_fail_at(&h,D_LOAD,1,GF2_EIO);
    assert(gf4_detect(&h.ctx,B0_DL)==GF2_EIO && !state.nmodes);
    assert(h.calls[D_LOAD]==1 && !h.calls[D_WRITE]);
    assert(h.ctx.ready && !h.ctx.accepting && !h.ctx.hold[0].pending);
    gf4_host_fail_at(&h,D_LOAD,0,0);
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==B0_DEPTHS);
    fresh(1); state.nmodes=1; state.head[1].enabled=true;
    assert(gf4_detect(&h.ctx,B0_DL)==GF4_EBUSY && state.nmodes==1 && !h.nevents);
    state.head[1].enabled=false; h.ctx.hold[0].pending=true;
    assert(gf4_detect(&h.ctx,B0_DL)==GF4_EBUSY && !h.nevents);
    h.ctx.hold[0].pending=false; h.ctx.accepting=true;
    assert(gf4_detect(&h.ctx,B0_DL)==GF4_EBUSY && !h.nevents);
    h.ctx.accepting=false; h.ctx.epoch++;
    assert(gf4_detect(&h.ctx,B0_DL)==GF4_ESTATE && !h.nevents);
}
static void ranges_and_tv(void)
{
    fresh(1); state.edid.ntiming=2;
    state.edid.timing[0]=svga; state.edid.timing[1]=xga;
    state.edid.range_valid=true; state.edid.max_khz=40000;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==2U*B0_DEPTHS && state.modes[0].t.hd==800);
    fresh(1); state.edid.ntiming=1; state.edid.timing[0]=svga;
    state.edid.timing[0].hs++; /* C:245-250: retain pixels, truncate only at CRTC encoding. */
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==2U*B0_DEPTHS);
    assert(state.modes[0].t.hs==svga.hs+1U);
    /* C:245-256,348-353: subtract encoded endpoints, not raw pixel
     * differences. 1824/8 - 801/8 = 128 exceeds the 7-bit blank span. */
    fresh(1); state.edid.ntiming=1; state.edid.timing[0]=svga;
    state.edid.timing[0].hd=801; state.edid.timing[0].ht=1824;
    assert(!gf4_detect(&h.ctx,B0_DL)); expect_vga(state.modes,state.nmodes);
    /* HRE span similarly: 1096/8 - 841/8 = 32 exceeds 5 bits. */
    fresh(1); state.edid.ntiming=1; state.edid.timing[0]=svga;
    state.edid.timing[0].hs=841; state.edid.timing[0].he=1096;
    state.edid.timing[0].ht=1200;
    assert(!gf4_detect(&h.ctx,B0_DL)); expect_vga(state.modes,state.nmodes);
    fresh(1); state.edid.ntiming=1; state.edid.timing[0]=xga;
    state.edid.range_valid=true; state.edid.min_v_hz=61;
    assert(!gf4_detect(&h.ctx,B0_DL) && !state.nmodes);
    fresh(1); state.edid.ntiming=2;
    state.edid.timing[0]=xga; state.edid.timing[1]=svga;
    state.edid.range_valid=true; state.edid.min_h_khz=37; state.edid.max_h_khz=38;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==B0_DEPTHS && state.modes[0].t.hd==800);
    /* A first timing that cannot fit any depth does not claim mandatory rows. */
    fresh(1); state.edid.ntiming=2;
    state.edid.timing[0]=xga; state.edid.timing[1]=vga; h.base.ch.B=B0_VGA8_BYTES;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==1 && state.modes[0].t.hd==640);
    fresh(1); routes[0].type=GF4_TV; routes[0].tv_norm=10; /* M:180-185 HD720P fits 800x600. */
    h.tv_modes.ntiming=1; h.tv_modes.timing[0]=svga;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==B0_DEPTHS);
    assert(h.calls[D_TV_MODES]==1 && !h.calls[D_EDID] && state.modes[0].t.hd==800);
    assert(state.edid_connector==routes[0].connector);
}
static void fairness(void)
{
    fresh(32);
    state.edid.ntiming=GF4_TIMINGS;
    /* Distinct clocks, identical representable raster: synthetic parser
     * candidates isolate the table capacity policy from timing generation. */
    for (u32 i=0;i<GF4_TIMINGS;i++) {
        state.edid.timing[i]=vga;
        state.edid.timing[i].khz+=i;
    }
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==GF4_MODES && state.edid.truncated);
    assert(state.edid_connector==31);
    for (u32 i=0;i<32U;i++) {
        const struct gf4_mode *m; u32 n;
        assert(!gf4_modes(&h.ctx,(u8)i,&m,&n));
        expect_vga(m,n); /* Mandatory first timing survives for every connector. */
    }
    fresh(2); state.edid.ntiming=GF4_TIMINGS;
    for (u32 i=0;i<GF4_TIMINGS;i++) {
        state.edid.timing[i]=vga; state.edid.timing[i].khz+=i;
    }
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==GF4_MODES);
    const struct gf4_mode *m; u32 n;
    assert(!gf4_modes(&h.ctx,0,&m,&n) && n==GF4_MODES-B0_DEPTHS);
    assert(!gf4_modes(&h.ctx,1,&m,&n)); expect_vga(m,n);
}
/* The first port consumes a whole budget; the next connector still reads
 * its base and extensions. A slow successful block must not shorten the
 * following block, including across the u32 clock wrap. */
static void block_budgets(void)
{
    fresh(2); routes[1].i2c_index=1;
    state.edid.ntiming=1; state.edid.timing[0]=svga; state.edid.extensions=2;
    slow_reads=dead_port0=true; *h.now=0xffff0000U;
    assert(!gf4_detect(&h.ctx,1)); /* Expired lifecycle dl is irrelevant. */
    const struct gf4_mode *m; u32 n;
    assert(!gf4_modes(&h.ctx,0,&m,&n)); expect_vga(m,n);
    assert(!gf4_modes(&h.ctx,1,&m,&n) && m[0].t.hd==800 && n==6);
    assert(ddc_logs==4 && parser_logs==3);
    fresh(1); slow_reads=true;
    state.edid.ntiming=1; state.edid.timing[0]=xga; state.edid.extensions=2;
    assert(!gf4_detect(&h.ctx,1) && state.modes[0].t.hd==1024);
    assert(ddc_logs==3 && parser_logs==3);
}
static void native_and_diagnostics(void)
{
    fresh(1); state.edid.preferred=~0U; state.edid.ntiming=3;
    state.edid.timing[0]=svga; state.edid.timing[1]=xga; state.edid.timing[2]=vga;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.modes[0].t.hd==1024);
    /* A valid preferred beats a larger mode. */
    fresh(1); state.edid.preferred=0; state.edid.ntiming=2;
    state.edid.timing[0]=svga; state.edid.timing[1]=xga;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.modes[0].t.hd==800);
    fresh(1); state.edid.preferred=~0U; state.edid.ntiming=2;
    state.edid.timing[0]=vga; state.edid.timing[1]=vga;
    state.edid.timing[1].khz=31500; /* Same size, 75 Hz wins over 60 Hz. */
    assert(!gf4_detect(&h.ctx,B0_DL) && state.modes[0].t.khz==31500);
    fresh(1); state.edid.preferred=~0U; state.edid.ntiming=2;
    state.edid.timing[0]=(struct gf4_timing){50350,640,656,752,800,
        960,980,984,1050,GF4_DBLSCAN|3}; /* Source 640x480, 60 Hz. */
    state.edid.timing[1]=(struct gf4_timing){40000,640,656,752,800,
        600,601,605,628,3}; /* Larger source height wins over doubled fields. */
    assert(!gf4_detect(&h.ctx,B0_DL) && state.modes[0].t.vd==600);
    fresh(1); state.edid.preferred=~0U; state.edid.ntiming=2;
    state.edid.timing[0]=(struct gf4_timing){62940,640,656,752,800,
        960,980,984,1050,GF4_DBLSCAN|3}; /* Source 640x480, 75 Hz. */
    state.edid.timing[1]=vga;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.modes[0].t.flags&GF4_DBLSCAN);
    fresh(1); state.edid.preferred=0; state.edid.ntiming=2;
    state.edid.timing[0]=xga; state.edid.timing[0].flags|=GF4_INTERLACE;
    state.edid.timing[1]=svga;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.modes[0].t.hd==800);
    /* Unsupported preferred clock must not hide the other declared mode. */
    fresh(1); state.edid.ntiming=2;
    state.edid.timing[0]=xga; state.edid.timing[1]=svga;
    routes[0].max_khz=40000;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.modes[0].t.hd==800 && pll_logs==3);
    fresh(1); state.edid.ntiming=1; state.edid.timing[0]=xga;
    state.edid.max_khz=40000; state.edid.range_valid=true;
    assert(!gf4_detect(&h.ctx,B0_DL) && timing_logs==1);
    fresh(1); h.base.ch.B=B0_VGA8_BYTES;
    assert(!gf4_detect(&h.ctx,B0_DL) && state.nmodes==1 && vram_logs==2);
}
/* Real parser -> detect: an EDID DTD declares XGA, rather than injecting a
 * pre-parsed timing. Bytes are E-EDID table 3.21's 1024x768@60 raster. */
static void declared_edid(void)
{
    fresh(1);
    gf2_host_memset(block,0,sizeof(block));
    for (u32 i=1;i<7;i++) block[i]=0xff;
    block[18]=1; block[19]=3; block[24]=2;
    for (u32 i=38;i<54;i++) block[i]=1;
    static const u8 dtd[18]={0x64,0x19,0x00,0x40,0x41,0x00,0x26,0x30,
                            24,136,0x36,0,0,0,0,0,0,0x18};
    for (u32 i=0;i<18;i++) block[54+i]=dtd[i];
    u8 sum=0;
    for (u32 i=0;i<127;i++) sum+=block[i];
    block[127]=(u8)(0U-sum);
    struct gf4_data_ops data=*h.ctx.data;
    data.edid_modes=gf4_edid_modes; h.ctx.data=&data;
    assert(!gf4_detect(&h.ctx,B0_DL));
    assert(state.edid.base_valid && state.edid.ntiming==1 && state.edid.preferred==0);
    assert(state.modes[0].t.hd==1024 && state.modes[0].t.vd==768 &&
           state.modes[0].t.khz==65000 && state.nmodes==6);
    assert(!timing_logs && !vram_logs && !pll_logs && ddc_logs==1 && parser_logs==1);
}
/* V/nouveau_connector.c:439-445,579-588: one successful probe precedes
 * the full EDID read; a failed probe leaves only this connector's fallback. */
static void probe_detect(void)
{
    fresh(1); state.edid.ntiming=1; state.edid.timing[0]=svga;
    assert(!gf4_detect(&h.ctx,B0_DL));
    assert(h.calls[D_PROBE]==1 && h.calls[D_EDID]==2);
    assert(probe_logs==1 && ddc_logs==1 && parser_logs==1);
    assert(state.modes[0].t.hd==800);
    const i32 errors[]={GF4_EDDC,GF4_ETIME};
    for (u32 i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
        fresh(2); routes[1].i2c_index=1;
        state.edid.ntiming=1; state.edid.timing[0]=svga;
        gf4_host_fail_at(&h,D_PROBE,2,errors[i]);
        assert(!gf4_detect(&h.ctx,B0_DL) && h.ctx.last_rc==errors[i]);
        assert(h.calls[D_PROBE]==2 && h.calls[D_EDID]==2);
        assert(probe_logs==2 && ddc_logs==1 && parser_logs==1);
        const struct gf4_mode *m; u32 n;
        assert(!gf4_modes(&h.ctx,0,&m,&n) && m[0].t.hd==800);
        assert(!gf4_modes(&h.ctx,1,&m,&n)); expect_vga(m,n);
    }
}
static void surfaces(void)
{
    fresh(1);
    node=(struct gf_vram_reservation_node){.value={&h.ctx,0,B0_VGA32_BYTES,7,3}};
    h.base.reservations=&node;
    struct gf3_surface s={0,B0_VGA8_BYTES,640,640,480,GF3_INDEX8,7,3, 0};
    struct gf4_mode m={.t=vga,.fmt=GF3_INDEX8,.pitch=640};
    assert(!gf4_surface_check(&h.ctx,&s,&m));
    struct gf_vram_reservation before=node.value;
    s.generation++;
    assert(gf4_surface_check(&h.ctx,&s,&m)==GF3_ESTATE);
    s.generation--; s.fmt=GF3_Y8;
    assert(gf4_surface_check(&h.ctx,&s,&m)==GF4_EFORMAT);
    s.fmt=GF3_INDEX8; s.bytes--;
    assert(gf4_surface_check(&h.ctx,&s,&m)==GF3_ERANGE);
    s.bytes++; s.off=1;
    assert(gf4_surface_check(&h.ctx,&s,&m)==GF4_ERANGE);
    s.off=0; m.t.hs=m.t.hd;
    assert(gf4_surface_check(&h.ctx,&s,&m)==GF4_ERANGE);
    m.t=vga; h.base.ch.B=B0_VGA8_BYTES-1U;
    assert(gf4_surface_check(&h.ctx,&s,&m)==GF4_ERANGE);
    h.base.ch.B=B0_VRAM; h.ctx.cursor[1].value=node.value;
    assert(gf4_surface_check(&h.ctx,&s,&m)==GF4_EBUSY);
    h.ctx.cursor[1].value=(struct gf_vram_reservation){0};
    gf4_host_fail_at(&h,D_SURFACE,h.calls[D_SURFACE]+1U,GF2_EIO);
    assert(gf4_surface_check(&h.ctx,&s,&m)==GF2_EIO);
    assert(node.value.owner==before.owner && node.value.bytes==before.bytes &&
        node.value.generation==before.generation && !h.calls[D_WRITE]);
}
void _start(void)
{
    probe_detect(); fallback_and_ids(); candidates_and_heads(); failures(); ranges_and_tv(); fairness(); surfaces(); block_budgets(); native_and_diagnostics(); declared_edid();
    __asm__ volatile("int $0x80" : : "a"(1),"b"(0));
    __builtin_unreachable();
}
