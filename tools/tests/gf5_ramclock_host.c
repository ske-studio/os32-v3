/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
/* B2/U1: freestanding UNIT2, link hwdev/gf/ramclock.c.
 * Golden writes transcribed independently from MIT nouveau c03c1a1
 * nvkm/subdev/fb/ramnv40.c:78-175, default (NV4a) partition.
 * Recreate by following that function with both SR1=3, old mux=0xc123,
 * old ctrl=0x80000100, old coef=0x1401, mc=0x80210100, mn=0x1902.
 * U1 also compiles the real clock/PLL below, so the guard is never a stub.
 * Synthetic v0x21 limits fix N=25/M=2, P=1, bias=1 at 168750 kHz;
 * recreate with bios/pll.c:301-333, clk/nv40.c:146-195 and
 * fb/ramnv40.c:34-63 (MIT c03c1a1). No local oracle files are required. */
#define GF5_HOST_UNIT 2
#define GF5_HOST_REAL_CLOCK
#include "gf5_fake.h"
/* Keep U1 self-contained until U7 owns the shared source registration. */
#include "../../hwdev/gf/clock.c"
#include "../../kdebug/gfinit_pll.c"

/* Register/command golden values: ramnv40.c:78-175. */
#define STRIDE 0x2000U /* ramnv40.c:80 */
#define VBL 0x600808U
#define HIGH 0x10000U
#define SR_INDEX 0xc03c4U
#define SR_DATA 0xc03c5U
#define SR_OLD 3U
#define BLANK 0x20U
#define CTRL 0x4020U
#define COEF 0x4024U
#define MUX 0xc040U
#define AUTO 0x100210U
#define SELF 0x1002dcU
/* ramnv40.c:115-120,145-152: prepare/refresh/mux and settle values. */
#define PRECHARGE 0x1002d4U
#define REFRESH 0x1002d0U
#define AUTO_BIT 0x80000000U
#define MUX_MASK 0xc000U
#define COMMAND 1U
#define WORD 4U
#define BYTE 1U
#define SR1 1U /* ramnv40.c:84,112,172 */
#define HEADS 2U /* ramnv40.c:79 */
#define PROBES 33U /* ramnv40.c:81-94: do/while cnt++ < 32 */
#define SETTLE_US 100U /* ramnv40.c:145,152 */
#define VBLANK_US 2000000U /* ramnv40.c:102,108,166: 2000ms */
/* Synthetic saved state and B1 plan, independent of the product solver. */
#define OLD_CTRL 0x80000100U
#define OLD_COEF 0x1401U
#define OLD_MUX 0xc123U
#define NEW_CTRL 0x80210100U
#define NEW_COEF 0x1902U
#define DEADLINE 8000000U /* Covers both heads' independent 2s waits. */
#define CAP 4096U /* Normal trace plus a bounded injected timeout. */
#define M_PC 0x80U /* Synthetic completed M program identity, not ROM parsing. */
static struct gf5_host h;
static struct gf4_state state;
static struct gf5_host_event events[64];
static struct gf4_host_event display_events[CAP];
static struct gf3_host_reg regs[64];
static struct gf4_host_read reads[80]; /* 2 * (initial + 33 probes + 3 edges). */
static struct gf_init_program program;
#define ROM_ENTRY 31U
#define REQUEST_KHZ 168750U
static u8 rom[GF_INIT_PLL_TABLE+4U+4U*ROM_ENTRY];
static const struct gf5_clock request={REQUEST_KHZ,0,REQUEST_KHZ};
static const struct gf5_plan plan = {
    .nc=0x80010100U,.nn=NEW_COEF,.mux=0x333U,.mc=NEW_CTRL,.mn=NEW_COEF,
    .actual={REQUEST_KHZ,REQUEST_KHZ,REQUEST_KHZ}
};
static u32 write_count, read_count, sleep_count, clock_count, self_depth, protected_heads;
static bool timeout_jump, slow_edges;
struct golden { u32 off,width,value; };
static const struct golden golden[] = {
    {SR_INDEX,BYTE,SR1}, {SR_INDEX+STRIDE,BYTE,SR1},
    {SR_INDEX,BYTE,SR1}, {SR_DATA,BYTE,SR_OLD|BLANK},
    {SR_INDEX+STRIDE,BYTE,SR1}, {SR_DATA+STRIDE,BYTE,SR_OLD|BLANK},
    {PRECHARGE,WORD,COMMAND}, {REFRESH,WORD,COMMAND}, {REFRESH,WORD,COMMAND},
    {AUTO,WORD,0}, {SELF,WORD,COMMAND}, {MUX,WORD,OLD_MUX&~MUX_MASK},
    {CTRL,WORD,NEW_CTRL}, {COEF,WORD,NEW_COEF}, {MUX,WORD,OLD_MUX},
    {SELF,WORD,0}, {AUTO,WORD,AUTO_BIT},
    {SR_INDEX,BYTE,SR1}, {SR_DATA,BYTE,SR_OLD},
    {SR_INDEX+STRIDE,BYTE,SR1}, {SR_DATA+STRIDE,BYTE,SR_OLD}
};
#define GOLDEN_WRITES (sizeof(golden)/sizeof(golden[0]))
#define RESTORE_WRITES (HEADS * 2U) /* index + data per saved SR1 */
#define SCRIPT_WRITES (GOLDEN_WRITES - RESTORE_WRITES)
/* ramnv40.c:111-113,171-173: each SR1 operation is index then data. */
#define SR_WRITES_PER_HEAD (RESTORE_WRITES / HEADS)
static u32 reg(u32 off)
{
    u32 v=0;
    for (u32 i=0;i<h.display.base.nregs;++i)
        if (regs[i].off==off) v=regs[i].value;
    return v;
}
static void observe_mmio(struct gf4_host *d,const struct gf4_host_event *e)
{
    /* Consumption precedes even the first probe read, including failure. */
    assert(h.ctx.plan_guard.ram_used);
    if (e->op==D_WRITE && !e->rc && e->a[1]==SELF) self_depth=e->a[3];
    /* While self-refresh is enabled, only MMIO/time callbacks occur;
     * all CPU/GPU admission remains closed. Neither test nor product maps
     * VRAM, calls COPY, resumes, or accesses a BAR1 aperture. */
    if (self_depth) {
        for (u32 head=0;head<HEADS;++head)
            if (protected_heads&(1U<<head)) assert(d->sr[head][SR1]&BLANK);
        assert(!h.ctx.accepting && d->base.paused && !d->ctx.accepting);
        if (e->op==D_READ || e->op==D_WRITE) assert(e->a[0]==0);
        assert(!h.calls[F5_COPY] && !h.calls[F5_RESUME]);
    }
    if (timeout_jump && e->op==D_SLEEP) h.now+=VBLANK_US;
    /* One failed read per edge, each followed by 1.5s of elapsed time. */
    if (slow_edges && e->op==D_SLEEP && d->next_read>=5U &&
        (d->next_read&1U)) h.now+=VBLANK_US*3U/4U;
}
static void observe_link(struct gf5_host *host,const struct gf5_host_event *e)
{
    if (e->op==F5_SCRIPT) {
        assert(!self_depth && !host->ctx.accepting && host->display.base.paused);
        assert(e->a[0]==M_PC && e->a[1]==DEADLINE);
        assert(e->p[1]==&program && host->ctx.rom.bytes==rom);
        assert(reg(AUTO)==AUTO_BIT);
    }
    if (e->op==F5_RESUME) assert(!self_depth);
}
static void add_read(u32 off,u32 value,u32 *n)
{
    reads[(*n)++]=(struct gf4_host_read){0,off,4,value};
}
static void rom_word(u32 at,u32 value)
{
    rom[at]=(u8)value; rom[at+1]=(u8)(value>>8);
}
static void plan_fixture(void)
{
    rom[GF_INIT_PLL_TABLE]=0x21; rom[GF_INIT_PLL_TABLE+1]=4;
    rom[GF_INIT_PLL_TABLE+2]=ROM_ENTRY; rom[GF_INIT_PLL_TABLE+3]=4;
    for (u32 i=0;i<4;++i) {
        u32 at=GF_INIT_PLL_TABLE+4U+i*ROM_ENTRY;
        rom_word(at+GF_PLL_ENT_VCO1_MIN,200);
        rom_word(at+GF_PLL_ENT_VCO1_MAX,500);
        rom_word(at+GF_PLL_ENT_VCO1_INPUT_MIN,10);
        rom_word(at+GF_PLL_ENT_VCO1_INPUT_MAX,20);
        rom[at+GF_PLL_ENT_VCO1_N_MIN]=rom[at+GF_PLL_ENT_VCO1_N_MAX]=25;
        rom[at+GF_PLL_ENT_VCO1_M_MIN]=rom[at+GF_PLL_ENT_VCO1_M_MAX]=2;
        rom[at+GF_PLL_ENT_MAX_P]=3; rom[at+GF_PLL_ENT_BIAS_P]=1;
    }
    struct gf5_plan calculated;
    assert(gf5_clock_calc(&h.ctx,&request,&calculated)==0);
    assert(calculated.mc==NEW_CTRL && calculated.mn==NEW_COEF);
    assert(calculated.nc==plan.nc && calculated.nn==plan.nn &&
           calculated.sc==plan.sc && calculated.mux==plan.mux);
    assert(calculated.actual.core_khz==REQUEST_KHZ &&
           calculated.actual.shader_khz==REQUEST_KHZ &&
           calculated.actual.mem_khz==REQUEST_KHZ);
}
/* active bits select moving heads; preblank selects already blank SR1s. */
static void setup(u32 active,u32 preblank,bool script)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf5_host_init(&h,&state);
    h.events=events; h.cevents=sizeof(events)/sizeof(events[0]);
    h.display.events=display_events; h.display.cevents=CAP;
    h.display.base.regs=regs; h.display.base.cregs=sizeof(regs)/sizeof(regs[0]);
    h.ctx.deadline=DEADLINE;
    h.ctx.rom=(struct gf5_rom_view){rom,sizeof(rom),script?&program:0,script?M_PC:0};
    plan_fixture();
    h.display.sr[0][1]=SR_OLD|((preblank&1U)?BLANK:0);
    h.display.sr[1][1]=SR_OLD|((preblank&2U)?BLANK:0);
    gf3_host_set(&h.display.base,0,CTRL,OLD_CTRL);
    gf3_host_set(&h.display.base,0,COEF,OLD_COEF);
    gf3_host_set(&h.display.base,0,MUX,OLD_MUX);
    gf3_host_set(&h.display.base,0,AUTO,AUTO_BIT);
    u32 n=0;
    for (u32 head=0;head<HEADS;++head) {
        u32 off=VBL+head*STRIDE;
        add_read(off,0,&n);
        if (active&(1U<<head)) add_read(off,1,&n);
        else for (u32 probe=0;probe<PROBES;++probe) add_read(off,0,&n);
    }
    for (u32 head=0;head<HEADS;++head) if ((active&~preblank)&(1U<<head)) {
        add_read(VBL+head*STRIDE,0,&n);
        add_read(VBL+head*STRIDE,HIGH,&n);
    }
    for (u32 head=0;head<HEADS;++head) if ((active&~preblank)&(1U<<head))
        add_read(VBL+head*STRIDE,HIGH,&n);
    gf4_host_reads(&h.display,reads,n);
    h.display.observe=observe_mmio; h.observe=observe_link;
    self_depth=0; protected_heads=active&~preblank; timeout_jump=false; slow_edges=false;
}
static void retained(void)
{
    assert(h.ctx.phase==GF5_FAULT && !h.ctx.accepting);
    assert(!h.calls[F5_RESUME] && !h.calls[F5_FREE] && !h.calls[F5_ALLOC]);
    assert(h.display.base.paused && !h.display.ctx.accepting);
    assert(h.ctx.rom.bytes==rom && h.ctx.rom.memory_program==&program);
    assert(h.ctx.rom.size==sizeof(rom) && h.ctx.deadline==DEADLINE);
}
/* No failure may add PLL/mux writes beyond the forward sequence. */
static void no_pll_rollback(void)
{
    u32 ctrl=0,coef=0,mux=0;
    for (u32 i=0;i<h.display.nevents;++i) {
        const struct gf4_host_event *e=&display_events[i];
        if (e->op!=D_WRITE) continue;
        if (e->a[1]==CTRL) { ++ctrl; assert(e->a[3]==NEW_CTRL); }
        if (e->a[1]==COEF) { ++coef; assert(e->a[3]==NEW_COEF); }
        if (e->a[1]==MUX) ++mux;
    }
    assert(ctrl<=1 && coef<=1 && mux<=2);
}
/* BIT M and late SR1 failures retain exactly the forward write sequence.
 * A failed SR1 index omits its data write; there is no second PLL pass. */
static void recovery_trace(u32 failed_write)
{
    u32 n=0,slept=0;
    for (u32 i=0;i<h.display.nevents;++i) {
        const struct gf4_host_event *e=&display_events[i];
        if (e->op==D_SLEEP) slept+=e->a[0];
        if (e->op!=D_WRITE) continue;
        assert(n<GOLDEN_WRITES);
        const struct golden *want=&golden[n];
        assert(e->a[0]==0 && e->a[1]==want->off &&
               e->a[2]==want->width && e->a[3]==want->value);
        ++n;
        if (n==failed_write && (want->off==SR_INDEX || want->off==SR_INDEX+STRIDE)) ++n;
    }
    assert(n==GOLDEN_WRITES && slept==2U*SETTLE_US);
    no_pll_rollback();
    assert(reg(CTRL)==NEW_CTRL && reg(COEF)==NEW_COEF && reg(MUX)==OLD_MUX);
    assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
    assert(h.calls[F5_SCRIPT]==1 && !h.calls[F5_ADMIT]);
}
static void normal(void)
{
    setup(3,0,true);
    assert(gf5_ram_prog(&h.ctx,&plan)==0);
    assert(h.display.next_read==h.display.nreads);
    u32 n=0,settles=0;
    for (u32 i=0;i<h.display.nevents;++i) {
        const struct gf4_host_event *e=&display_events[i];
        if (e->op==D_WRITE) {
            assert(n<sizeof(golden)/sizeof(golden[0]));
            assert(e->a[0]==0 && e->a[1]==golden[n].off);
            assert(e->a[2]==golden[n].width && e->a[3]==golden[n].value);
            ++n;
        }
        if (e->op==D_SLEEP) { assert(e->a[0]==GF2_WAIT_MAX_US); ++settles; }
    }
    assert(n==sizeof(golden)/sizeof(golden[0]) && settles==2U*(SETTLE_US/GF2_WAIT_MAX_US));
    assert(h.calls[F5_PAUSE]==1 && h.calls[F5_RESUME]==1 && h.calls[F5_SCRIPT]==1);
    assert(h.calls[F5_ADMIT]==1 && h.ctx.accepting && !h.display.base.paused);
    assert(h.ctx.source_rc==0 && h.ctx.last_rc==0 && h.ctx.phase==GF5_RUN);
    write_count=h.display.calls[D_WRITE]; read_count=h.display.calls[D_READ];
    sleep_count=h.display.calls[D_SLEEP]; clock_count=h.display.calls[D_CLOCK];
    assert(h.display.sr[0][1]==SR_OLD && h.display.sr[1][1]==SR_OLD);
}
/* ramnv40.c:99-109,165-169: six independent waits may total >2s,
 * and neither prior work nor an expired outer deadline reduces a budget. */
static void independent_waits(void)
{
    setup(3,0,true);
    u32 n=HEADS*2U;
    for (u32 head=0;head<HEADS;++head) {
        add_read(VBL+head*STRIDE,HIGH,&n);
        add_read(VBL+head*STRIDE,0,&n);
        add_read(VBL+head*STRIDE,0,&n);
        add_read(VBL+head*STRIDE,HIGH,&n);
    }
    for (u32 head=0;head<HEADS;++head) {
        add_read(VBL+head*STRIDE,0,&n);
        add_read(VBL+head*STRIDE,HIGH,&n);
    }
    gf4_host_reads(&h.display,reads,n);
    h.now=DEADLINE-100U;
    slow_edges=true;
    assert(gf5_ram_prog(&h.ctx,&plan)==0);
    assert(h.display.next_read==n && h.now>DEADLINE+4U*VBLANK_US);
    assert(h.ctx.deadline==DEADLINE && h.ctx.accepting);
    assert(h.calls[F5_SCRIPT]==1 && h.calls[F5_RESUME]==1);
    assert(reg(CTRL)==NEW_CTRL && reg(COEF)==NEW_COEF);
    assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
    assert(h.display.sr[0][SR1]==SR_OLD && h.display.sr[1][SR1]==SR_OLD);
}
static void rejected(const struct gf5_plan *p,i32 rc)
{
    struct gf5_host_activity before=gf5_host_activity(&h);
    bool used=h.ctx.plan_guard.ram_used;
    bool core_used=h.ctx.plan_guard.core_used;
    assert(gf5_ram_prog(&h.ctx,p)==rc);
    struct gf5_host_activity after=gf5_host_activity(&h);
    assert(after.reads==before.reads && after.writes==before.writes &&
           after.pauses==before.pauses && after.resumes==before.resumes);
    assert(h.ctx.plan_guard.ram_used==used && h.ctx.plan_guard.core_used==core_used);
    assert(h.ctx.last_rc==rc && h.ctx.source_rc==rc);
    assert(!h.calls[F5_SCRIPT] && !h.calls[F5_ADMIT]);
}
static void guard_cases(void)
{
    setup(3,0,true); h.ctx.plan_guard.valid=false;
    rejected(&plan,GF5_ESTATE);
    setup(3,0,true); ++h.display.base.ch.epoch;
    rejected(&plan,GF5_ESTATE);
    setup(3,0,true); ++h.ctx.rom_generation;
    rejected(&plan,GF5_ESTATE);
    setup(3,0,true);
    struct gf5_plan changed=plan; changed.mc^=1U;
    rejected(&changed,GF5_EINVAL);
    changed=plan; changed.mn^=1U;
    rejected(&changed,GF5_EINVAL);
    /* Rejection must leave the original pair usable. */
    assert(gf5_ram_prog(&h.ctx,&plan)==0);
    assert(h.ctx.plan_guard.ram_used && !h.ctx.plan_guard.core_used);
    gf5_host_trace_clear(&h);
    rejected(&plan,GF5_ESTATE);
    assert(gf5_clock_prog(&h.ctx,&plan)==0);
    assert(h.ctx.plan_guard.ram_used && h.ctx.plan_guard.core_used);
    /* A failed first probe consumes RAM too; RUN alone cannot retry it. */
    setup(3,0,true);
    gf4_host_fail_at(&h.display,D_READ,1,-78);
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
    assert(h.ctx.plan_guard.ram_used && !h.ctx.plan_guard.core_used);
    gf4_host_fail_at(&h.display,D_READ,0,0);
    h.ctx.phase=GF5_RUN;
    gf4_host_reads(&h.display,reads,h.display.nreads);
    gf5_host_trace_clear(&h);
    rejected(&plan,GF5_ESTATE);
    assert(gf5_clock_calc(&h.ctx,&request,&changed)==0);
    assert(!h.ctx.plan_guard.ram_used && !h.ctx.plan_guard.core_used);
    assert(gf5_ram_prog(&h.ctx,&changed)==0);
}
void _start(void)
{
    guard_cases();
    normal();
    independent_waits();
    /* Every successful-path I/O site can fail, including applied writes.
     * The first source rc survives every cleanup; no script is retried. */
    for (u32 nth=1;nth<=write_count;++nth) {
        setup(3,0,true);
        gf4_host_write_applied_error(&h.display,nth,-77);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        retained(); assert(h.ctx.source_rc==-77 && h.ctx.last_rc==GF5_EIO);
        assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
        assert(h.calls[F5_SCRIPT]<=1); no_pll_rollback();
    }
    for (u32 nth=1;nth<=read_count;++nth) {
        setup(3,0,true);
        gf4_host_fail_at(&h.display,D_READ,nth,-78);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        assert(h.ctx.source_rc==-78 && !h.calls[F5_RESUME]); no_pll_rollback();
        assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
    }
    for (u32 nth=1;nth<=sleep_count;++nth) {
        setup(3,0,true);
        gf4_host_fail_at(&h.display,D_SLEEP,nth,-79);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        retained(); assert(h.ctx.source_rc==-79);
        no_pll_rollback();
    }
    for (u32 nth=1;nth<=clock_count;++nth) {
        setup(3,0,true);
        gf4_host_fail_at(&h.display,D_CLOCK,nth,-80);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        retained(); assert(h.ctx.source_rc==-80); no_pll_rollback();
        assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
    }
    setup(3,0,true);
    gf5_host_fail_at(&h,F5_SCRIPT,1,GF_SCRIPT_ETIME);
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_ETIME);
    retained(); assert(h.calls[F5_SCRIPT]==1 && h.ctx.source_rc==GF_SCRIPT_ETIME);
    recovery_trace(0);
    /* Each saved SR1 index/data write can report an applied I/O failure.
     * These must not trigger a second PLL pass. */
    for (u32 nth=SCRIPT_WRITES+1U;nth<=GOLDEN_WRITES;++nth) {
        setup(3,0,true);
        gf4_host_write_applied_error(&h.display,nth,-77);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        retained(); assert(h.ctx.source_rc==-77 && h.ctx.last_rc==GF5_EIO);
        recovery_trace(nth);
    }
    setup(0,0,false);
    assert(gf5_ram_prog(&h.ctx,&plan)==0 && h.calls[F5_SCRIPT]==0);
    assert(h.display.calls[D_SLEEP]==HEADS*PROBES+2U*(SETTLE_US/GF2_WAIT_MAX_US));
    setup(3,2,false);
    assert(gf5_ram_prog(&h.ctx,&plan)==0);
    assert(h.display.sr[0][1]==SR_OLD && h.display.sr[1][1]==(SR_OLD|BLANK));
    setup(3,0,true);
    h.ctx.accepting=false; h.display.ctx.accepting=false;
    h.ctx.phase=GF5_RESTORING;
    assert(gf5_ram_prog(&h.ctx,&plan)==0 && !h.ctx.accepting && !h.calls[F5_ADMIT]);
    /* Pause/resume errors do not publish admission. */
    setup(3,0,true);
    gf5_host_fail_at(&h,F5_PAUSE,1,GF2_ETIME);
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_ETIME);
    retained(); assert(!h.display.calls[D_WRITE] && !h.calls[F5_SCRIPT]);
    setup(3,0,true);
    gf5_host_fail_at(&h,F5_RESUME,1,-83);
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
    assert(h.ctx.phase==GF5_FAULT && !h.ctx.accepting && !h.calls[F5_ADMIT]);
    setup(3,0,true);
    gf5_host_fail_at(&h,F5_ADMIT,1,-84);
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
    assert(h.ctx.phase==GF5_FAULT && !h.ctx.accepting && h.ctx.source_rc==-84);
    assert(!h.display.ctx.accepting && h.ctx.rom.memory_program==&program);
    /* Malformed/unavailable prepared M is rejected before pausing. */
    setup(3,0,true); h.ctx.rom.memory_program=0;
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EBIOS && !h.calls[F5_PAUSE]);
    setup(3,0,true); h.ctx.rom.bytes=0;
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_ENOTSUP && !h.calls[F5_PAUSE]);
    /* Vblank stall at head1 restores head0 before any refresh command. */
    setup(3,0,true);
    reads[5].value=HIGH; /* head0 wait-high remains high */
    reads[6].value=HIGH; /* head1 wait-low stalls */
    reads[7].value=HIGH; reads[8].value=HIGH; reads[9].value=HIGH;
    /* A single read followed by a 2s clock jump reaches the local timeout. */
    h.display.nreads=7; timeout_jump=true;
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_ETIME);
    retained(); assert(!h.calls[F5_SCRIPT] && reg(CTRL)==OLD_CTRL);
    assert(h.now==VBLANK_US+GF2_WAIT_MAX_US);
    assert(h.display.sr[0][1]==SR_OLD && h.display.sr[1][1]==SR_OLD);
    for (u32 i=0;i<h.display.nevents;++i)
        assert(display_events[i].op!=D_WRITE || display_events[i].a[1]!=PRECHARGE);
    assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
    setup(3,0,true);
    h.ctx.heat.down_active=true;
    h.ctx.heat.ceiling=(struct gf5_clock){1,1,1};
    /* A valid decoded plan over the ceiling cannot pause or touch hardware. */
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EBUSY);
    assert(!h.calls[F5_PAUSE] && !h.display.nevents && !h.ctx.plan_guard.ram_used);
    gf2_host_exit(0);
}
