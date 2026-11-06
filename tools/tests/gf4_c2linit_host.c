/* libc-free C2-links-init; A1 UNIT14 has no product dependency stubs.
 * Link real decoder, PLL parser, runner, save/pll and all op translation units.
 * Independent oracle: nouveau c03c1a1 nvkm/subdev/bios/init.c:539-572
 * (bases),1093-1143 (bus order),400-407 (BIT I),1663-1682 (zero SUB).
 * Fixture addresses are synthetic ROM offsets, not device ROM contents. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#include "../../hwdev/gf/internal.h"

enum {
    C2_ROM_BYTES=4096, C2_EVENTS=32768, C2_REGS=64,
    C2_BIT=0x20, C2_I=0x60, C2_SUB_TABLE=0x80, C2_MACRO_TABLE=0xa0,
    C2_CONDITION_TABLE=0xc0, C2_FLAG_TABLE=0xe0, C2_FLAG_DATA=0xf0,
    C2_MAIN=0x100, C2_SUB=0x200, C2_DIRECT=0x300,
    /* init.c:539-572 and 1093-1143: direct bases, read flag, DATA alias. */
    C2_T0=0x6808b0, C2_T1=0x6808b8, C2_T2=0x6828b0, C2_T3=0x6828b8,
    C2_READ=0x10000, C2_DATA=4,
    C2_ADDR=0x17, C2_MASK=0x3c, C2_VALUE=0xa2, C2_OLD=0xf5,
    /* Independent literal (f5 & 3c) | a2 = b6, init.c:1116. */
    C2_MERGED=0xb6, C2_DEADLINE=100000, C2_NOW=20,
    C2_MACRO_REG=0x400, C2_MACRO_VALUE=0xdecafbad
};
static struct gf4_host c2_host;
static struct gf4_state c2_state;
static struct gf4_host_event c2_events[C2_EVENTS];
static struct gf3_host_reg c2_regs[C2_REGS];
static struct gf_init_context c2_init;
static struct gf_init_program c2_work;
static u8 c2_rom[C2_ROM_BYTES];
static u32 c2_restores, c2_points, c2_point_fail, c2_expire_clock;
static u32 c2_writes_at_expiry;
static int c2_restore_error;
static bool c2_stall_wait;
static void c2_observe(struct gf4_host *h,const struct gf4_host_event *e)
{
    if (e->op==D_SLEEP && c2_stall_wait) h->local_now+=GF_INIT_OPCODE_US;
    if(e->op==D_CLOCK && e->nth==c2_expire_clock) {
        *(u32 *)e->p[1]=C2_DEADLINE;
        c2_writes_at_expiry=h->calls[D_WRITE];
    }
}
static int c2_point(void *owner)
{
    assert(owner==&c2_host);
    return ++c2_points==c2_point_fail ? -1 : 0;
}
static int c2_restore(void *owner, struct gf_init_context *ctx)
{
    assert(owner==&c2_host && ctx==&c2_init);
    c2_restores++;
    int rc=gf_init_restore(ctx);
    return c2_restore_error ? -1 : rc;
}
static void c2_put(u32 off,u32 value,u32 width)
{
    assert(off<=sizeof(c2_rom) && width<=sizeof(c2_rom)-off);
    for(u32 i=0;i<width;i++) c2_rom[off+i]=(u8)(value>>(i*8));
}
static void c2_code(u32 pc,const u8 *code,u32 n)
{
    assert(pc<=sizeof(c2_rom) && n<=sizeof(c2_rom)-pc);
    for(u32 i=0;i<n;i++) c2_rom[pc+i]=code[i];
}
static void c2_reset(void)
{
    gf2_host_memset(c2_rom,0,sizeof(c2_rom));
    gf4_host_init(&c2_host,&c2_state);
    c2_host.events=c2_events; c2_host.cevents=C2_EVENTS;
    c2_host.base.regs=c2_regs; c2_host.base.cregs=C2_REGS;
    c2_host.local_now=C2_NOW;
    c2_restores=c2_points=c2_point_fail=c2_expire_clock=0; c2_restore_error=0;
    c2_stall_wait=false;
    c2_writes_at_expiry=0; c2_host.observe=c2_observe;
    c2_init=(struct gf_init_context){.rom=c2_rom,.rom_size=sizeof(c2_rom),
        .started_us=C2_NOW-1,
        .io={.opaque=&c2_host,.read=gf4_host_read,.write=gf4_host_write,
             .timing={gf4_host_clock,gf4_host_wait},.point=c2_point,.restore=c2_restore}};
    const u8 signature[]={0xff,0xb8,'B','I','T',0,0};
    c2_code(C2_BIT,signature,sizeof(signature));
    c2_rom[C2_BIT+8]=12; c2_rom[C2_BIT+9]=6; c2_rom[C2_BIT+10]=1;
    c2_rom[C2_BIT+12]='I'; c2_rom[C2_BIT+13]=1;
    c2_put(C2_BIT+14,12,2); c2_put(C2_BIT+16,C2_I,2);
    c2_put(C2_I,C2_SUB_TABLE,2); c2_put(C2_I+4,C2_MACRO_TABLE,2);
    c2_put(C2_I+6,C2_CONDITION_TABLE,2); c2_put(C2_I+10,C2_FLAG_TABLE,2);
    c2_put(C2_MACRO_TABLE,C2_MACRO_REG,4);
    c2_put(C2_MACRO_TABLE+4,C2_MACRO_VALUE,4);
}
static int c2_run(u8 head,u8 or_mask,u32 dl)
{
    u32 started=c2_init.started_us;
    int rc=gf_init_run_script(&c2_init,&c2_work,C2_MAIN,head,or_mask,dl);
    assert(c2_init.started_us==started && c2_init.deadline==dl);
    assert(c2_init.last_rc==rc);
    return rc;
}
static void c2_io(u32 nth,enum gf4_host_op kind,u32 off,u32 value)
{
    u32 seen=0;
    for(u32 i=0;i<c2_host.nevents;i++) {
        const struct gf4_host_event *e=&c2_events[i];
        if(e->op!=D_READ && e->op!=D_WRITE) continue;
        if(seen++!=nth) continue;
        assert(e->op==kind && e->a[0]==0 && e->a[1]==off && e->a[2]==4);
        if(kind==D_WRITE) assert(e->a[3]==value);
        return;
    }
    assert(0);
}
static void c2_tmds(void)
{
    /* Direct 0..3 and raw OR masks are independent of logical head. */
    const u8 selectors[]={0,1,2,3,0x80,0x80,0x80,0x80,0x80,0x81,0x81,0x81};
    const u8 ors[]={1,1,1,1,1,2,4,8,12,4,2,8};
    const u32 bases[]={C2_T0,C2_T1,C2_T2,C2_T3,C2_T0,C2_T1,C2_T2,C2_T3,C2_T2,C2_T3,C2_T0,C2_T2};
    for(u32 k=0;k<sizeof(selectors);k++) for(u32 head=0;head<2;head++) {
        c2_reset();
        const u8 code[]={0x4f,selectors[k],C2_ADDR,C2_MASK,C2_VALUE,
                         0x50,selectors[k],1,0x11,0x63,0x71};
        c2_code(C2_MAIN,code,sizeof(code));
        gf3_host_set(&c2_host.base,0,bases[k]+C2_DATA,C2_OLD);
        assert(c2_run(head,ors[k],C2_DEADLINE)==0 && !c2_restores);
        assert(c2_init.head==head && c2_init.or_mask==ors[k]);
        assert(c2_host.calls[D_WRITE]==5 && c2_host.calls[D_READ]==1);
        c2_io(0,D_WRITE,bases[k],C2_ADDR|C2_READ);
        c2_io(1,D_READ,bases[k]+C2_DATA,0);
        c2_io(2,D_WRITE,bases[k]+C2_DATA,C2_MERGED);
        c2_io(3,D_WRITE,bases[k],C2_ADDR);
        c2_io(4,D_WRITE,bases[k]+C2_DATA,0x63);
        c2_io(5,D_WRITE,bases[k],0x11);
        /* Success cleanup belongs to caller, exactly once. */
        assert(!c2_init.io.restore(&c2_host,&c2_init) && c2_restores==1);
    }
    c2_reset();
    const u8 head_code[]={0x53,0x11,7,0x71};
    c2_code(C2_MAIN,head_code,sizeof(head_code));
    assert(c2_run(1,1,C2_DEADLINE)==0);
    assert(c2_host.calls[D_WRITE]==2);
    assert(c2_host.cr[1][0x11]==7 && !c2_host.cr[0][0x11]);
}
static void c2_controls(void)
{
    c2_reset();
    const u8 code[]={0x33,2,0x6b,0,0x36,0x5b,0,3,0x6b,1,
                     0x38,0x4f,0xff,0,0,0,0x50,0xff,1,0,0,0x72,0x71};
    const u8 sub[]={0x6f,0,0x71};
    c2_code(C2_MAIN,code,sizeof(code)); c2_code(C2_SUB,sub,sizeof(sub));
    c2_code(C2_DIRECT,sub,sizeof(sub));
    c2_put(C2_SUB_TABLE,C2_SUB,2); /* SUB1 remains zero, must be skipped. */
    assert(c2_run(1,4,C2_DEADLINE)==0 && c2_host.calls[D_WRITE]==3);
    assert(!c2_host.calls[D_READ] && !c2_init.calls && !c2_init.repeats);
    for(u32 i=0;i<3;i++) c2_io(i,D_WRITE,C2_MACRO_REG,C2_MACRO_VALUE);
    /* The decoder must follow BIT I references even when S1 index limits
     * would refuse this SUB. Self recursion exhausts the existing stack. */
    c2_reset(); const u8 recursive[]={0x6b,0,0x71};
    c2_code(C2_MAIN,recursive,sizeof(recursive)); c2_put(C2_SUB_TABLE,C2_MAIN,2);
    assert(c2_run(0,1,C2_DEADLINE)==-1 && c2_init.calls==GF_INIT_CALL_DEPTH);
    assert(c2_restores==1 && !c2_host.calls[D_WRITE]);
    /* Shared callee decoded once, repeat linkage local to each column. */
    c2_reset(); const u8 shared[]={0x5b,0,3,0x5b,0,3,0x71};
    c2_code(C2_MAIN,shared,sizeof(shared)); c2_code(C2_DIRECT,sub,sizeof(sub));
    assert(c2_run(0,1,C2_DEADLINE)==0 && c2_work.count==5);
    assert(c2_host.calls[D_WRITE]==2);
    /* CONDITION and IO_FLAG use +6/+10, MACRO uses +4; skipped macro
     * stays skipped until RESUME. init.c:400-407,479-529,1727-1744. */
    c2_reset(); const u8 conditions[]={0x75,0,0x6f,0,0x72,0x39,0,0x6f,0,0x71};
    c2_code(C2_MAIN,conditions,sizeof(conditions));
    c2_put(C2_CONDITION_TABLE,C2_MACRO_REG,4);
    c2_put(C2_CONDITION_TABLE+4,1,4); c2_put(C2_CONDITION_TABLE+8,1,4);
    c2_put(C2_FLAG_TABLE,RECOVER_CRTC,2); c2_rom[C2_FLAG_TABLE+2]=0x11;
    c2_rom[C2_FLAG_TABLE+3]=1; c2_put(C2_FLAG_TABLE+5,C2_FLAG_DATA,2);
    c2_rom[C2_FLAG_TABLE+7]=1; c2_rom[C2_FLAG_TABLE+8]=1;
    c2_rom[C2_FLAG_DATA]=1;
    assert(c2_run(1,1,C2_DEADLINE)==0 && c2_host.calls[D_WRITE]==2);
    assert(c2_host.calls[D_READ]==2 && !c2_init.skip);
}
static void c2_errors(void)
{
    const u8 good[]={0x50,0,1,0x11,0x63,0x71};
    for(u32 bad=0;bad<8;bad++) {
        c2_reset(); c2_code(C2_MAIN,good,sizeof(good));
        if(bad==0) c2_rom[C2_MAIN]=0xff;
        if(bad==1) c2_init.rom_size=C2_MAIN+4; /* truncated pair */
        if(bad==2) c2_rom[C2_BIT+14]=10; /* I payload too short */
        if(bad==3) c2_put(C2_BIT+16,C2_ROM_BYTES-1,2); /* I out of ROM */
        if(bad==4) c2_rom[C2_MAIN]=0x36; /* unmatched END_REPEAT */
        if(bad==5) { const u8 v[]={0x33,1,0x71}; c2_code(C2_MAIN,v,sizeof(v)); }
        if(bad==6) { const u8 v[]={0x5b,0xff,0xff,0x71}; c2_code(C2_MAIN,v,sizeof(v)); }
        if(bad==7) c2_rom[C2_BIT+13]=2; /* unsupported BIT I version */
        assert(c2_run(0,1,C2_DEADLINE)==-1 && c2_restores==1);
        assert(!c2_host.calls[D_READ] && !c2_host.calls[D_WRITE]);
    }
    c2_reset(); c2_code(C2_MAIN,good,sizeof(good)); c2_rom[C2_MAIN+1]=4;
    assert(c2_run(0,1,C2_DEADLINE)==-1 && c2_restores==1);
    assert(!c2_host.calls[D_WRITE]);
    /* Capacity and local repeat depth are checked before device writes. */
    c2_reset();
    for(u32 n=0;n<GF_INIT_INSNS;n++) c2_rom[C2_MAIN+n]=0x38;
    c2_rom[C2_MAIN+GF_INIT_INSNS]=0x71;
    assert(c2_run(0,1,C2_DEADLINE)==-1 && c2_restores==1);
    assert(!c2_host.calls[D_WRITE]);
    c2_reset();
    for(u32 n=0;n<=GF_INIT_REPEAT_DEPTH;n++) {
        c2_rom[C2_MAIN+2*n]=0x33; c2_rom[C2_MAIN+2*n+1]=1;
    }
    assert(c2_run(0,1,C2_DEADLINE)==-1 && c2_restores==1);
    assert(!c2_host.calls[D_WRITE]);
    /* A branch into an operand must fail during preparation, even if the
     * operand happens to equal DONE. No side effects before rejection. */
    c2_reset(); const u8 operand[]={0x50,0,1,0x71,0,0x5b,3,1,0x71};
    c2_code(C2_MAIN,operand,sizeof(operand));
    assert(c2_run(0,1,C2_DEADLINE)==-1 && !c2_host.calls[D_WRITE]);
    /* Invalid entry must retain the pending save and avoid restore. */
    c2_reset(); c2_init.saved_count=1;
    assert(gf_init_run_script(&c2_init,&c2_work,C2_MAIN,0,1,C2_DEADLINE)==-1);
    assert(c2_init.saved_count==1 && !c2_restores && !c2_points);
    c2_init.saved_count=0;
    assert(gf_init_run_script(&c2_init,&c2_work,0,0,1,C2_DEADLINE)==-1);
    c2_init.io.read=0;
    assert(gf_init_run_script(&c2_init,&c2_work,C2_MAIN,0,1,C2_DEADLINE)==-1);
    assert(!c2_restores && !c2_points);
    /* Partial MMIO must stop immediately, preserve original EIO even when
     * cleanup fails; no second DATA/address write may follow the error. */
    for(u32 nth=1;nth<=2;nth++) {
        c2_reset(); c2_code(C2_MAIN,good,sizeof(good));
        gf4_host_fail_at(&c2_host,D_WRITE,nth,-733); c2_restore_error=1;
        assert(c2_run(1,4,C2_DEADLINE)==-1 && c2_restores==1);
        assert(c2_host.calls[D_WRITE]==nth);
    }
}
static void c2_deadlines(void)
{
    const u8 wait[]={0x74,0xd1,7,0x50,0,1,0x11,0x63,0x71}; /* 2001us = two chunks + 1 */
    c2_reset(); c2_code(C2_MAIN,wait,sizeof(wait));
    assert(c2_run(1,4,C2_DEADLINE)==0 && c2_host.calls[D_SLEEP]==3);
    assert(c2_init.wait_us==2001 && c2_host.local_now==C2_NOW+2001);
    /* Expiry injected at EVERY prepare/instruction/chunk boundary must
     * prevent all later MMIO, even with cleanup failure. */
    u32 clocks=c2_host.calls[D_CLOCK];
    for(u32 nth=1;nth<=clocks;nth++) {
        c2_reset(); c2_code(C2_MAIN,wait,sizeof(wait));
        c2_expire_clock=nth; c2_restore_error=1;
        assert(c2_run(1,4,C2_DEADLINE)==GF_SCRIPT_ETIME && c2_restores==1);
        assert(c2_host.calls[D_CLOCK]==nth && c2_host.calls[D_WRITE]==c2_writes_at_expiry);
    }
    for(u32 nth=1;nth<=3;nth++) {
        c2_reset(); c2_code(C2_MAIN,wait,sizeof(wait));
        gf4_host_fail_at(&c2_host,D_SLEEP,nth,-735);
        assert(c2_run(1,4,C2_DEADLINE)==-1 && c2_restores==1);
        assert(c2_host.calls[D_SLEEP]==nth && c2_init.wait_us==2001);
    }
    c2_reset(); c2_code(C2_MAIN,wait,sizeof(wait)); c2_restore_error=1;
    assert(c2_run(1,4,C2_NOW+1000)==GF_SCRIPT_ETIME && c2_restores==1);
    assert(c2_host.calls[D_SLEEP]==1 && c2_init.wait_us==2001);
    assert(c2_init.fail_pc==C2_MAIN);
    /* RESET's saved register is cleaned up even after deadline expiry.
     * A failed cleanup write retains the table for the existing owner. */
    const u8 reset[]={0x65,0,4,0,0,1,0,0,0,2,0,0,0,0x71};
    for(u32 fail=0;fail<2;fail++) {
        c2_reset(); c2_code(C2_MAIN,reset,sizeof(reset));
        gf3_host_set(&c2_host.base,0,RECOVER_PBUS184C,0xabcd0f55U);
        if(fail) gf4_host_fail_at(&c2_host,D_WRITE,3,-736);
        assert(c2_run(0,1,C2_NOW+GF_INIT_RESET_US)==GF_SCRIPT_ETIME);
        assert(c2_restores==1 && c2_host.calls[D_WRITE]==3 &&
               c2_init.saved_count==fail && c2_init.fail_pc==C2_MAIN);
    }
    /* dl=0 is an absolute wrapped deadline, never an unlimited sentinel. */
    c2_reset(); c2_rom[C2_MAIN]=0x71;
    assert(c2_run(0,1,0)==GF_SCRIPT_ETIME && c2_restores==1);
    assert(!c2_host.calls[D_WRITE] && !c2_points);
    c2_reset(); c2_rom[C2_MAIN]=0x71; c2_host.local_now=0xfffffff0U;
    c2_init.started_us=0xfffffff0U;
    assert(c2_run(0,1,0)==0);
    c2_reset(); c2_rom[C2_MAIN]=0x71; c2_init.started_us=C2_NOW-GF_INIT_EXEC_US;
    assert(c2_run(0,1,C2_DEADLINE)==GF_SCRIPT_ETIME);
    /* Total wait ceiling is distinct from the absolute/elapsed deadline.
     * REPEAT accepts 1..254 (shared decoder contract). 76 full u16 waits
     * fit in 5s; the 77th must fail before any wait callback/accounting. */
    /* 76 repeats of one u16 wait (0xffff us): wait = 76*65535 us; each wait
     * is split into 66 sleep callbacks; steps = REPEAT + 76*(WAIT+END) + the
     * refused 77th REPEAT body entry. */
    enum { C2_WAIT_REPS=76U, C2_WAIT_ONE_US=65535U, C2_SLEEPS_PER_WAIT=66U };
    const u32 c2_wait_total=C2_WAIT_REPS*C2_WAIT_ONE_US;
    const u32 c2_sleep_total=C2_WAIT_REPS*C2_SLEEPS_PER_WAIT;
    const u32 c2_wait_steps=1U+C2_WAIT_REPS*2U+1U;
    c2_reset(); const u8 wait_budget[]={0x33,76,0x74,0xff,0xff,0x36,0x71};
    c2_code(C2_MAIN,wait_budget,sizeof(wait_budget));
    assert(c2_run(0,1,GF_INIT_EXEC_US)==0 && !c2_restores);
    assert(c2_init.wait_us==c2_wait_total && c2_host.calls[D_SLEEP]==c2_sleep_total);
    assert(c2_host.local_now==C2_NOW+c2_wait_total);
    c2_reset(); c2_code(C2_MAIN,wait_budget,sizeof(wait_budget));
    c2_rom[C2_MAIN+1]=77;
    assert(c2_run(0,1,GF_INIT_EXEC_US)==GF_SCRIPT_ETIME && c2_restores==1);
    assert(c2_init.wait_us==c2_wait_total && c2_host.calls[D_SLEEP]==c2_sleep_total);
    assert(c2_host.local_now==C2_NOW+c2_wait_total &&
           c2_init.elapsed_us<GF_INIT_EXEC_US && c2_init.steps==c2_wait_steps);
    assert(c2_init.fail_pc==C2_MAIN+2 && !c2_host.calls[D_WRITE]);
    /* Step cap uses ETIME; call-stack cap uses EIO. */
    c2_reset(); const u8 budget[]={0x33,254,0x33,254,0x63,0x36,0x36,0x71};
    c2_code(C2_MAIN,budget,sizeof(budget));
    assert(c2_run(0,1,C2_DEADLINE)==GF_SCRIPT_ETIME);
    assert(c2_init.steps==GF_INIT_SCHEDULE && c2_restores==1);
    c2_reset(); c2_code(C2_MAIN,wait,sizeof(wait));
    gf4_host_fail_at(&c2_host,D_CLOCK,1,-734);
    assert(c2_run(0,1,C2_DEADLINE)==-1 && c2_restores==1);
    c2_reset(); c2_code(C2_MAIN,wait,sizeof(wait)); c2_point_fail=1;
    assert(c2_run(0,1,C2_DEADLINE)==-1 && c2_restores==1);
}
/* TMDS: many individually valid TIME opcodes may exceed both the caller
 * deadline and the old cumulative wait/elapsed caps. F5 cases above retain
 * their fixed outer deadline. */
static void c2_opcode_budgets(void)
{
    c2_reset();
    const u8 waits[]={0x33,100,0x74,0xff,0xff,0x36,0x71};
    c2_code(C2_MAIN,waits,sizeof(waits));
    c2_init.opcode_timing=true;
    c2_init.opcode_started_us=C2_NOW;
    c2_init.started_us=C2_NOW-GF_INIT_EXEC_US;
    assert(!c2_run(1,4,C2_NOW+1000));
    assert(c2_init.wait_us==100U*65535U && !c2_restores);
    /* A stalled single operation is still rejected, with unconditional cleanup. */
    c2_reset(); c2_code(C2_MAIN,waits,sizeof(waits));
    c2_init.opcode_timing=true;
    c2_init.opcode_started_us=C2_NOW;
    c2_stall_wait=true;
    assert(c2_run(1,4,C2_NOW+1000)==GF_SCRIPT_ETIME && c2_restores==1);
}
void _start(void)
{
    c2_tmds(); c2_controls(); c2_errors(); c2_deadlines(); c2_opcode_budgets();
    gf2_host_exit(0);
}
