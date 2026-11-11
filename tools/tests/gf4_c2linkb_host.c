/* C2-links-b: A1 UNIT14, libc-free; real select/callback/runner/B10.
 * A2 links the real F4/F3/V0 dependencies of links.c plus runner.c,
 * save.c, pll.c, decoder/PLL helpers and op translation units, GF_PRODUCTION_MODULE,
 * -m32 -nostdlib with section GC. Do not separately compile links.c.
 * GF4_LINKS_SOURCE permits runtime mutants without changing this oracle.
 * Sources: MIT nouveau-v6.1 nouveau_bios.c:66-106,627-680,904-962;
 * nouveau c03c1a1 dispnv04/dfp.c:68-90,446-460, init.c:539-572,1093-1143.
 * ROM offsets below are synthetic. Expected PCs, base addresses and write
 * sequences are independently tabulated from those sources, not product
 * calculations. No reference checkout or libc is needed at runtime. */
#include "gf_startlog_host.h"
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#include "../../hwdev/gf/production.h"
#include "../../drivers/video/geforce/geforce_module.h"
#ifndef GF4_LINKS_SOURCE
#define GF4_LINKS_SOURCE "../../hwdev/gf/display/links.c"
#endif
#include GF4_LINKS_SOURCE

/* Test storage, synthetic offsets, and independent register oracle. */
enum {
    ROM_BYTES=4096, TRACE_CAPACITY=4096, REG_CAPACITY=128,
    BIT_OFF=0x20, T_DATA=0x60, I_DATA=0x70, T_TABLE=0x90,
    CLOCK_A=0xc0, CLOCK_B=0xe0, PC_HIGH=0x100, PC_LOW=0x180,
    PC_OTHER=0x200, PC_WAIT=0x280, PC_BINDING=0x300,
    BIT_HEADER_BYTES=12, BIT_ENTRY_BYTES=6, T_POINTER_BYTES=2,
    I_POINTER_BYTES=12, T_VERSION=0x11,
    /* nouveau_bios.c:66-91: 10000/20000 units of 10kHz, zero row. */
    HIGH_THRESHOLD=20000, LOW_THRESHOLD=10000, HIGH_KHZ=200000,
    LOW_KHZ=100000, ROM_SENTINEL=0xfeed,
    /* nouveau_bios.c:673-677; dfp.c:68-90. */
    SEL_CLK=0x680524, BIND_BITS=0x50000, SEL_INITIAL=0x5abcd,
    /* Script binding nibble6 becomes7 using saved nibble5's bits0/2;
     * every other bit remains from the script (nouveau_bios.c:673-677). */
    SEL_SCRIPT=0x98765432, SEL_RESTORED=0x98775432,
    TMDS_A=0x6808b0, TMDS_B=0x6808b8, TMDS_C=0x6828b0,
    TMDS_DATA_DELTA=4, BIND_INDEX=4, BIND_VALUE_A=0x88, BIND_VALUE_C=0x80,
    CR_PORT=0x6013d4, CR_OWNER=0x44, OWNER_B=3, STRIDE=0x2000,
    SCRIPT_INDEX=0x17, SCRIPT_VALUE=0xa2, OTHER_VALUE=0x63,
    NOW=20, DEADLINE=1000000, DUAL_EDGE=165000,
    /* CR44 index/data; one TMDS pair; one CR index/data; SEL_CLK cleanup.
     * nouveau_bios.c:93-106,673-677; init.c:1121-1143,1196-1208. */
    OWNER_WRITES=2, GROUP_WRITES=2, CR_WRITES=2, CLEANUP_WRITES=1,
    BIND_WRITES=4, CALLBACK_WRITES=OWNER_WRITES+GROUP_WRITES+CR_WRITES+BIND_WRITES+CLEANUP_WRITES,
    VGA_WIDTH=640, XRGB_CPP=4, INJECT=-733
};
static struct gf4_host h;
static struct gf4_state s;
static struct gf4_host_event trace[TRACE_CAPACITY];
static struct gf3_host_reg registers[REG_CAPACITY];
static struct gf_display_context legacy;
static struct gf_init_context script_ctx;
static struct gf_init_program script_work;
static u8 rom[ROM_BYTES];
static const struct gf4_exec_ops *ops;
static u32 loans,returns,restores,points,tracked_owner_writes,runner_writes;
static bool loaned,fail_acquire,fail_restore,expire_acquire;
static u32 expire_clock,writes_at_expiry,events_at_acquire;
static bool expiry_origin;
static u32 steps_at_release,head_at_release,or_at_release,dl_at_release,pc_at_release;
static GeforceModuleContext module_context;
const GeforceModuleContext *geforce_context=&module_context;
static struct gf4_route route={.connector=0,.head=1,.type=GF4_TMDS,
                               .or_mask=4,.head_mask=3};
static void put(u32 off,u32 value,u32 width)
{
    assert(off<=ROM_BYTES && width<=ROM_BYTES-off);
    for (u32 i=0;i<width;i++) rom[off+i]=(u8)(value>>(i*8));
}
static void code(u32 off,const u8 *data,u32 n)
{
    assert(off<=ROM_BYTES && n<=ROM_BYTES-off);
    for (u32 i=0;i<n;i++) rom[off+i]=data[i];
}
static void observe(struct gf4_host *host,const struct gf4_host_event *e)
{
    if (e->op==D_CLOCK && e->nth==expire_clock) {
        /* Keep the fake clock monotonic, including after an origin sample.
         * A new opcode owns its full budget; a sample inside it may expire. */
        host->local_now+=GF_INIT_OPCODE_US;
        *(u32 *)e->p[1]=host->local_now;
        expiry_origin=e->p[1]==&script_ctx.opcode_started_us;
        writes_at_expiry=host->calls[D_WRITE];
    }
}
static int loan_read(void *owner,u32 bar,u32 off,u32 width,u32 *value)
{
    assert(loaned);
    return gf4_host_read(owner,bar,off,width,value);
}
static int tracked_write(void *owner,u32 bar,u32 off,u32 width,u32 value)
{
    assert(loaned);
    if (off==CR_PORT+1U && h.index[0][0]==CR_OWNER) ++tracked_owner_writes;
    ++runner_writes;
    return gf4_host_write(owner,bar,off,width,value);
}
static int point(void *owner)
{
    assert(owner==&h && loaned); ++points; return 0;
}
static int restore(void *owner,struct gf_init_context *ctx)
{
    assert(owner==&h && loaned); ++restores;
    int rc=gf_init_restore(ctx);
    if (fail_restore || rc) { legacy.reusable=false; return -1; }
    return 0;
}
static int acquire(struct gf_display_context *l)
{
    assert(l==&legacy && l->initialized && !loaned && !l->init && !l->program);
    loaned=true; ++loans; events_at_acquire=h.nevents;
    script_ctx=(struct gf_init_context){.io=l->io,.rom=rom,
        .rom_size=ROM_BYTES,.started_us=NOW};
    l->init=&script_ctx; l->program=&script_work;
    if (expire_acquire) h.local_now=DEADLINE;
    return fail_acquire ? -1 : 0;
}
static void release(struct gf_display_context *l)
{
    assert(l==&legacy && loaned && restores==loans);
    steps_at_release=script_ctx.steps; head_at_release=script_ctx.head;
    or_at_release=script_ctx.or_mask; dl_at_release=script_ctx.deadline;
    pc_at_release=script_work.count ? script_work.insns[0].pc : 0;
    /* Poison the loan: callback must not read init/work/ROM after release. */
    gf2_host_memset(&script_ctx,0xa5,sizeof(script_ctx));
    gf2_host_memset(&script_work,0xa5,sizeof(script_work));
    l->init=0; l->program=0; loaned=false; ++returns;
}
static const struct gf_display_owner owner={.acquire=acquire,.release=release};
static void fixture(void)
{
    startlog_clear();
    gf2_host_memset(&s,0,sizeof(s));
    gf2_host_memset(&script_work,0,sizeof(script_work));
    gf2_host_memset(rom,0,sizeof(rom));
    gf4_host_init(&h,&s); h.events=trace; h.cevents=TRACE_CAPACITY;
    h.base.regs=registers; h.base.cregs=REG_CAPACITY; h.local_now=NOW;
    h.observe=observe;
    loans=returns=restores=points=tracked_owner_writes=runner_writes=0;
    loaned=fail_acquire=fail_restore=expire_acquire=false;
    expire_clock=writes_at_expiry=events_at_acquire=0;
    expiry_origin=false;
    steps_at_release=head_at_release=or_at_release=dl_at_release=pc_at_release=0;
    legacy=(struct gf_display_context){.initialized=true,.reusable=true,.owner=&owner,
        .io={.opaque=&h,.read=loan_read,.write=tracked_write,
             .timing={gf4_host_clock,gf4_host_wait},.point=point,.restore=restore}};
    h.ctx.legacy=&legacy; legacy.display4=&h.ctx;
    gf4_links_table()->owner=&h.ctx; ops=gf4_exec_table(); h.ctx.exec=ops;
    assert(ops->tmds_script);
    route=(struct gf4_route){.connector=0,.head=1,.type=GF4_TMDS,
                            .or_mask=4,.head_mask=3};
    h.ctx.routes=&route; h.ctx.nroutes=1;
    /* Nonzero owned metadata must survive every script success/failure. */
    h.ctx.scan[0][0].value.reservation_id=11;
    h.ctx.cursor[1].value.reservation_id=12;
    h.ctx.test.value.reservation_id=13;
    h.ctx.hold[0].old_front.reservation_id=14;
    const u8 signature[]={0xff,0xb8,'B','I','T',0,0};
    code(BIT_OFF,signature,sizeof(signature));
    rom[BIT_OFF+8]=BIT_HEADER_BYTES; rom[BIT_OFF+9]=BIT_ENTRY_BYTES;
    rom[BIT_OFF+10]=2;
    rom[BIT_OFF+12]='T'; rom[BIT_OFF+13]=1;
    put(BIT_OFF+14,T_POINTER_BYTES,2); put(BIT_OFF+16,T_DATA,2);
    rom[BIT_OFF+18]='I'; rom[BIT_OFF+19]=1;
    put(BIT_OFF+20,I_POINTER_BYTES,2); put(BIT_OFF+22,I_DATA,2);
    put(T_DATA,T_TABLE,2); rom[T_TABLE]=T_VERSION;
    put(T_TABLE+11,CLOCK_A,2); put(T_TABLE+13,CLOCK_B,2);
    put(CLOCK_A,HIGH_THRESHOLD,2); put(CLOCK_A+2,PC_HIGH,2);
    put(CLOCK_A+4,LOW_THRESHOLD,2); put(CLOCK_A+6,PC_LOW,2);
    put(CLOCK_A+8,0,2); put(CLOCK_A+10,PC_OTHER,2);
    put(CLOCK_B,0,2); put(CLOCK_B+2,PC_OTHER,2);
    /* ZM_TMDS_GROUP selector80: raw OR; CRTC write: logical head. */
    const u8 high[]={0x50,0x80,1,SCRIPT_INDEX,SCRIPT_VALUE,0x53,0x11,7,0x71};
    const u8 low[]={0x50,0x81,1,SCRIPT_INDEX,OTHER_VALUE,0x71};
    const u8 other[]={0x50,0x80,1,SCRIPT_INDEX,OTHER_VALUE,0x53,0x11,9,0x71};
    const u8 wait[]={0x74,0xe8,3,0x71}; /* TIME 1000us, init.c:1777-1794. */
    code(PC_HIGH,high,sizeof(high)); code(PC_LOW,low,sizeof(low));
    code(PC_OTHER,other,sizeof(other)); code(PC_WAIT,wait,sizeof(wait));
    /* ZM_REG SEL_CLK changes every bit; cleanup must retain non-binding bits. */
    rom[PC_BINDING]=0x7a; put(PC_BINDING+1,SEL_CLK,4);
    put(PC_BINDING+5,SEL_SCRIPT,4); rom[PC_BINDING+9]=0x71;
    gf3_host_set(&h.base,0,SEL_CLK,SEL_INITIAL);
}
static void selected(u32 khz,u32 expected)
{
    u32 pc=ROM_SENTINEL;
    assert(!gf4_tmds_script_select(rom,ROM_BYTES,&route,khz,&pc));
    assert(pc==expected && !h.nevents);
}
static void rejected(u32 size,i32 expected)
{
    u32 pc=ROM_SENTINEL;
    assert(gf4_tmds_script_select(rom,size,&route,LOW_KHZ,&pc)==expected);
    assert(pc==ROM_SENTINEL && !h.nevents);
}
static void selection(void)
{
    fixture(); route.or_mask=1;
    selected(HIGH_KHZ-1,PC_LOW); selected(HIGH_KHZ,PC_HIGH);
    selected(HIGH_KHZ+1,PC_HIGH); selected(LOW_KHZ,PC_LOW);
    selected(LOW_KHZ-1,PC_OTHER); selected(0,PC_OTHER);
    /* Head does not select a different clock table; lowest OR bit does. */
    route.head=0; selected(HIGH_KHZ,PC_HIGH);
    route.or_mask=3; selected(HIGH_KHZ,PC_HIGH);
    route.or_mask=2; selected(HIGH_KHZ,PC_OTHER);
    route.or_mask=4; selected(HIGH_KHZ,PC_OTHER);
    route.or_mask=12; selected(HIGH_KHZ,PC_OTHER);
    route.or_mask=8; rejected(ROM_BYTES,GF4_ENODEV);
    route.or_mask=0; rejected(ROM_BYTES,GF4_EINVAL);
    route.or_mask=16; rejected(ROM_BYTES,GF4_EINVAL);
    fixture(); route.head=2; rejected(ROM_BYTES,GF4_EINVAL);
    fixture(); route.head_mask=1; rejected(ROM_BYTES,GF4_EINVAL);
    fixture(); route.type=GF4_ANALOG; rejected(ROM_BYTES,GF4_EINVAL);
    fixture(); route.location=1; rejected(ROM_BYTES,GF4_EUNSUPPORTED);
    fixture(); route.ext_type=1; rejected(ROM_BYTES,GF4_EUNSUPPORTED);
    fixture(); rom[BIT_OFF]=0; rejected(ROM_BYTES,GF4_ENODEV);
    fixture(); rom[BIT_OFF+14]=1; rejected(ROM_BYTES,GF4_EUNSUPPORTED);
    fixture(); put(T_DATA,0,2); rejected(ROM_BYTES,GF4_ENODEV);
    fixture(); rom[T_TABLE]=0x20; rejected(ROM_BYTES,GF4_EUNSUPPORTED);
    fixture(); put(T_DATA,ROM_BYTES,2); rejected(ROM_BYTES,GF4_ERANGE);
    fixture(); put(BIT_OFF+16,ROM_BYTES-1,2); rejected(ROM_BYTES,GF4_ERANGE);
    fixture(); rejected(T_TABLE+14,GF4_ERANGE);
    fixture(); put(T_TABLE+13,0,2); rejected(ROM_BYTES,GF4_ENODEV);
    fixture(); put(T_TABLE+13,ROM_BYTES-3,2); rejected(ROM_BYTES,GF4_ERANGE);
    fixture(); put(CLOCK_B+2,0,2); rejected(ROM_BYTES,GF4_ENODEV);
    fixture(); put(CLOCK_B+2,ROM_BYTES,2); rejected(ROM_BYTES,GF4_ERANGE);
    fixture(); route.or_mask=1;
    /* No zero threshold before ROM end: finite bounds failure. */
    put(T_TABLE+11,ROM_BYTES-4,2); put(ROM_BYTES-4,HIGH_THRESHOLD,2);
    rejected(ROM_BYTES,GF4_ERANGE);
    u32 pc=ROM_SENTINEL;
    assert(gf4_tmds_script_select(0,ROM_BYTES,&route,0,&pc)==GF4_EINVAL);
    assert(gf4_tmds_script_select(rom,ROM_BYTES,0,0,&pc)==GF4_EINVAL);
    assert(gf4_tmds_script_select(rom,ROM_BYTES,&route,0,0)==GF4_EINVAL);
    assert(pc==ROM_SENTINEL);
}
static i32 run(u32 khz,u32 dl)
{
    struct gf4_ctx before=h.ctx;
    i32 rc=ops->tmds_script(ops->owner,&route,khz,dl);
    assert(!gf2_host_memcmp(h.ctx.scan,before.scan,sizeof(before.scan)));
    assert(!gf2_host_memcmp(h.ctx.cursor,before.cursor,sizeof(before.cursor)));
    assert(!gf2_host_memcmp(&h.ctx.test,&before.test,sizeof(before.test)));
    assert(!gf2_host_memcmp(h.ctx.hold,before.hold,sizeof(before.hold)));
    assert(h.ctx.last_rc==rc && !loaned && !legacy.init && !legacy.program);
    assert(loans==returns && restores==loans);
    return rc;
}
static void write_is(u32 nth,u32 off,u32 width,u32 value)
{
    u32 seen=0;
    for (u32 i=0;i<h.nevents;i++) if (trace[i].op==D_WRITE) {
        if (seen++!=nth) continue;
        assert(trace[i].a[0]==0 && trace[i].a[1]==off &&
               trace[i].a[2]==width && trace[i].a[3]==value);
        return;
    }
    assert(false);
}
static void callback(void)
{
    const u8 ors[]={1,2,4};
    const u32 bases[]={TMDS_A,TMDS_B,TMDS_C};
    const u32 pcs[]={PC_HIGH,PC_OTHER,PC_OTHER};
    const u32 values[]={SCRIPT_VALUE,OTHER_VALUE,OTHER_VALUE};
    for (u32 head=0;head<GF4_HEADS;head++) for (u32 n=0;n<sizeof(ors);n++) {
        fixture(); route.head=head; route.or_mask=ors[n];
        assert(!run(HIGH_KHZ,DEADLINE));
        assert(loans==1 && points && head_at_release==head && or_at_release==ors[n]);
        assert(dl_at_release==DEADLINE && pc_at_release==pcs[n]);
        assert(tracked_owner_writes==1 && h.calls[D_WRITE]==CALLBACK_WRITES);
        write_is(0,CR_PORT,1,CR_OWNER); write_is(1,CR_PORT+1,1,head ? OWNER_B : 0);
        write_is(2,bases[n]+TMDS_DATA_DELTA,4,values[n]);
        write_is(3,bases[n],4,SCRIPT_INDEX);
        write_is(4,CR_PORT+head*STRIDE,1,0x11);
        write_is(5,CR_PORT+head*STRIDE+1,1,n ? 9 : 7);
        write_is(10,SEL_CLK,4,SEL_INITIAL);
    }
    fixture(); route.or_mask=1;
    assert(!run(HIGH_KHZ-1,DEADLINE));
    assert(pc_at_release==PC_LOW && h.calls[D_WRITE]==OWNER_WRITES+GROUP_WRITES+BIND_WRITES+CLEANUP_WRITES);
    /* selector81 uses OR1's other bank: init.c:539-572. */
    write_is(2,TMDS_B+TMDS_DATA_DELTA,4,OTHER_VALUE);
    write_is(3,TMDS_B,4,SCRIPT_INDEX);
}
static u32 register_value(u32 off)
{
    for (u32 i=0;i<h.base.nregs;i++)
        if (!registers[i].bar && registers[i].off==off) return registers[i].value;
    assert(false); return 0;
}
static void binding_and_repeat(void)
{
    fixture(); put(CLOCK_B+2,PC_BINDING,2);
    assert(!run(HIGH_KHZ,DEADLINE));
    assert(register_value(SEL_CLK)==SEL_RESTORED);
    assert(tracked_owner_writes==1 && h.ctx.reusable && legacy.reusable);
    write_is(2,SEL_CLK,4,SEL_SCRIPT); write_is(7,SEL_CLK,4,SEL_RESTORED);
    assert(!run(HIGH_KHZ,DEADLINE));
    assert(loans==2 && returns==2 && restores==2);
    assert(register_value(SEL_CLK)==SEL_RESTORED);
}
static void failures(void)
{
    fixture(); assert(run(HIGH_KHZ,NOW)==GF4_ETIME && !loans && !h.calls[D_WRITE]);
    assert(startlog_equal("GF4 ERROR stage=script_before_acquire rc=-409 head=1 remain_us=0 reg=00000000 val=00000000\n"));
    fixture(); assert(run(HIGH_KHZ,0)==GF4_ETIME && !loans);
    fixture(); fail_acquire=true;
    assert(run(HIGH_KHZ,DEADLINE)==-1 && loans==1 && !points && !h.calls[D_WRITE]);
    fixture(); expire_acquire=true;
    assert(!run(HIGH_KHZ,DEADLINE) && loans==1 && points && h.calls[D_WRITE]);
    fixture(); rom[T_TABLE]=0x20; fail_restore=true;
    assert(run(HIGH_KHZ,DEADLINE)==GF4_EUNSUPPORTED && !points);
    assert(!h.ctx.reusable && !legacy.reusable);
    fixture(); rom[PC_OTHER]=0xff; fail_restore=true;
    assert(run(HIGH_KHZ,DEADLINE)==-1 && !h.ctx.reusable);
    assert(h.calls[D_WRITE]==3); /* raw CR44 pair, then binding cleanup */
    fixture(); fail_restore=true;
    assert(run(HIGH_KHZ,DEADLINE)==-1 && !h.ctx.reusable);
    fixture(); route.location=1;
    assert(run(HIGH_KHZ,DEADLINE)==GF4_EUNSUPPORTED && !points && !h.calls[D_WRITE]);
    fixture();
    assert(ops->tmds_script(0,&route,HIGH_KHZ,DEADLINE)==GF4_EINVAL && !loans);
    /* Every bus failure gets a normal counterexample above. No script
     * DATA/address follows its failure; binding cleanup still executes. */
    for (u32 n=1;n<=CALLBACK_WRITES;n++) {
        fixture(); gf4_host_fail_at(&h,D_WRITE,n,INJECT);
        assert(run(HIGH_KHZ,DEADLINE)==-1);
        if (n<=2) assert(!points && h.calls[D_WRITE]==n+1);
        if (n==3 || n==4) assert(h.calls[D_WRITE]==n+1);
        if (n==CALLBACK_WRITES) assert(!h.ctx.reusable && !legacy.reusable);
    }
    for (u32 n=1;n<=2;n++) {
        fixture(); gf4_host_fail_at(&h,D_READ,n,INJECT);
        assert(run(HIGH_KHZ,DEADLINE)==-1);
        if (n==1) assert(!h.calls[D_WRITE] && !points);
        else assert(!h.ctx.reusable);
    }
    fixture(); put(CLOCK_B+2,PC_WAIT,2);
    assert(!run(HIGH_KHZ,DEADLINE));
    u32 clocks=h.calls[D_CLOCK];
    for (u32 n=1;n<=clocks;n++) {
        fixture(); put(CLOCK_B+2,PC_WAIT,2); expire_clock=n;
        i32 rc=run(HIGH_KHZ,DEADLINE);
        assert(rc==(expiry_origin ? 0 : GF4_ETIME));
        if (!expiry_origin && n>2) {
            assert(gf2_host_strstr(startlog,"GF4 ERROR stage=rom_script rc=-409 head=1 remain_us="));
            assert(gf2_host_strstr(startlog," pc="));
        }
        /* Preparation/opcode origins start a fresh operation. Rejection
         * inside an operation permits only the binding cleanup write. */
        assert(h.calls[D_WRITE]==(expiry_origin ? OWNER_WRITES+BIND_WRITES+CLEANUP_WRITES :
               writes_at_expiry+(n>2 ? CLEANUP_WRITES : 0U)));
    }
    for (u32 n=1;n<=clocks;n++) {
        fixture(); put(CLOCK_B+2,PC_WAIT,2);
        gf4_host_fail_at(&h,D_CLOCK,n,INJECT);
        /* The post-acquire clock is called directly and preserves its rc;
         * runner and pre-acquire clock failures retain their -1 contract. */
        assert(run(HIGH_KHZ,DEADLINE)==(n==2 ? INJECT : -1));
        assert(h.calls[D_WRITE]==(n>2 ? OWNER_WRITES+CLEANUP_WRITES : 0U));
    }
}
static void b10(void)
{
    const u32 clocks[]={DUAL_EDGE-1,DUAL_EDGE,DUAL_EDGE+1};
    const u32 counts[]={1,2,2}; /* nouveau_bios.c:674 */
    for (u32 n=0;n<sizeof(clocks)/sizeof(clocks[0]);n++) {
        fixture();
        struct gf4_head_image *image=&s.head[route.head];
        image->enabled=true; image->route=route;
        image->mode=(struct gf4_mode){.id=1,.fmt=GF3_XRGB8888,.pitch=VGA_WIDTH*XRGB_CPP,
            .t={clocks[n],640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V}};
        h.cr[0][CR_OWNER]=0x5a; /* arbitrary old owner must survive B10. */
        struct gfinit_mode mode={0};
        assert(!gf4_tmds_load(&h.ctx,&route,&mode,DEADLINE));
        assert(loans==1 && returns==1 && restores==1 && h.cr[0][CR_OWNER]==0x5a);
        u32 binds=0;
        for (u32 i=0;i<h.nevents;i++) if (trace[i].op==D_WRITE &&
            trace[i].a[2]==4 && trace[i].a[3]==BIND_INDEX &&
            (trace[i].a[1]==TMDS_C || trace[i].a[1]==TMDS_C+8U)) {
            assert(i>events_at_acquire && trace[i-1].op==D_WRITE);
            const struct gf4_host_event *data=&trace[i-1];
            assert(data->a[1]==(binds ? TMDS_C+12U : TMDS_C+4U));
            assert(data->a[3]==(binds ? BIND_VALUE_A : BIND_VALUE_C));
            ++binds;
        }
        assert(binds==counts[n] && tracked_owner_writes==1);
    }
}
static void lvds_binding(void)
{
    const u32 clocks[]={DUAL_EDGE-1,DUAL_EDGE+1};
    for (u32 n=0;n<2;n++) for (u32 dual=0;dual<2;dual++) {
        fixture(); route.type=GF4_LVDS; route.dual_link=dual!=0;
        loaned=true;
        assert(!links_bind_head(&legacy.io,&route,clocks[n]));
        assert(h.calls[D_WRITE]==(dual ? 4U : 2U));
        write_is(0,TMDS_C+4U,4,BIND_VALUE_C|1U);
        write_is(1,TMDS_C,4,BIND_INDEX);
        if (dual) {
            write_is(2,TMDS_C+12U,4,BIND_VALUE_A|1U);
            write_is(3,TMDS_C+8U,4,BIND_INDEX);
        }
    }
}
void _start(void)
{
    selection(); callback(); binding_and_repeat(); failures(); b10(); lvds_binding();
    gf2_host_exit(0);
}
