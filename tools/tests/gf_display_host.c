/* Real C plan is the write oracle. Bus/owner are fake; no physical I/O. */
#define GF_C_ENTRY previous_c_tests
#include "gfinit_plan_c_host.c"
#include "../../hwdev/gf/runner.c"
#include "../../hwdev/gf/save.c"
#include "../../hwdev/gf/pll.c"
#include "gf_previous_calc.inc" /* generated from the immutable P3 base */

struct sim_reg {u32 off,value;};
static struct sim_reg sim_regs[4096];
static u32 sim_count,sim_index[6],sim_vga[6][256];
static u32 sim_calls,sim_fail,sim_second_fail,sim_points,sim_point_fail,sim_time;
static u32 sim_restores,sim_saved,sim_acquires,sim_releases,sim_validates;
static u32 sim_waits,sim_wait_fail,sim_validate_fail,sim_acquire_fail;
static u32 sim_check_reads,sim_check_mismatch,sim_restore_fail;
static u32 sim_pll_write_fail,sim_pll_writes;
static bool sim_compare,sim_trace;
static struct gf_display_context display;
static struct gf_init_context display_init;
static struct gf_init_program display_program;
static u8 sim_rgb[VIDEO_PALETTE_BYTES];
static u32 sim_seen[GF_INIT_MAINS];
static int sim_port(u32 off)
{
    for (u32 h=0;h<2;h++) {
        if (off==0x6013d4+h*0x2000) return h*3;
        if (off==0xc03c4+h*0x2000) return h*3+1;
        if (off==0xc03ceU + h*0x2000) return h*3+2;
    }
    return -1;
}
static u32 *sim_register(u32 off)
{
    for (u32 i=0;i<sim_count;i++) if (sim_regs[i].off==off) return &sim_regs[i].value;
    CHECK(sim_count<4096);
    sim_regs[sim_count]=(struct sim_reg){off,0};
    return &sim_regs[sim_count++].value;
}
static int sim_io(bool write,u32 off,u32 width,u32 *v)
{
    CHECK(width==1 || width==4 || width==2);
    CHECK(off!=0xfac && off<0x1000000);
    sim_calls++;
    if (sim_calls==sim_fail || sim_calls==sim_second_fail) return -1;
    if (write && display.ordinal>=30 && display.ordinal<=34 &&
        display.load_context && ++sim_pll_writes==sim_pll_write_fail) return -1;
    int index=sim_port(off),data=sim_port(off-1);
    /* Side-effect reads are forbidden, including on error and cleanup. */
    CHECK(write || (off!=0x6813c9 && off!=0x6013c0));
    if (!write) {
        if (index>=0) *v=sim_index[index];
        else if (data>=0) *v=sim_vga[data][sim_index[data]];
        else *v=*sim_register(off==0xc03cc?0xc03c2:off);
        /* Match the old C test's independent LIVE stimulus. CR11's last
         * lock reads its just-written value; gate restore sees disconnected. */
        if (sim_compare && display.ordinal<gf_mode_row_count) {
            const struct gf_mode_row *r=&gf_mode_rows[display.ordinal];
            if (r->expr==GF_C_LIVE || r->expr==GF_C_PLL) {
                if (!(off==0x6013d5 && sim_index[0]==0x11 && r->bits==0x80))
                    *v=0xa5a55a5aU & (width==1?255:~0U);
            }
        }
    } else if (index>=0) sim_index[index]=*v;
    else if (data>=0) {
        sim_vga[data][sim_index[data]]=*v;
        if (data%3==0 && sim_index[data]==0x1f)
            sim_vga[data][0x1f]=*v==0x57?3:0;
    } else *sim_register(off)=*v;
    if (!write && display.section==GF_DISPLAY_CHECK && ++sim_check_reads==sim_check_mismatch)
        *v^=off==0x680580?0x100:off==0x680608?0x100000:1;
    if (sim_trace) {
        output("bus=");val(display.ordinal);val(write);val(off);val(width);val(*v);output("\n");
    }
    return 0;
}
static int sim_read(void *p,u32 bar,u32 off,u32 width,u32 *v)
{(void)p;CHECK(bar==0);return sim_io(false,off,width,v);}
static int sim_write(void *p,u32 bar,u32 off,u32 width,u32 v)
{(void)p;CHECK(bar==0);return sim_io(true,off,width,&v);}
static int sim_point(void *p)
{
    (void)p;
    if (display.init && display.init->main_id<GF_INIT_MAINS)
        sim_seen[display.init->main_id]++;
    return ++sim_points==sim_point_fail?-1:0;
}
static int sim_clock(void *p,u32 *v){(void)p;*v=sim_time;return 0;}
static int sim_delay(void *p,u32 us)
{
    (void)p;sim_time+=us;
    if (sim_trace) {output("delay=");val(display.ordinal);val(us);output("\n");}
    return ++sim_waits==sim_wait_fail?-1:0;
}
static int sim_restore(void *p,struct gf_init_context *tx)
{
    (void)p;sim_restores++;sim_saved+=tx->saved_count;
    int rc=gf_init_restore(tx);
    return sim_restore_fail?-1:rc;
}
static int sim_acquire(struct gf_display_context *c)
{
    sim_acquires++;
    CHECK(!c->active && !c->initialized && !c->baseline_valid);
    display_init=(struct gf_init_context){.io=c->io,.rom=rom,.rom_size=sizeof(rom),.started_us=sim_time};
    c->init=&display_init;c->program=&display_program;
    if (gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&c->limits)) return -1;
    return sim_acquire_fail?-1:0;
}
static void sim_release(struct gf_display_context *c)
{
    (void)c;sim_releases++;
    CHECK(sim_acquires==sim_releases);
    if (!sim_acquire_fail && !sim_fail && !sim_point_fail && !sim_wait_fail) {
        CHECK(c->baseline_valid && display_init.main_id==4);
        for (u32 i=0;i<5;i++) CHECK(sim_seen[i]>0);
    }
}
static int sim_validate(struct gf_display_context *c)
{
    sim_validates++;
    CHECK(c->initialized && c->baseline_valid && !c->init && !c->program);
    return sim_validate_fail?-1:0;
}
static const struct gf_display_owner sim_owner={sim_acquire,sim_release,sim_validate};
static void sim_reset(void)
{
    sim_count=sim_calls=sim_fail=sim_second_fail=sim_points=sim_point_fail=sim_time=0;
    sim_restores=sim_saved=sim_acquires=sim_releases=sim_validates=sim_waits=0;
    sim_wait_fail=sim_validate_fail=sim_acquire_fail=0;sim_compare=sim_trace=false;
    sim_check_reads=sim_check_mismatch=sim_restore_fail=sim_pll_write_fail=sim_pll_writes=0;
    for(u32 i=0;i<6;i++) {sim_index[i]=0;for(u32 j=0;j<256;j++)sim_vga[i][j]=0;}
    for(u32 i=0;i<5;i++)sim_seen[i]=0;
    display=(struct gf_display_context){.owner=&sim_owner,.reusable=true,
        .io={.read=sim_read,.write=sim_write,.point=sim_point,.restore=sim_restore,
             .timing={sim_clock,sim_delay}}};
    display.bars[1]=(struct recover_bar_extent){.base=0x90000000,.length=0x10000000,.valid=true};
    for (u32 i=0;i<768;i++) sim_rgb[i]=(u8)(i*13+7);
}
static void calc_equivalence(void)
{
    struct gfinit_pll_limits l;
    CHECK(!gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&l));
    u32 seed=0x915ad;
    for(u32 n=0;n<512;n++) {
        struct gfinit_mode_saved s={0};
        for(u32 i=0;i<sizeof(s);i++) {
            seed=seed*1664525+1013904223;
            ((u8 *)&s)[i]=(u8)(seed>>24);
        }
        /* Both single-stage, two-stage and unsatisfiable clocks. */
        struct gfinit_pll_limits limits=l;
        if(n%3==1) limits.vco1.max_freq=40000;
        if(n%3==2) limits.ref=0;
        struct gfinit_mode a,b;
        int old=gf_previous_calc(&limits,&s,&a),now=gfinit_mode_calc(&limits,&s,&b);
        CHECK(old==now);
        for(u32 i=0;i<sizeof(a);i++) CHECK(((u8 *)&a)[i]==((u8 *)&b)[i]);
    }
    output("PASS calc: 512 seeded saved states, single/two-stage and failed solve, entire structure\n");
}
static void row_comparison(u32 variant,bool fp)
{
    /* Existing C plan/executor supplies expected final write values. */
    c_walk(variant,fp);
    output("adapter variant=");val(variant);val(fp);output("\n");
    sim_reset();sim_compare=true;
    struct gfinit_mode_saved s={0};
    if (variant) s=(struct gfinit_mode_saved){255,255,0x5a,0x12,0xfe,0x12345678,0x87654321,0xabcdef01};
    CHECK(!gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&display.limits));
    CHECK(!gfinit_mode_calc(&display.limits,&s,&display.mode));
    for(u32 i=0;i<256;i++) gfinit_mode_palette(i,&display.palette[i*3]);
    *sim_register(0x680848)=fp?0x10000000:0x20000022;
    sim_trace=true;
    CHECK(!gf_display_modeset(&display));
    sim_trace=false;sim_compare=false;
    CHECK(!display.load_context && !sim_restores);
    output("adapter end\n");
}
static void pll_failure_cases(void)
{
    /* Independent video destinations and seed snapshots. Fail each write,
     * including final C040 restore, and check all original values survive. */
    const u32 regs[]={0xc040,0x680580,0x680578,0x680508};
    for(u32 fault=1;fault<=5;fault++) {
        sim_reset();
        struct gfinit_mode_saved saved={0};
        CHECK(!gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&display.limits));
        CHECK(!gfinit_mode_calc(&display.limits,&saved,&display.mode));
        for(u32 i=0;i<4;i++)*sim_register(regs[i])=0x12345678+i;
        sim_pll_write_fail=fault;
        CHECK(gf_display_modeset(&display)==-1 && sim_restores==1 && sim_saved>0);
        CHECK(!display.load_context);
        for(u32 i=0;i<4;i++)CHECK(*sim_register(regs[i])==0x12345678+i);
    }
    output("PASS PLL: each video write failure restores operation snapshots before return\n");
}
static void service_cases(void)
{
    struct video_device d={&display,&gf_video_ops};
    struct video_info info;struct video_mode mode;struct video_fb fb;
    sim_reset();
    CHECK(!video_stop(&d) && !sim_calls);
    CHECK(!video_query(&d,&info) && !info.active && info.current_mode==VIDEO_MODE_NONE);
    CHECK(!video_mode_info(&d,0,&mode) && mode.id!=0);
    CHECK(!video_framebuffer(&d,&fb) && fb.bytes==307200);
    CHECK(video_start(&d,mode.id+1,sim_rgb)==OS32_ERR_INVAL && !sim_calls);
    CHECK(video_start(&d,mode.id,0)==OS32_ERR_INVAL && !sim_calls);
    display.bars[1].length=307199;
    CHECK(video_start(&d,mode.id,sim_rgb)==OS32_ERR_INVAL && !sim_calls);
    display.bars[1].length=0x10000000;
    CHECK(!video_start(&d,mode.id,sim_rgb));
    CHECK(display.active && display.initialized && display.baseline_valid && display.reusable);
    CHECK(sim_acquires==1 && sim_releases==1 && sim_validates==1);
    u32 first_calls=sim_calls,first_points=sim_points,first_waits=sim_waits,first_checks=sim_check_reads;
    u8 latency=display.mode.cr[0x52];
    CHECK(!video_query(&d,&info) && info.active && info.current_mode==mode.id);
    CHECK(!video_blank(&d,1) && display.blank);
    u32 before=sim_calls;CHECK(!video_blank(&d,1) && sim_calls==before);
    CHECK(!video_blank(&d,0) && !display.blank);
    CHECK(!video_palette(&d,255,1,sim_rgb));
    CHECK(video_palette(&d,255,2,sim_rgb)==OS32_ERR_INVAL);
    CHECK(!video_display_start(&d,0) && video_display_start(&d,1)==OS32_ERR_NOSYS);
    CHECK(!video_stop(&d) && !display.active && display.stop_ok && display.reusable);
    CHECK(!*sim_register(0x1850) && !*sim_register(0x1854) && !*sim_register(0x140));
    before=sim_calls;CHECK(!video_stop(&d) && sim_calls==before);
    /* Poison live CR52: a restart must still use the main4 baseline. */
    sim_vga[0][0x52]=0xf0;
    CHECK(!video_start(&d,mode.id,sim_rgb) && sim_acquires==1 && sim_validates==3);
    CHECK(display.mode.cr[0x52]==latency);
    CHECK(!video_stop(&d));
    /* Every actual bus failure in a full first start must revoke reuse and
     * finish cleanup/quiesce; failure position comes from the successful run. */
    for(u32 i=1;i<=first_calls;i++) {
        sim_reset();sim_fail=i;
        CHECK(video_start(&d,mode.id,sim_rgb)==-1);
        CHECK(!display.active && !display.reusable && !display.load_context && !display.init && !display.program);
        CHECK(sim_releases==1 && sim_restores>=1);
        before=sim_calls;CHECK(video_start(&d,mode.id,sim_rgb)==-1 && sim_calls==before);
    }
    for(u32 i=1;i<=first_points;i++) {
        sim_reset();sim_point_fail=i;
        CHECK(video_start(&d,mode.id,sim_rgb)==-1 && !display.reusable && !display.active);
    }
    for(u32 i=1;i<=first_waits;i++) {
        sim_reset();sim_wait_fail=i;
        CHECK(video_start(&d,mode.id,sim_rgb)==-1 && !display.reusable && !display.active);
    }
    for(u32 i=1;i<=first_checks;i++) {
        sim_reset();sim_check_mismatch=i;
        int rc=video_start(&d,mode.id,sim_rgb);
        CHECK(i==2 ? rc==0 : rc==-1); /* 510 is evidence-only (mask zero). */
        if (i!=2) CHECK(!display.active && !display.reusable && display.section==GF_DISPLAY_CHECK);
    }
    sim_reset();sim_acquire_fail=1;CHECK(video_start(&d,mode.id,sim_rgb)==-1 && sim_releases==1);
    sim_reset();CHECK(!video_start(&d,mode.id,sim_rgb));CHECK(!video_stop(&d));
    sim_validate_fail=1;CHECK(video_start(&d,mode.id,sim_rgb)==-1 && !display.reusable && sim_acquires==1);
    /* Stop attempts all three writes even if cleanup or a quiesce write fails. */
    for(u32 i=1;i<=3;i++) {
        sim_reset();CHECK(!video_start(&d,mode.id,sim_rgb));sim_fail=sim_calls+i;
        before=sim_calls;CHECK(video_stop(&d)==-1 && sim_calls==before+3 && !display.reusable);
    }
    sim_reset();CHECK(!video_start(&d,mode.id,sim_rgb));sim_restore_fail=1;
    before=sim_calls;CHECK(video_stop(&d)==-1 && sim_calls==before+3 && !display.reusable);
    info=(struct video_info){1,1,1,1};
    CHECK(video_query(0,&info)==OS32_ERR_INVAL && !info.caps && !info.mode_count);
    CHECK(video_mode_info(0,0,&mode)==OS32_ERR_INVAL && !mode.width);
    CHECK(video_framebuffer(0,&fb)==OS32_ERR_INVAL && !fb.bytes);
    CHECK(video_start(0,1,sim_rgb)==OS32_ERR_INVAL && video_palette(0,0,1,sim_rgb)==OS32_ERR_INVAL);
    CHECK(video_blank(0,0)==OS32_ERR_INVAL && video_stop(0)==OS32_ERR_INVAL && video_display_start(0,0)==OS32_ERR_INVAL);
    output("PASS service: runner/baseline/load/check, restart, palette/blank/stop, all bus/point/wait failures\n");
}
static void record_cases(void)
{
    /* astra P3 review counterexamples, kept as regressions. */
    struct video_device d={&display,&gf_video_ops};
    sim_reset();CHECK(!video_start(&d,1,sim_rgb));sim_fail=sim_calls+1;
    CHECK(video_palette(&d,0,1,sim_rgb)==-1);
    CHECK(display.target==GF_MODE_DAC_MASK && display.section==GF_DISPLAY_PALETTE && display.last_rc==-1);
    sim_reset();CHECK(!video_start(&d,1,sim_rgb));sim_fail=sim_calls+1;
    CHECK(video_blank(&d,1)==-1);
    CHECK(display.target==RECOVER_PRMVIO+RECOVER_SEQ && display.section==GF_DISPLAY_BLANK && display.last_rc==-1);
    sim_reset();CHECK(!video_start(&d,1,sim_rgb));sim_fail=sim_calls+1;
    CHECK(video_stop(&d)==-1);
    CHECK(display.target==RECOVER_PBUS1850 && display.section==GF_DISPLAY_STOP && display.last_rc==-1);
    sim_reset();CHECK(!video_start(&d,1,sim_rgb));CHECK(!video_stop(&d));
    u32 before=sim_calls;
    CHECK(!video_blank(&d,0) && sim_calls==before);
    CHECK(video_blank(&d,1)==-1 && sim_calls==before);
    /* A parser-valid VIDEO entry (VCO1 20-60MHz, M=1) solves with too large
     * an error; diagnostic C and production start both reject it. */
    u32 at=GF_INIT_PLL_TABLE+4+35*2;
    put(at+GF_PLL_ENT_VCO1_MIN,20,2);put(at+GF_PLL_ENT_VCO1_MAX,60,2);
    put(at+GF_PLL_ENT_VCO1_M_MIN,1,1);put(at+GF_PLL_ENT_VCO1_M_MAX,1,1);
    struct gfinit_pll_limits limits;struct gfinit_mode m;
    const struct gfinit_mode_saved empty={0};
    CHECK(!gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&limits));
    CHECK(!gfinit_mode_calc(&limits,&empty,&m) && m.pll.error>GF_MODE_CLOCK_KHZ/GF_PLL_MARGIN_DIV);
    CHECK(c_append(&composed,&decoded,&profile,0x10000000,&limits,0)==-1);
    sim_reset();
    CHECK(video_start(&d,1,sim_rgb)==-1 && !display.active && !display.reusable &&
          display.section==GF_DISPLAY_CALC);
    fixture();
    output("PASS record: palette/blank/stop failure target+section kept, same blank after stop, PLL error ceiling shared\n");
}
void _start(void)
{
    gf_fixture_init();c_build();
    calc_equivalence();
    row_comparison(0,true);row_comparison(1,true);row_comparison(1,false);
    pll_failure_cases();service_cases();record_cases();
    output(failures?"DISPLAY FAIL\n":"DISPLAY PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");for(;;){}
}
