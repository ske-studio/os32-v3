/* Independent MMIO oracle, nouveau c03c1a1 dispnv04/tvnv17.c:144-218,
 * 368-606; dac.c:235-330; tvmodesnv17.c:367-434,470-589; hw.c:503-535.
 * Golden CTV words below were evaluated from those output modelines and
 * rescaler at overscan=50. Fixture payload markers test transmission, not
 * the kernel-only B11-data mathematics. No external reference at runtime. */
#define GF4_HOST_UNIT 11
#include "gf4_display_fake.h"
#define REQUIRE(x) do { __asm__ volatile ("" ::: "memory"); if (!(x)) return __LINE__; } while (0)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
/* nvreg.h:114-127,347-422, hw.h:70-96; dac.c:41-52. */
#define T_STRIDE 0x2000U
#define T_CR 0x6013d4U
/* nvreg.h:82,129-133,248; crtc.c:334-337,579-582. */
#define T_SR 0x0c03c4U
#define T_PIXEL 0x28U
#define T_SLAVED (1U<<7)
#define T_CLOCK 1U
#define T_CLKDIV2 (1U<<3)
#define T_CLOCK_BASE 0x21U
/* tvmodesnv17.c:483-491: Composite on sensed pin 4. */
#define T_COMPOSITE_MUX (2U|0x010000U)
#define T_COMPOSITE_ENC7 0x10U
#define T_OWNER 0x44U
#define T_TG 0x680848U
#define T_DAC 0x68052cU
#define T_TEST 0x680608U
#define T_POINT 0x680610U
#define T_POWER2 0x1588U
#define T_POWER4 0x1590U
#define T_INDEX 0xd220U
#define T_DATA 0xd224U
#define T_PTV 0xd000U
#define T_ON ((1U<<28)|(1U<<4)|1U)
#define T_OFF ((2U<<28)|(2U<<4)|2U)
#define T_CTV_ON (T_ON|(1U<<24)|(1U<<20))
#define T_DEBUG ((1U<<24)|(1U<<20)|(1U<<12)|(1U<<8)|(8U<<4)|(1U<<4)|1U)
#define T_TEST_RUN 0x00100000U
#define T_CURIE (0x1aU<<16)
#define T_SAMPLE (0xa0U|(0xa0U<<10)|(0xa0U<<20)|(8U<<28))
#define T_INSERT (1U<<12)
/* Test-only distinct old values reveal unintended writes and early commits. */
#define T_OLD_OWNER 4U
#define T_OLD_DAC 0x00400101U
#define T_OLD_TG (T_ON|(1U<<17))
#define T_OLD_PTV 0x13111101U /* tvnv17.c:482-485 plus enabled. */
#define T_OTHER 0x12345678U
#define T_DEADLINE 100000U
#define T_FAILURE (-777)
#define T_ENC_MARKER 0x5aU
#define T_FILTER_MARKER 0x01000000U
#define T_PTV_MARKER 0x10000000U
#define T_CTV_MARKER 0x20000000U
static struct gf4_host host;
static struct gf4_state state,before;
static struct gf4_ctx before_ctx;
static struct gf4_host_event events[2048];
static struct gf3_host_reg regs[512];
static struct gf4_route route;
static struct gfinit_mode input;
static struct gf4_tv_state payload;
static u8 sensed_pins;
static bool sample_phase;
static u32 expected_clock;
/* Norm IDs: tvnv17.h:56-70. 6/8 are low-definition encoder, 7/9/10/11 CTV. */
static const u32 ctv_norms[]={7,9,10,11};
static const struct gf4_timing ctv_inputs[]={
    {27000,640,672,680,858,400,410,414,525,0},
    {27000,640,672,680,864,480,485,489,625,0},
    {74250,800,840,848,1650,600,605,610,750,0},
    {74250,1280,1320,1408,2200,720/2,724/2,728/2,1125/2,GF4_INTERLACE}
};
/* tvmodesnv17.c:202-213: native 1080i, normalized to CRTC fields. */
static const struct gf4_timing full1080i={
    74250,1920,1961,2049,2200,1080/2,1084/2,1088/2,1125/2,GF4_INTERLACE
};
#define T_FULL1080I COUNT(ctv_norms)
/* tvnv17.c:540-574, tvmodesnv17.c:541-589. 1080i forces overscan=100.
 * E.g. 720p margins=min((1280-800)/2,1280/20)=64 and 36;
 * scale values floor(800*2048/1152)=1422, floor(600*2048/648)&~3=1896. */
static const u32 golden_h[][7]={
    {719,857,722,734,742,36,683},
    {719,863,722,729,737,36,683},
    {1279,1649,1296,1348,1356,64,1215},
    {1919,2199,1952,1960,2048,320,1599},
    {1919,2199,1952,1960,2048,0,1919} /* native: no horizontal margin. */
};
static const u32 golden_v[][7]={
    {479,524,479,489,493,24,455},
    {575,624,575,580,584,28,547},
    {719,749,719,724,729,36,683},
    {1079,1124,1079,1083,1087,180,899},
    {1079,1124,1079,1083,1087,0,1079} /* native: no vertical margin. */
};
/* Values independently evaluated by integer arithmetic from the modelines. */
static const u32 golden_scale[]={
    (1U<<28)|(1896U<<16)|(1U<<12)|2022U,
    (1U<<28)|(1888U<<16)|(1U<<12)|2022U,
    (1U<<28)|(1896U<<16)|(1U<<12)|1422U,
    (1U<<28)|(2048U<<16)|(1U<<12)|2048U,
    (1U<<28)|(2048U<<16)|(1U<<12)|2048U /* unity, tvmodesnv17.c:567-569. */
};
static u32 get(u32 off)
{
    u32 value=0;
    for (u32 b=0;b<4U;++b) value|=gf4_host_reg_byte(&host,0,off+b)<<(8U*b);
    return value;
}
static void observe(struct gf4_host *h,const struct gf4_host_event *e)
{
    if (e->op==D_TV && !e->rc) *(struct gf4_tv_state *)e->p[1]=payload;
    if (e->op==D_WRITE && e->a[1]==T_TEST && (e->a[3]&T_INSERT)) sample_phase=true;
    if (e->op==D_WRITE && e->a[1]==T_TEST+T_STRIDE && (e->a[3]&T_INSERT)) sample_phase=true;
    if (e->op==D_READ && sample_phase && sensed_pins!=0xffU && !e->rc && e->a[1]==T_TEST+
        ((route.or_mask&12U) ? 0x68U : 0)+((route.or_mask&10U) ? T_STRIDE : 0))
        *(u32 *)e->p[1]=((u32)sensed_pins<<28)|get(e->a[1]);
    if (e->op==D_TV_GPIO && !e->rc) {
        /* Emulate C2 logical on for the one configured line (nv10.c:60-88). */
        if (h->ctx.dcb.tvdac[0].present)
            gf3_host_set(&h->base,0,0x60081cU,(get(0x60081cU)&~3U)|(e->a[0] ? 1U : 0));
    }
}
static void setup(u8 head,u8 or_mask,u32 norm,bool changing)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=COUNT(events);
    host.base.regs=regs; host.base.cregs=COUNT(regs); host.observe=observe;
    host.ctx.changing=changing; host.ctx.tv_sense_valid=true;
    host.ctx.tv_pin_mask=4; host.ctx.tv_subconnector=1; /* tv.c local Composite. */
    route=(struct gf4_route){.head=head,.head_mask=3,.type=GF4_TV,.or_mask=or_mask,.tv_norm=norm};
    input=(struct gfinit_mode){.fp_control=T_OLD_TG}; input.cr[0x33]=0xff;
    input.cr[T_PIXEL]=(16U+1U)/8U; /* crtc.c:579: RGB565 depth. */
    input.sr[T_CLOCK]=T_CLOCK_BASE;
    expected_clock=T_CLOCK_BASE;
    host.cr[head^1U][T_PIXEL]=T_ENC_MARKER;
    host.sr[head^1U][T_CLOCK]=T_ENC_MARKER;
    state.head[head].enabled=state.next[head].enabled=true;
    state.head[head].route=state.next[head].route=route;
    state.head[head].fp_on=state.next[head].fp_on=T_CTV_ON;
    struct gf4_timing t={25175,640,672,768,880,480,480,492,525,GF4_NEG_H|GF4_NEG_V};
    for (u32 i=0;i<COUNT(ctv_norms);++i) if (norm==ctv_norms[i]) t=ctv_inputs[i];
    state.head[head].mode.t=state.next[head].mode.t=t;
    payload=(struct gf4_tv_state){.version=GF4_TV_VERSION,.size=sizeof(payload),.norm=norm};
    for (u32 i=0;i<COUNT(payload.enc);++i) payload.enc[i]=(u8)(i^T_ENC_MARKER);
    for (u32 i=0;i<4U;++i) for (u32 j=0;j<7U;++j) {
        payload.hfilter[i][j]=T_FILTER_MARKER+i*0x100U+j;
        payload.hfilter2[i][j]=2U*T_FILTER_MARKER+i*0x100U+j;
        payload.vfilter[i][j]=3U*T_FILTER_MARKER+i*0x100U+j;
    }
    for (u32 i=0;i<COUNT(payload.ptv);++i) payload.ptv[i]=T_PTV_MARKER+i;
    payload.ptv[0]=T_OLD_PTV&~1U;
    for (u32 i=0;i<COUNT(payload.ctv);++i) payload.ctv[i]=T_CTV_MARKER+i;
    state.tv[head]=payload; state.tv[head].ptv[0]=T_OLD_PTV;
    u32 o=((or_mask&12U) ? 0x68U : 0)+((or_mask&10U) ? T_STRIDE : 0);
    gf3_host_set(&host.base,0,T_DAC+o,T_OLD_DAC);
    gf3_host_set(&host.base,0,T_TG+head*T_STRIDE,T_OLD_TG);
    gf3_host_set(&host.base,0,T_TG+(head^1U)*T_STRIDE,T_OTHER);
    gf3_host_set(&host.base,0,T_TEST+o,0);
    gf3_host_set(&host.base,0,T_POWER2,T_OTHER);
    gf3_host_set(&host.base,0,T_POWER4,T_OTHER);
    host.cr[0][T_OWNER]=T_OLD_OWNER;
    sensed_pins=4; sample_phase=false;
}
struct expected { enum gf4_host_op op; u32 off,value; };
static struct expected expected[512];
static u32 nexpected;
static void w(u32 off,u32 value)
{
    assert(nexpected<COUNT(expected)); expected[nexpected++]=(struct expected){D_WRITE,off,value};
}
static void index_w(u32 port,u32 index,u32 value) { w(port,index); w(port+1U,value); }
static void power_expected(bool ctv,bool on,u32 ptv,u32 fp)
{
    w(T_TG+route.head*T_STRIDE,ctv && on ? T_CTV_ON : (fp&~T_ON)|T_OFF);
    w(T_PTV+0x200U,(ptv&~1U)|(!ctv && on ? 1U : 0));
    expected[nexpected++]=(struct expected){D_TV_GPIO,0,on};
    expected[nexpected++]=(struct expected){D_DAC,0,on};
}
static int golden_load(i32 ctv_index,u32 mux,u32 enc7)
{
    bool ctv=ctv_index>=0;
    u32 d=route.head*T_STRIDE;
    u32 o=((route.or_mask&12U) ? 0x68U : 0)+((route.or_mask&10U) ? T_STRIDE : 0);
    u32 off=(T_OLD_TG&~T_ON)|T_OFF;
    nexpected=0;
    w(T_CR,T_OWNER); index_w(T_CR,T_OWNER,route.head ? 3U : 0);
    power_expected(ctv,false,T_OLD_PTV,T_OLD_TG);
    w(T_TG+d,off);
    u32 dac=(T_OLD_DAC&~0x30U)|1U|T_CURIE|(ctv ? 0x20U : 0x10U);
    if (ctv) dac=(dac&~0x100U)|(route.head ? 0x100U : 0);
    w(T_DAC+o,dac);
    index_w(T_CR+d,T_PIXEL,input.cr[T_PIXEL]|T_SLAVED);
    index_w(T_SR+d,T_CLOCK,expected_clock);
    index_w(T_CR+d,0x33U,0xc4U|(ctv ? (route.head ? 1U : 9U) : 0));
    index_w(T_CR+d,0x53U,0x40U); index_w(T_CR+d,0x54U,0);
    w(0x680630U+d,2); w(0x680700U+d,1); w(0x6808c0U+d,0);
    if (ctv) {
        for (u32 i=0;i<7U;++i) { w(0x680800U+d+4U*i,golden_v[ctv_index][i]); w(0x680820U+d+4U*i,golden_h[ctv_index][i]); }
        w(T_TG+d,T_CTV_ON); w(0x680880U+d,T_DEBUG);
        w(0x680884U+d,golden_scale[ctv_index]); w(0x680888U+d,0); w(0x68084cU+d,0x801080U);
        for (u32 i=0;i<COUNT(payload.ctv);++i) w(0x680c00U+d+4U*i,payload.ctv[i]);
    }
    for (u32 i=0;i<COUNT(payload.enc);++i) {
        w(T_INDEX,i); w(T_DATA,!ctv && i==7U ? enc7 : !ctv && i==0x25U ? 0 : payload.enc[i]);
    }
    const u32 bases[]={0xd310U,0xd390U,0xd510U};
    const u32 offsets[]={0,0x1c,0x40,0x5c};
    const u32 (*filters[])[7]={payload.hfilter,payload.hfilter2,payload.vfilter};
    for (u32 f=0;f<COUNT(bases);++f) for (u32 i=0;i<COUNT(offsets);++i)
        for (u32 j=0;j<7U;++j) w(bases[f]+offsets[i]+4U*j,filters[f][i][j]);
    const u32 ptv_offsets[]={0x200,0x204,0x208,0x20c,0x304,0x500,0x504,0x508,0x600,0x604,0x608,0x60c,0x610,0x614};
    u32 ptv=payload.ptv[0];
    if (!ctv) ptv=(ptv&~0x10U)|(route.head ? 0x10U : 0);
    for (u32 i=0;i<COUNT(ptv_offsets);++i)
        w(T_PTV+ptv_offsets[i],i==0 ? ptv : !ctv && i==1 ? mux : payload.ptv[i]);
    w(T_INDEX,0x3e); w(T_DATA,1); w(T_INDEX,0x3e); w(T_DATA,0);
    w(T_TEST+o,T_TEST_RUN);
    power_expected(ctv,true,ptv,ctv ? T_CTV_ON : off);
    index_w(T_CR,T_OWNER,T_OLD_OWNER);
    u32 n=0;
    for (u32 i=0;i<host.nevents;++i) {
        const struct gf4_host_event *e=&events[i];
        if (e->op!=D_WRITE && e->op!=D_TV_GPIO && e->op!=D_DAC) continue;
        REQUIRE(n<nexpected && e->op==expected[n].op);
        if (e->op==D_WRITE) {
            REQUIRE(e->a[0]==0 && e->a[1]==expected[n].off && e->a[3]==expected[n].value);
            REQUIRE(e->a[2]==((expected[n].off==T_CR || expected[n].off==T_CR+1U ||
                    expected[n].off==T_CR+d || expected[n].off==T_CR+d+1U ||
                    expected[n].off==T_SR+d || expected[n].off==T_SR+d+1U) ? 1U : 4U));
        } else REQUIRE(e->a[0]==expected[n].value);
        ++n;
    }
    REQUIRE(n==nexpected);
    REQUIRE(get(T_TG+(route.head^1U)*T_STRIDE)==T_OTHER);
    REQUIRE(host.cr[0][T_OWNER]==T_OLD_OWNER);
    REQUIRE(state.work[route.head].head==route.head);
    REQUIRE(host.cr[route.head][T_PIXEL]==(input.cr[T_PIXEL]|T_SLAVED));
    REQUIRE(state.work[route.head].cr[T_PIXEL]==(input.cr[T_PIXEL]|T_SLAVED));
    REQUIRE(host.sr[route.head][T_CLOCK]==expected_clock);
    REQUIRE(state.work[route.head].sr[T_CLOCK]==expected_clock);
    REQUIRE(host.cr[route.head^1U][T_PIXEL]==T_ENC_MARKER);
    REQUIRE(host.sr[route.head^1U][T_CLOCK]==T_ENC_MARKER);
    REQUIRE(state.tv[route.head].ptv[0]==((ptv&~1U)|(ctv ? 0 : 1U)));
    if (ctv) {
        REQUIRE(!gf2_host_memcmp(state.work[route.head].fp_horiz,golden_h[ctv_index],sizeof(golden_h[0])));
        REQUIRE(!gf2_host_memcmp(state.work[route.head].fp_vert,golden_v[ctv_index],sizeof(golden_v[0])));
    }
    return 0;
}
static int loads(void)
{
    for (u8 head=0;head<GF4_HEADS;++head) for (u8 or=1;or<=8U;or<<=1)
        for (u32 norm=0;norm<12U;++norm) {
            setup(head,or,norm,head!=0);
            i32 ctv=-1;
            for (u32 i=0;i<COUNT(ctv_norms);++i) if (norm==ctv_norms[i]) ctv=(i32)i;
            REQUIRE(!gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE));
            REQUIRE(!golden_load(ctv,0x010002U,0x10));
        }
    /* tvmodesnv17.c:481-508: pin-dependent composite, SVIDEO, SCART,
     * and unknown (unchanged) routing. Cached component has its own mux. */
    const u8 pins[]={2,0,12,14,0,14};
    const u8 sub[]={1,1,2,4,0,3};
    const u32 mux[]={0x100002U,0x110002U,0x11012U,0x111012U,T_PTV_MARKER+1U,0x111333U};
    const u8 enc7[]={0x10,0x10,0x18,0x18,7U^T_ENC_MARKER,0x14};
    for (u32 i=0;i<COUNT(pins);++i) {
        setup(0,1,0,false); host.ctx.tv_pin_mask=pins[i]; host.ctx.tv_subconnector=sub[i];
        REQUIRE(!gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE));
        REQUIRE(!golden_load(-1,mux[i],enc7[i]));
    }
    return 0;
}
static int tv_mode_bits(void)
{
    /* crtc.c:579-582: depth byte must survive TV slaving at each depth. */
    const u32 depths[]={8,16,24}; /* RGB8888 has depth=24, not storage bpp=32. */
    /* tvmodesnv17.c:439-454: first three request CLKDIV2; 640x480
     * supplies a control with a stale incoming divider to clear. */
    const struct gf4_timing modes[]={
        {0,320,344,392,560,2*200,2*200,2*202,2*220,GF4_DBLSCAN|GF4_NEG_H|GF4_NEG_V},
        {0,320,344,392,560,2*240,2*240,2*246,2*263,GF4_DBLSCAN|GF4_NEG_H|GF4_NEG_V},
        {0,400,432,496,640,2*300,2*300,2*303,2*314,GF4_DBLSCAN},
        {0,640,672,768,880,480,480,492,525,GF4_NEG_H|GF4_NEG_V}
    };
    for (u8 head=0;head<GF4_HEADS;++head)
        for (u32 depth=0;depth<COUNT(depths);++depth)
            for (u32 i=0;i<COUNT(modes);++i) {
                setup(head,1,0,head!=0);
                input.cr[T_PIXEL]=(depths[depth]+1U)/8U;
                input.sr[T_CLOCK]=T_CLOCK_BASE|T_CLKDIV2;
                state.head[head].mode.t=state.next[head].mode.t=modes[i];
                expected_clock=T_CLOCK_BASE|((modes[i].flags&GF4_DBLSCAN) ? T_CLKDIV2 : 0);
                REQUIRE(!gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE));
                REQUIRE(!golden_load(-1,T_COMPOSITE_MUX,T_COMPOSITE_ENC7));
            }
    for (u8 head=0;head<GF4_HEADS;++head) {
        setup(head,1,ctv_norms[COUNT(ctv_norms)-1U],head!=0);
        state.head[head].mode.t=state.next[head].mode.t=full1080i;
        REQUIRE(!gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE));
        REQUIRE(!golden_load(T_FULL1080I,T_COMPOSITE_MUX,T_COMPOSITE_ENC7));
    }
    return 0;
}
static int load_failures(void)
{
    setup(1,4,10,true);
    REQUIRE(!gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE));
    u32 count[D_OP_COUNT]; gf2_host_memcpy(count,host.calls,sizeof(count));
    const enum gf4_host_op ops[]={D_ROUTE,D_TV,D_CLOCK,D_READ,D_WRITE,D_TV_GPIO,D_DAC};
    for (u32 op=0;op<COUNT(ops);++op) for (u32 nth=1;nth<=count[ops[op]];++nth) {
        setup(1,4,10,true); before=state; before_ctx=host.ctx;
        gf4_host_fail_at(&host,ops[op],nth,T_FAILURE);
        REQUIRE(gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE)==T_FAILURE);
        REQUIRE(!gf2_host_memcmp(&state,&before,sizeof(state)));
        REQUIRE(!gf2_host_memcmp(&host.ctx,&before_ctx,sizeof(before_ctx)));
        REQUIRE(get(T_TG)==T_OTHER);
        bool failed=false;
        for (u32 i=0;i<host.nevents;++i) {
            const struct gf4_host_event *e=&events[i];
            if (failed) {
                REQUIRE(e->op==D_WRITE);
                REQUIRE((e->a[1]==T_CR && e->a[3]==T_OWNER) ||
                        (e->a[1]==T_CR+1U && e->a[3]==T_OLD_OWNER));
            }
            if (e->rc) failed=true;
        }
        REQUIRE(failed);
    }
    setup(1,4,10,true); before=state; before_ctx=host.ctx;
    /* Owner selection data may have applied before a bus error. */
    gf4_host_write_applied_error(&host,3,T_FAILURE);
    REQUIRE(gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE)==T_FAILURE);
    REQUIRE(host.cr[0][T_OWNER]==T_OLD_OWNER);
    REQUIRE(!gf2_host_memcmp(&state,&before,sizeof(state)));
        REQUIRE(!gf2_host_memcmp(&host.ctx,&before_ctx,sizeof(before_ctx)));
    return 0;
}
static int dpms(void)
{
    for (u8 head=0;head<GF4_HEADS;++head) for (u32 norm=0;norm<12U;++norm)
        for (u32 mode=GF4_ON;mode<=GF4_OFF;++mode) {
            setup(head,1,norm,true);
            bool ctv=norm==7U || norm==9U || norm==10U || norm==11U;
            REQUIRE(!gf4_tv_dpms(&host.ctx,&route,(enum gf4_dpms)mode,T_DEADLINE));
            u32 value=ctv && mode==GF4_ON ? T_CTV_ON : (T_OLD_TG&~T_ON)|T_OFF;
            REQUIRE(get(T_TG+head*T_STRIDE)==value);
            REQUIRE(get(T_TG+(head^1U)*T_STRIDE)==T_OTHER);
            REQUIRE(get(T_PTV+0x200U)==((T_OLD_PTV&~1U)|(!ctv && mode==GF4_ON ? 1U : 0)));
            REQUIRE(state.work[head].fp_control==value);
            REQUIRE(state.next[head].fp_on==T_CTV_ON);
            u32 sequence=0;
            for (u32 i=0;i<host.nevents;++i) {
                const struct gf4_host_event *e=&events[i];
                if (e->op==D_WRITE) {
                    REQUIRE(sequence<2U);
                    REQUIRE(e->a[1]==(sequence ? T_PTV+0x200U : T_TG+head*T_STRIDE)); ++sequence;
                } else if (e->op==D_TV_GPIO) {
                    REQUIRE(sequence==2U && e->a[0]==(mode==GF4_ON) && e->a[1]==T_DEADLINE); ++sequence;
                } else if (e->op==D_DAC) {
                    REQUIRE(sequence==3U && e->a[0]==(mode==GF4_ON)); ++sequence;
                }
            }
            REQUIRE(sequence==4U);
        }
    const enum gf4_host_op ops[]={D_ROUTE,D_CLOCK,D_READ,D_WRITE,D_TV_GPIO,D_DAC};
    setup(1,4,0,true);
    REQUIRE(!gf4_tv_dpms(&host.ctx,&route,GF4_OFF,T_DEADLINE));
    u32 count[D_OP_COUNT]; gf2_host_memcpy(count,host.calls,sizeof(count));
    for (u32 op=0;op<COUNT(ops);++op) for (u32 nth=1;nth<=count[ops[op]];++nth) {
        setup(1,4,0,true); before=state; before_ctx=host.ctx;
        gf4_host_fail_at(&host,ops[op],nth,T_FAILURE);
        REQUIRE(gf4_tv_dpms(&host.ctx,&route,GF4_OFF,T_DEADLINE)==T_FAILURE);
        REQUIRE(!gf2_host_memcmp(&state,&before,sizeof(state)));
        REQUIRE(!gf2_host_memcmp(&host.ctx,&before_ctx,sizeof(before_ctx)));
        REQUIRE(events[host.nevents-1U].rc==T_FAILURE);
    }
    return 0;
}
static int senses(void)
{
    /* tvnv17.c:163-183: independently specified connector truth table.
     * D0/nvkm dcb.c:125 keeps has_component_output=false, hence SCART. */
    const u8 pins[]={0,2,4,12,14,6};
    const u8 sub[]={0,1,1,2,4,0};
    for (u32 i=0;i<COUNT(pins);++i) for (u8 or=1;or<=8U;or<<=1) {
        setup(1,or,0,false);
        host.ctx.tv_sense_valid=false; sensed_pins=pins[i];
        host.ctx.dcb.tvdac[0]=(struct gf4_gpio_value){true,2,{0,1}};
        gf3_host_set(&host.base,0,0x60081cU,T_OTHER);
        REQUIRE(!gf4_tv_sense(&host.ctx,&route,T_DEADLINE));
        REQUIRE(host.ctx.tv_sense_valid && host.ctx.tv_pin_mask==pins[i]);
        REQUIRE(host.ctx.tv_subconnector==sub[i]);
        REQUIRE(host.local_now==4000U+1000U+5000U); /* dac.c:284,303,312. */
        REQUIRE(get(T_DAC+((or&12U) ? 0x68U : 0)+((or&10U) ? T_STRIDE : 0))==T_OLD_DAC);
        REQUIRE(get(T_POWER2)==T_OTHER && get(T_POWER4)==T_OTHER);
        REQUIRE(!get(T_TEST+((or&12U) ? 0x68U : 0)+((or&10U) ? T_STRIDE : 0)));
        REQUIRE(get(0x60081cU)==T_OTHER && host.cr[0][T_OWNER]==T_OLD_OWNER);
        bool sample=false;
        u32 delays=0;
        for (u32 j=0;j<host.nevents;++j) {
            const struct gf4_host_event *e=&events[j];
            if (e->op==D_WRITE && (e->a[1]==T_POINT || e->a[1]==T_POINT+T_STRIDE) && e->a[3]) {
                REQUIRE(e->a[3]==T_SAMPLE); sample=true;
            }
            if (e->op==D_SLEEP) ++delays;
        }
        REQUIRE(sample && delays==3U);
    }
    /* The second sample is ANDed, not ORed (dac.c:314-316). */
    setup(1,4,0,false); host.ctx.tv_sense_valid=false;
    /* Use observe's mask once, then change it before the residual-current sample. */
    sensed_pins=0xff;
    const struct gf4_host_read reads[]={
        {0,T_TEST+0x68U,4,0}, /* saved TEST */
        {0,T_TEST+0x68U,4,14U<<28},
        {0,T_TEST+0x68U,4,4U<<28}
    };
    gf4_host_reads(&host,reads,COUNT(reads));
    REQUIRE(!gf4_tv_sense(&host.ctx,&route,T_DEADLINE));
    /* Scripted samples bypass the automatic sampled-pin observation. */
    REQUIRE(host.next_read==COUNT(reads));
    REQUIRE(host.ctx.tv_pin_mask==4U);
    return 0;
}
static int sense_failures(void)
{
    setup(1,4,0,false); host.ctx.tv_sense_valid=false;
    host.ctx.dcb.tvdac[0]=(struct gf4_gpio_value){true,2,{0,1}};
    gf3_host_set(&host.base,0,0x60081cU,T_OTHER);
    REQUIRE(!gf4_tv_sense(&host.ctx,&route,T_DEADLINE));
    u32 count[D_OP_COUNT]; gf2_host_memcpy(count,host.calls,sizeof(count));
    const enum gf4_host_op ops[]={D_ROUTE,D_CLOCK,D_READ,D_WRITE,D_SLEEP,D_TV_GPIO};
    for (u32 op=0;op<COUNT(ops);++op) for (u32 nth=1;nth<=count[ops[op]];++nth) {
        setup(1,4,0,false); host.ctx.tv_sense_valid=false;
        host.ctx.tv_pin_mask=12; host.ctx.tv_subconnector=2;
        host.ctx.dcb.tvdac[0]=(struct gf4_gpio_value){true,2,{0,1}};
        gf3_host_set(&host.base,0,0x60081cU,T_OTHER); before=state; before_ctx=host.ctx;
        gf4_host_fail_at(&host,ops[op],nth,T_FAILURE);
        REQUIRE(gf4_tv_sense(&host.ctx,&route,T_DEADLINE)==T_FAILURE);
        REQUIRE(!host.ctx.tv_sense_valid && host.ctx.tv_pin_mask==12 && host.ctx.tv_subconnector==2);
        REQUIRE(!gf2_host_memcmp(&state,&before,sizeof(state)));
        REQUIRE(!gf2_host_memcmp(&host.ctx,&before_ctx,sizeof(before_ctx)));
        /* First failure forbids further waits and logical GPIO enables;
         * subsequent raw I/O is restoration only. */
        bool failed=false;
        for (u32 j=0;j<host.nevents;++j) {
            const struct gf4_host_event *e=&events[j];
            if (failed) REQUIRE(e->op==D_WRITE || e->op==D_READ);
            if (e->rc) failed=true;
        }
        REQUIRE(failed);
    }
    setup(1,4,0,false); host.ctx.tv_sense_valid=false;
    REQUIRE(gf4_tv_sense(&host.ctx,&route,4000U)==GF4_ETIME);
    REQUIRE(!host.ctx.tv_sense_valid && host.cr[0][T_OWNER]==T_OLD_OWNER);
    REQUIRE(get(T_DAC+0x68U)==T_OLD_DAC && get(T_POWER2)==T_OTHER);
    setup(1,4,0,false); host.ctx.tv_sense_valid=false;
    gf4_host_write_applied_error(&host,3,T_FAILURE);
    REQUIRE(gf4_tv_sense(&host.ctx,&route,T_DEADLINE)==T_FAILURE);
    REQUIRE(host.cr[0][T_OWNER]==T_OLD_OWNER && !host.ctx.tv_sense_valid);
    /* Load automatically detects exactly once; cached detect survives mode loads. */
    setup(1,4,0,false); host.ctx.tv_sense_valid=false;
    REQUIRE(!gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE));
    REQUIRE(host.ctx.tv_sense_valid && host.calls[D_SLEEP]==3U);
    gf4_host_trace_clear(&host);
    REQUIRE(!gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE));
    REQUIRE(!host.calls[D_SLEEP]);
    return 0;
}
static int unbind(void)
{
    for (u32 active=0;active<2U;++active) {
        setup(1,1,10,true);
        struct gf4_route digital={.head=0,.head_mask=3,.type=GF4_TMDS,.or_mask=4,.dual_link=true};
        host.ctx.routes=&digital; host.ctx.nroutes=1;
        state.next[0].enabled=active!=0; state.next[0].route=digital;
        /* ramdac=1, bit3=0 => currently bound head B. */
        gf3_host_set(&host.base,0,0x6828b4U,0x80U);
        REQUIRE(!gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE));
        u32 bind=0;
        for (u32 i=0;i<host.nevents;++i) if (events[i].op==D_WRITE &&
            (events[i].a[1]==0x6828b4U || events[i].a[1]==0x6828bcU)) {
            REQUIRE(!active);
            REQUIRE(events[i].a[3]==(bind ? 0x80U : 0x88U)); ++bind;
        }
        REQUIRE(bind==(active ? 0U : 2U));
    }
    return 0;
}
static int boundary(void)
{
    setup(0,1,0,false); host.local_now=T_DEADLINE;
    REQUIRE(gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_ETIME);
    REQUIRE(!host.calls[D_WRITE]);
    setup(0,1,0,false); route.tv_norm=12;
    REQUIRE(gf4_tv_sense(&host.ctx,&route,T_DEADLINE)==GF4_EINVAL);
    REQUIRE(!host.nevents);
    route.tv_norm=0; route.location=1;
    REQUIRE(gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_EUNSUPPORTED);
    REQUIRE(!host.nevents);
    route.location=0; host.ctx.epoch++;
    REQUIRE(gf4_tv_dpms(&host.ctx,&route,GF4_ON,T_DEADLINE)==GF4_ESTATE);
    REQUIRE(!host.nevents);
    setup(0,1,0,false);
    REQUIRE(gf4_tv_load(&host.ctx,&route,0,T_DEADLINE)==GF4_EINVAL);
    REQUIRE(gf4_tv_dpms(&host.ctx,&route,(enum gf4_dpms)-1,T_DEADLINE)==GF4_EINVAL);
    REQUIRE(!host.nevents);
    gf4_host_fail_at(&host,D_TV,1,GF4_ERANGE); before=state; before_ctx=host.ctx;
    REQUIRE(gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_ERANGE);
    REQUIRE(!host.calls[D_WRITE] && !gf2_host_memcmp(&state,&before,sizeof(state)));
    setup(0,1,7,false); state.head[0].mode.t.hd=721;
    REQUIRE(gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_ERANGE);
    REQUIRE(!host.calls[D_WRITE]);
    setup(0,1,0,false); payload.version++;
    REQUIRE(gf4_tv_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_ESTATE);
    REQUIRE(!host.calls[D_WRITE]);
    return 0;
}
static int test(void)
{
    int rc=loads();
    if (!rc) rc=tv_mode_bits();
    if (!rc) rc=load_failures();
    if (!rc) rc=dpms();
    if (!rc) rc=senses();
    if (!rc) rc=sense_failures();
    if (!rc) rc=unbind();
    if (!rc) rc=boundary();
    return rc;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1),"b"(rc ? 1 : 0));
    __builtin_unreachable();
}
