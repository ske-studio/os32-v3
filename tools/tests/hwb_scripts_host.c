/* Real executor against indexed VGA, DAC and byte-addressed LFB models. */
#include "hwb_fake.h"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_exec.c"
#include "userland/shell/hwb_log.c"
/* SCRIPTS_DATA */
static struct hwb_program pg;
static struct hwb_state st;
static char source[HWB_FILE_BYTES];
static u8 vram[307200], cr[256], sr[256], gr[256], ar[32], rgb[768];
static u32 ci, si, gi, ai, af, pal, hidden, reads6, hidden_ok, pal_done;
static u32 fills, fill_ends, bytes, loglines, writes, points, fail_at, abort_at, tick;
static u32 enabled, disabled, clock_a, clock_b, pll_written, painted, alias_bus;
static u32 capacity=7, cr_c1, ref_pal, pci_reads, io_count, dac_select, ext_dac, dac_writes;
static u32 io_addr[10], io_value[10];
static u32 init_addr[64], init_val[64], init_width[64], init_count, init_record, config_bit;
static char fill_end[HWB_LOG_BYTES];
static const char *input;
static u32 misc, mask_dac, readpal, active_mode, save_done, restore_started;
static u32 read_error, initial_misc;
static u8 active_cr[256], active_sr[256], active_gr[256], active_ar[32], active_rgb[768];
struct access { u32 action, addr, width, value; };
static struct access saved_trace[4096], restored_trace[4096];
static u32 saved_count, restored_count, trace_pos, active_clock_a, active_clock_b;
static void copy(u8 *d,const u8 *s,u32 n) { for(u32 k=0;k<n;k++) d[k]=s[k]; }
static void snapshot(void) {
    active_clock_a=clock_a; active_clock_b=clock_b; copy(active_cr,cr,256); copy(active_sr,sr,256); copy(active_gr,gr,256);
    copy(active_ar,ar,32); copy(active_rgb,rgb,768);
}
static void remember(struct hw_req *r) {
    if(input!=scripts[7]) return;
    struct access *a=0;
    if(restore_started) { CHECK(restored_count<4096); a=&restored_trace[restored_count++]; }
    else if(!save_done) { CHECK(saved_count<4096); a=&saved_trace[saved_count++]; }
    if(a) { a->action=r->action; a->addr=r->addr-0x20400000; a->width=r->width; a->value=r->value; }
}

static u32 length(const char *s) { u32 n=0; while(s[n]) n++; return n; }
static int contains(const char *s, const char *p) {
    for (;*s;s++) { u32 n=0; while(p[n] && s[n]==p[n]) n++; if(!p[n]) return 1; } return 0;
}
static int bus(void *ctx, struct hw_req *r) {
    (void)ctx; r->status=HW_OK;
    if(r->space==HW_PCI) {
        CHECK(r->action==HW_READ); pci_reads++;
        r->value=r->addr==0x10 ? 0x20000000 : r->addr==0x14 ? 0x20400000 : 0;
        return 0;
    }
    if(r->action==HW_WRITE) {
        if(++writes==fail_at) { r->status=HW_IO_ERROR; return 0; }
    }
    if(r->space==HW_IO) {
        if(input==scripts[7] && r->addr==0x68 && r->value==0x0F && active_mode!=2) {
            snapshot(); restore_started=1;
        }
        if(r->addr==0x68 && r->value==0x0E) CHECK(sr[0]==3 && !(sr[1]&0x20));
        CHECK(io_count<10); io_addr[io_count]=r->addr; io_value[io_count++]=r->value;
        if(r->addr==0xFAC) {
            if(r->value==2) {
                CHECK(cr[0x23]==0x8A && cr[0x29]==0x87 && gr[0x23]==0xFC);
                CHECK(ext_dac==0x8F && dac_writes==3);
                CHECK(hidden_ok && !painted && !pal_done && sr[0]==3 && !(sr[1]&0x20));
                enabled++; CHECK(!disabled);
            } else { CHECK(r->value==0); disabled++; }
        }
        return 0;
    }
    if(r->addr < 0x20000000) { /* arbitrary physical fill fixture */
        CHECK(r->action==HW_WRITE); CHECK(r->addr+r->width<=sizeof(vram));
        for(u32 k=0;k<r->width;k++) vram[r->addr+k]=r->value>>(8*k);
        return 0;
    }
    if(r->addr < 0x20400000) {
        if(input==scripts[7]) CHECK(enabled==1);
        u32 off=r->addr-0x20000000;
        if(alias_bus) off=0;
        if(off>=sizeof(vram)) off=0;
        if(r->action==HW_READ) {
            r->value=0; for(u32 k=0;k<r->width;k++) r->value|=(u32)vram[off+k]<<(8*k);
        } else {
            CHECK(sr[0]==3); CHECK(off+r->width<=sizeof(vram));
            for(u32 k=0;k<r->width;k++) vram[off+k]=r->value>>(8*k);
        }
        return 0;
    }
    u32 a=r->addr-0x20400000, val=r->value;
    if(r->action==HW_READ) {
        r->value=a==0x3D5 || a==0x3B5 ? cr[ci] : a==0x3C5 ? sr[si] : a==0x3CF ? gr[gi] : 0;
        if(a==0x83C6) { CHECK(dac_select==4); dac_select=0; r->value=ext_dac; }
        if(a==0x3DA || a==0x3BA) af=0;
        if(a==0x3C1) r->value=ar[ai];
        if(a==0x3CC) r->value=misc;
        if(a==0x43C8) r->value=clock_a;
        if(a==0x43C9) r->value=clock_b;
        if(a==0x3C9) { CHECK(readpal<768); r->value=rgb[readpal++];
            if(read_error && readpal>=4 && readpal<=6 && pal==6) r->value^=1;
        }
        if(a==0x3C8) reads6=0;
        if(a==0x3C6) { r->value=reads6>=4?hidden:mask_dac; reads6++; }
        remember(r); return 0;
    }
    if(a==0x3C2 && val==0xC3 && !init_count) { init_record=1; save_done=1; }
    remember(r);
    if(init_record) {
        if(init_count<64) { init_addr[init_count]=a; init_val[init_count]=val;
            init_width[init_count++]=r->width; }
        if(a==0x3D5 && ci==0x11 && (val&0x80)) init_record=0;
    }
    for(u32 k=0;k<r->width;k++,a++,val>>=8) {
        u8 x=val;
        if(a==0x83C8) dac_select=x;
        if(a==0x83C6) { CHECK(dac_select==4); dac_select=0; ext_dac=x; dac_writes++; }
        if(a==0x3D4 || a==0x3B4) ci=x;
        if(a==0x3D5 || a==0x3B5) cr[ci]=x;
        if(a==0x3C4) si=x;
        if(a==0x3C5) sr[si]=x;
        if(a==0x3CE) gi=x;
        if(a==0x3CF) gr[gi]=x;
        if(a==0x3C0) { if(!af) { ai=x&31; af=1; } else { ar[ai]=x; af=0; } }
        if(a==0x43C8) { clock_a=x; pll_written=1; }
        if(a==0x43C9) clock_b=x;
        if(a==0x3C6) {
            if(reads6==4) { hidden=x; hidden_ok=1; reads6=0; }
            else { CHECK(x==255); mask_dac=x; }
        }
        if(a==0x3C2) misc=x;
        if(a==0x3C7) { readpal=x*3; reads6=0; }
        if(a==0x3C8) { pal=x*3; reads6=0; }
        if(a==0x3C9) { CHECK(pal<768); rgb[pal++]=x; if(pal==24 && enabled && !disabled) pal_done=1; }
    }
    return 0;
}
static int point(void *ctx,u32 *flag) { (void)ctx; *flag=++points==abort_at; return 0; }
static int now(void *ctx,u32 *lo,u32 *hi) { (void)ctx; *lo=tick; *hi=0; tick+=1000000; return 0; }
static void yield(void *ctx) { (void)ctx; }
static void line(void *ctx,const char *s) {
    (void)ctx; bytes+=length(s)+2; loglines++;
    if(contains(s,"phase=begin op=mmiofill")) { fills++; CHECK(contains(s,"count=")); }
    if(contains(s,"phase=end op=mmiofill")) {
        fill_ends++; CHECK(contains(s,"done="));
        u32 k=0; do { fill_end[k]=s[k]; } while(s[k++]);
    }
    if(contains(s,"tag=\"bottom-right\"")) painted=1;
}
static int file(void *ctx,const char *path,char *buf,u32 cap,u32 *n) {
    (void)ctx;(void)path; *n=length(input); CHECK(*n<=cap);
    for(u32 k=0;k<*n;k++) buf[k]=input[k];
    return 0;
}
static int find(void *ctx,u32 v,u32 d,u32 start,u32 *b,u32 cap) {
    (void)ctx;(void)v;(void)d;(void)start;(void)b;(void)cap; return 0;
}
static struct hwb_backend backend={0,0,bus,now,yield,point,line,file,find};
static enum hwb_rc run(const char *s,u32 mode) {
    input=s; active_mode=mode; hwb_init(&st,&backend);
    struct hwb_initial init={"mode",mode};
    return hwb_run(&st,&pg,source,sizeof(source),"script",&init,1);
}
static void reset(void) {
    hwbf_zero(vram,sizeof(vram)); hwbf_zero(cr,sizeof(cr)); hwbf_zero(sr,sizeof(sr));
    hwbf_zero(gr,sizeof(gr)); hwbf_zero(ar,sizeof(ar)); hwbf_zero(rgb,sizeof(rgb));
    ci=si=gi=ai=af=pal=hidden=reads6=hidden_ok=pal_done=0;
    fills=fill_ends=bytes=loglines=writes=points=fail_at=abort_at=tick=0;
    enabled=disabled=clock_a=clock_b=pll_written=painted=alias_bus=pci_reads=io_count=0;
    init_count=init_record=save_done=restore_started=saved_count=restored_count=0;
    misc=initial_misc; readpal=0; mask_dac=255; sr[0x0C]=config_bit;
    /* Nonzero, distinct initial values expose lost high palette entries. */
    for(u32 k=0;k<768;k++) rgb[k]=(k*13+7)&255;
    hidden=0x5A; clock_a=0x37; clock_b=0x19; sr[0x0E]=0x24; sr[0x0D]=0x17;
    fill_end[0]=0; dac_select=dac_writes=0; ext_dac=0x80;
    cr[0x29]=0xB3; cr[0x23]=0xAA; cr[0x2F]=0x81; cr[0x2A]=0x12;
    cr[0x39]=0xFF; gr[0x2F]=0x80; gr[0x0F]=0xA8; gr[0x23]=0xFE;
    cr[0x3A]=0xAA; cr[0x50]=0xBB; gr[6]=0x0C;
    cr[0x11]=0x95; sr[1]=0x0A;
    sr[9]=0x10; cr[0x1F]=capacity; cr[0xC1]=cr_c1; cr[0xC0]=ref_pal ? 0x80:0;
}
static void fill_tests(void) {
    HWB_TEST("fill-values"); reset();
    CHECK(run("mmiofill8 phys 0 3 0x12\nmmiofill16 phys 4 (1+2) 0x3456\nmmiofill32 phys 12 2 0x89ABCDEF\nend\n",0)==HWB_OK);
    CHECK(st.writes==8 && fills==3 && fill_ends==3 && loglines==9);
    for(u32 k=0;k<20;k++) {
        u8 expected=k<3?0x12:k==3||k==10||k==11?0:k<10?((k&1)?0x34:0x56):
                    (0x89ABCDEFU>>((k&3)*8));
        CHECK(vram[k]==expected);
    }
    CHECK(contains(fill_end,"count=2 done=2 rc=OK"));
    HWB_TEST("fill-window"); reset(); sr[0]=3;
    CHECK(run("device d = bdf 7:3.1\nwindow f = bar d 0\nmmiofill16 f + 16 3 0x5678\nend",0)==HWB_OK);
    CHECK(pci_reads==2 && st.writes==3 && vram[16]==0x78 && vram[21]==0x56);
    CHECK(contains(fill_end,"addr=0x20000010 val=0x5678 count=3 done=3 rc=OK"));
    reset(); sr[0]=3;
    CHECK(run("device d = bdf 7:3.1\nmmiofill32 bar d 0 + 0 1 0x12345678",0)==HWB_OK);
    CHECK(pci_reads==2 && st.writes==1 && vram[3]==0x12);
    HWB_TEST("fill-invalid");
    const char *bad[]={"mmiofill8 phys 0 0 1", "mmiofill8 phys 0 4294967296 1",
        "mmiofill16 phys 1 2 1", "mmiofill8 phys 0 1 256",
        "mmiofill32 phys 0xFFFFFFFC 2 1", "mmiofill8 phys 0xFFFFFFFF 2 1"};
    for(u32 k=0;k<sizeof(bad)/sizeof(*bad);k++) {
        reset(); CHECK(run(bad[k],0)==HWB_ARG); CHECK(writes==0 && st.writes==0);
    }
    HWB_TEST("fill-failure"); reset(); fail_at=3;
    CHECK(run("mmiofill8 phys 0 5 7\nmmiowrite8 phys 99 1",0)==HWB_IO);
    CHECK(st.writes==2 && writes==3 && fills==1 && fill_ends==1 && vram[99]==0);
    CHECK(contains(fill_end,"count=5 done=2 rc=IO"));
    HWB_TEST("fill-abort"); reset(); abort_at=5; /* load, instruction, element 0,1,2 */
    CHECK(run("mmiofill8 phys 0 5 7\nmmiowrite8 phys 99 1",0)==HWB_ABORT);
    CHECK(st.writes==2 && writes==2 && fills==1 && fill_ends==1 && vram[99]==0);
    CHECK(contains(fill_end,"count=5 done=2 rc=ABORT"));
}
/* Derive VGA timing and KGA from 640/656/752/800,480/490/492/525.
 * No expected CRTC table copied from the script or design. */
static void timing(void) {
    u32 ht=800, hd=640, hs=656, he=752, vt=525, vd=480, vs=490, ve=492;
    u32 hb=(ht>>3)-1, hstart=(hd>>3)-1, vb=vt-1;
    u8 expected[25]={0};
    if(hb-- > (hstart&63)) hb=0;
    expected[0]=(ht>>3)-5; expected[1]=expected[2]=hstart;
    expected[3]=0x80|(hb&31); expected[4]=hs>>3;
    expected[5]=((hb&32)<<2)|((he>>3)&31);
    expected[6]=vt-2;
    expected[7]=(((vt-2)&256)>>8)|(((vd-1)&256)>>7)|((vs&256)>>6)|
        (((vd-1)&256)>>5)|16|(((vt-2)&512)>>4)|(((vd-1)&512)>>3)|((vs&512)>>2);
    expected[9]=(((vd-1)&512)>>4)|64;
    expected[16]=vs; expected[17]=(ve&15)|32; expected[18]=vd-1;
    expected[19]=hd>>3; expected[21]=vd-1;
    if(vb>vd-1 && (vb&127)>((vd-1)&127)) vb=0; else vb--;
    expected[22]=vb; expected[23]=0xC3; expected[24]=255;
    for(u32 k=0;k<25;k++) CHECK(cr[k]==expected[k]);
}
static void pll(void) {
    u32 best=750, a=0, b=0, ref=ref_pal?17734480U:14318180U;
    for(u32 k=0;k<=1;k++) for(u32 n=0;n<=121;n++) for(u32 m=1;m<=31;m++) {
        u32 freq=(ref*(n+8)/((m+2)*(1U<<k)))/1000;
        u32 delta=freq>25175?freq-25175:25175-freq;
        if(delta<best) {best=delta; a=((m&1)<<7)|n; b=((m&254)>>1)|(k<<4);}
    }
    CHECK(best<750 && clock_a==a && clock_b==b);
}
static void route(u32 mode) {
    u32 addr[]={0x68,0x6A,0x6A,0x6A,0xFAC,0xFAC,0x6A,0x6A,0x6A,0x68};
    u32 val[]={0x0E,7,0x8F,6,2,0,7,0x8E,6,0x0F};
    CHECK(io_count==(mode==2?5U:10U));
    CHECK(ext_dac==0x80 && dac_writes==(mode==2?5U:8U));
    for(u32 k=0;k<io_count;k++) {
        u32 j=mode==2?k+5:k;
        CHECK(io_addr[k]==addr[j] && io_value[k]==val[j]);
    }
}
/* Driver 1.4.0 lines 625-671: writes from subsystem enable through MCLK.
 * Each tuple is BAR1 offset, value, width; configuration branch is explicit. */
static void init_trace(void) {
    static const u32 common[][3]={{0x3C2,0xC3,1},{0x3C4,0x0B,1},
        {0x3C4,0x000B,2},{0x3C4,0x0E,1},{0x3C4,0x240E,2},{0x3C4,0x0C,1},
        {0x3C4,0x0E,1}};
    static const u32 low[][3]={{0x46E8,0x10,1},{0x102,1,1},{0x46E8,8,1}};
    static const u32 high[][3]={{0x94,0,1},{0x102,1,1},{0x94,0x20,1},{0x3C3,1,1}};
    static const u32 tail[][3]={{0x3C0,0x10,1},{0x3C0,0x41,1},
        {0x3D4,0x11,1},{0x3D4,0x11,1},{0x3D5,0x15,1},
        {0x3C4,0x0B,1},{0x3C4,0x0E,1},{0x3C4,0xA40E,2},
        {0x3D4,0x3820,2},{0x3D4,0x2020,2},{0x3D4,0x0E30,2},
        {0x3D4,0x0031,2},{0x3D4,0x0032,2},{0x3D4,0x213B,2},
        {0x43C6,0xAF,1},{0x43C7,0,1},
        {0x3C4,0x0B,1},{0x3C4,0x0E,1},{0x3C4,0x240E,2},
        {0x3D4,0x11,1},{0x3D4,0x11,1},{0x3D5,0x95,1}};
    u32 n=0;
#define TRACE(tab) do { for(u32 j=0;j<sizeof(tab)/sizeof((tab)[0]);j++,n++) { \
    CHECK(n<init_count && init_addr[n]==(tab)[j][0] && \
          init_val[n]==(tab)[j][1] && init_width[n]==(tab)[j][2]); } } while(0)
    TRACE(common); if(config_bit) { TRACE(high); } else { TRACE(low); }
    TRACE(tail); CHECK(n==init_count);
#undef TRACE
}
/* Independent access contract: driver:1084-1136, DAC:763-1048,
 * VGA:845-892,1016-1140. Compare every operation, including read order. */
static struct access *trace;
static u32 trace_count;
static void want(u32 action,u32 addr,u32 width,u32 value) {
    CHECK(trace_pos<trace_count);
    struct access *a=&trace[trace_pos++];
    CHECK(a->action==action && a->addr==addr && a->width==width);
    if(action==HW_WRITE) CHECK(a->value==value);
}
static void rd(u32 a) { want(HW_READ,a,1,0); }
static void wr(u32 a,u32 v) { want(HW_WRITE,a,1,v); }
static void ri(u32 p,u32 i) { wr(p,i); rd(p+1); }
static void wi(u32 p,u32 i,u32 v) { want(HW_WRITE,p,2,i|(v<<8)); }
static void palette_off(u32 cb) { rd(cb+6); wr(0x3C0,32); }
static u8 initial_cr[256],initial_gr[256],initial_sr[256],initial_ar[32];
static void protect_trace(u32 on) {
    wr(0x3C4,1); rd(0x3C5);
    if(on) { wr(0x3C4,0); wr(0x3C5,1); }
    wr(0x3C4,1); wr(0x3C5,on?(active_sr[1]|32):(initial_sr[1]&~32));
    if(!on) { wr(0x3C4,0); wr(0x3C5,3); }
    rd(on || initial_misc?0x3DA:0x3BA); wr(0x3C0,on?0:32);
}
static void state_trace(void) {
    u32 cb=initial_misc?0x3D4:0x3B4; /* Saved VGA side; active mode is color. */
    static const u8 save_cr[]={0x13,0x21,0x1E,0x27,0x2B,0x2F,0x2A,
                               0x3A,0x29,0x38,0x36,0x39,0x55,0x50};
    static const u8 restore_cr[]={0x1E,0x21,0x50,0x27,0x2B,0x29,0x36,
                                  0x2F,0x2A,0x3A,0x38,0x39,0x55};
    trace=saved_trace; trace_count=saved_count; trace_pos=0;
    /* Identification precedes Save; locate the first colormap mask write. */
    while(trace_pos<trace_count && !(trace[trace_pos].action==HW_WRITE &&
          trace[trace_pos].addr==0x3C6)) trace_pos++;
    wr(0x3C6,255); wr(0x3C7,0);
    for(u32 k=0;k<6;k++) rd(0x3C9);
    wr(0x3C8,1);
    for(u32 k=3;k<6;k++) wr(0x3C9,((k*13+7)^63)&63);
    wr(0x3C7,1); for(u32 k=0;k<3;k++) rd(0x3C9);
    wr(0x3C8,1); for(u32 k=3;k<6;k++) wr(0x3C9,(k*13+7)&255);
    wr(0x3C7,2);
    for(u32 k=6;k<768;k++) { rd(0x3C9); rd(cb+6); rd(cb+6); }
    palette_off(cb); rd(0x3CC);
    for(u32 k=0;k<25;k++) ri(cb,k);
    rd(cb+6); wr(0x3C0,0);
    for(u32 k=0;k<21;k++) { rd(cb+6); wr(0x3C0,k); rd(0x3C1); }
    palette_off(cb);
    for(u32 k=0;k<9;k++) ri(0x3CE,k);
    for(u32 k=1;k<5;k++) ri(0x3C4,k);
    ri(0x3C4,11); ri(0x3C4,14); wi(0x3C4,14,0xC2);
    for(u32 k=0;k<sizeof(save_cr);k++) ri(cb,save_cr[k]);
    ri(0x3CE,15); ri(0x3CE,47);
    rd(0x3C8); for(u32 k=0;k<5;k++) rd(0x3C6);
    rd(0x3C8); rd(0x3CC); rd(0x43C8); rd(0x43C9);
    ri(0x3C4,13); wi(0x3C4,14,0x24);
    rd(0x3CC); /* First Init read follows the completed Save. */
    CHECK(trace_pos==trace_count); /* Save finishes before first Init write. */
    trace=restored_trace; trace_count=restored_count; trace_pos=0;
    protect_trace(1);
    ri(0x3C4,11); wi(0x3C4,14,0xC2);
    rd(0x3C8); for(u32 k=0;k<4;k++) rd(0x3C6);
    wr(0x3C6,0x5A); rd(0x3C8);
    for(u32 k=0;k<sizeof(restore_cr);k++) {
        if(k==2) wi(0x3C4,13,0x17);
        wi(0x3D4,restore_cr[k],initial_cr[restore_cr[k]]);
    }
    wi(0x3CE,47,initial_gr[47]); wi(0x3CE,15,initial_gr[15]);
    wi(0x3D4,19,initial_cr[19]);
    wr(0x43C8,0x37); wr(0x43C9,0x19); wr(0x3C2,initial_misc);
    wi(0x3C4,14,0x24^2);
    wr(0x3C2,initial_misc);
    for(u32 k=1;k<5;k++) wi(0x3C4,k,initial_sr[k]);
    wi(cb,17,initial_cr[17]&127);
    for(u32 k=0;k<25;k++) wi(cb,k,initial_cr[k]);
    for(u32 k=0;k<9;k++) wi(0x3CE,k,initial_gr[k]);
    rd(cb+6); wr(0x3C0,0);
    for(u32 k=0;k<21;k++) { rd(cb+6); wr(0x3C0,k); wr(0x3C0,initial_ar[k]); }
    palette_off(cb); wr(0x3C6,255); wr(0x3C8,0);
    for(u32 k=0;k<768;k++) { wr(0x3C9,(k*13+7)&255); rd(cb+6); rd(cb+6); }
    palette_off(cb); protect_trace(0);
    CHECK(trace_pos==trace_count);
    for(u32 k=0;k<25;k++) CHECK(cr[k]==initial_cr[k]);
    for(u32 k=0;k<sizeof(save_cr);k++) CHECK(cr[save_cr[k]]==initial_cr[save_cr[k]]);
    for(u32 k=0;k<9;k++) CHECK(gr[k]==initial_gr[k]);
    for(u32 k=1;k<5;k++) CHECK(sr[k]==initial_sr[k]);
    for(u32 k=0;k<21;k++) CHECK(ar[k]==initial_ar[k]);
}

static void mode_tests(void) {
    HWB_TEST("mode");
    for(u32 m=0;m<=4;m++) for(ref_pal=0;ref_pal<2;ref_pal++) for(config_bit=0;config_bit<=0x10;config_bit+=0x10) {
        reset(); copy(initial_cr,cr,256); copy(initial_sr,sr,256);
        copy(initial_gr,gr,256); copy(initial_ar,ar,32);
        CHECK(run(scripts[7],m)==HWB_OK);
        if(m==1 || m==3 || m==4) {
            HWB_TEST("pc98-init"); init_trace();
            HWB_TEST("state-save-restore"); state_trace();
            CHECK(misc==initial_misc && clock_a==0x37 && clock_b==0x19 && hidden==0x5A);
            CHECK(sr[0x0E]==(0x24^2) && sr[0x0D]==0x17);
            CHECK(gr[6]==0x0C && cr[0x3A]==0xAA && cr[0x50]==0xBB);
            for(u32 k=0;k<768;k++) CHECK(rgb[k]==((k*13+7)&255));
            copy(cr,active_cr,256); copy(sr,active_sr,256); copy(gr,active_gr,256);
            copy(ar,active_ar,32); copy(rgb,active_rgb,768);
            clock_a=active_clock_a; clock_b=active_clock_b;
            HWB_TEST("mode");
        } else CHECK(restored_count==0);
        if(m==0) { CHECK(!enabled&&!disabled&&!fills); continue; }
        if(m==2) { CHECK(!enabled && disabled==1&&!fills); route(m); continue; }
        CHECK(enabled==1 && disabled==1);
        if(m==4) { CHECK(!fills); route(m); continue; }
        timing(); pll(); route(m);
        CHECK(st.writes==writes);
        CHECK(cr[0x3A]==0 && cr[0x50]==0 && cr[0x21]==0x20);
        CHECK(gr[6]==(m==3?0x0C:5));
        CHECK(cr[0x1E]==0x80 && sr[0x0D]==0x20 && cr[0x27]==8 && cr[0x2B]==0);
        CHECK(cr[0x29]==0x83 && cr[0x36]==0x80 && cr[0x2F]==0x91 && cr[0x2A]==0x52);
        CHECK(cr[0x38]==0 && cr[0x39]==0xFF && cr[0x55]==0x1F);
        CHECK(gr[0x2F]==0x84 && gr[0x0F]==0xA2 && gr[0x23]==0xFD);
        CHECK(sr[0]==3 && sr[1]==0x11 && sr[2]==15 && sr[3]==0 && sr[4]==14);
        CHECK(clock_a==(ref_pal?0xA4:0xE5) && clock_b==(ref_pal?0x0E:0x1E));
        for(u32 k=0;k<16;k++) CHECK(ar[k]==k);
        CHECK(ar[16]==0x41 && ar[17]==0 && ar[18]==15 && ar[19]==0 && ar[20]==0);
        for(u32 k=0;k<24;k++) CHECK(rgb[k]==(((k/3)>>(k%3))&1?63:0));
        for(u32 y=0;y<480;y++) for(u32 x=0;x<640;x++) {
            u8 color=y>>6;
            if(!x||x==639||!y||y==479 || (x>=16&&x<32&&y>=16&&y<32) ||
                (x>=608&&x<616&&y>=32&&y<64)) color=7;
            CHECK(vram[y*640+x]==color);
        }
        CHECK(fills==8+2+479+16+32 && fill_ends==fills);
        CHECK(bytes<5000000); /* Explicit Save/Restore logging includes 768 DAC components. */
        /* Report exact byte budget once. */
        if(m==1&&!ref_pal) {
            char out[16]; u32 n=0,v=bytes; do {out[n++]='0'+v%10;v/=10;}while(v);
            hwb_test_write("serial bytes="); while(n) {char c[2]={out[--n],0}; hwb_test_write(c);} hwb_test_write("\n");
        }
    }
    config_bit=0;
    HWB_TEST("capacity");
    u32 sizes[]={1048576,524288,6291456,1048576,8388608,1048576,
        10485760,2097152,12582912,1048576,14680064,1048576,
        16777216,1048576,20971520,4194304};
    u32 extended[]={20971520,25165824,29360128,33554432};
    for(u32 k=0;k<16;k++) for(u32 j=0;j<(k==14?4U:1U);j++) {
        capacity=0x70|k; cr_c1=0xEE | (j&1) | ((j&2)<<3);
        reset(); CHECK(run(scripts[7],0)==HWB_OK);
        u32 found=0;
        for(u32 n=0;n<st.var_count;n++) if(hwbf_equal(st.vars[n].name,"capacity")) {
            CHECK(st.vars[n].value==(k==14?extended[j]:sizes[k])); found=1;
        }
        CHECK(found && !enabled && !disabled && !fills);
    }
    capacity=0x7F; cr_c1=0; reset();
    CHECK(run(scripts[7],1)==HWB_OK); CHECK(enabled==1 && fills>0);
    capacity=7;
    HWB_TEST("alias"); reset(); alias_bus=1;
    CHECK(run(scripts[7],1)==HWB_OK); CHECK(enabled==1 && disabled==1 && !fills);
    for(u32 k=0;k<4;k++) CHECK(vram[k]==0);
}
static void unreadable_palette(void) {
    HWB_TEST("palette-default"); reset(); read_error=1;
    CHECK(run(scripts[7],1)==HWB_OK);
    u32 hash=2166136261U;
    for(u32 k=0;k<768;k++) hash=(hash^rgb[k])*16777619U;
    /* FNV-1a of all 768 bytes of VGA defaultDAC, vgaHW.c:90-155. */
    CHECK(hash==0x3CD28647U); read_error=0;
}
static void array_restore(void) {
    HWB_TEST("array-save-restore"); reset(); sr[0]=3;
    for (u32 k=0;k<8;k++) vram[k]=k*17+3;
    CHECK(run("dim save 1024\nlet i = 0\nlabel save_loop\n"
              "mmioread8 phys (0x20000000+i) -> save[i]\nlet i = i + 1\n"
              "if i != 8 goto save_loop\nmmiofill8 phys 0x20000000 8 0\n"
              "let i = 0\nlabel restore_loop\nmmiowrite8 phys (0x20000000+i) save[i]\n"
              "let i = i + 1\nif i != 8 goto restore_loop\nend",0)==HWB_OK);
    CHECK(st.writes==16);
    for (u32 k=0;k<8;k++) CHECK(vram[k]==k*17+3);
}
void _start(void) {
    u32 err;
    HWB_TEST("syntax");
    for(u32 k=0;k<sizeof(scripts)/sizeof(*scripts);k++) CHECK(hwb_parse(&pg,scripts[k],length(scripts[k]),&err)==HWB_OK);
    fill_tests(); array_restore();
    for(initial_misc=0;initial_misc<2;initial_misc++) mode_tests();
    initial_misc=0; unreadable_palette(); hwb_test_write("PASS fill and real mode script\n"); hwb_test_exit(0);
}
