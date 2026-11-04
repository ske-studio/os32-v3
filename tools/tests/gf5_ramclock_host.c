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
/* B2: freestanding UNIT2, link hwdev/gf/ramclock.c only.
 * Golden writes transcribed independently from MIT nouveau c03c1a1
 * nvkm/subdev/fb/ramnv40.c:78-175, default (NV4a) partition.
 * Recreate by following that function with both SR1=3, old mux=0xc123,
 * old ctrl=0x80000100, old coef=0x1401, mc=0x80210100, mn=0x1902.
 * The plan is an opaque B1 result; this test does not solve a PLL. */
#define GF5_HOST_UNIT 2
#include "gf5_fake.h"

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
static u8 rom[M_PC+1U];
static const struct gf5_plan plan = {.mc=NEW_CTRL,.mn=NEW_COEF};
static u32 write_count, read_count, sleep_count, clock_count, self_depth, protected_heads;
static bool timeout_jump;
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
/* Same ramnv40.c:115-152 sequence with the saved PLL, never BIT M.
 * GF_FEAT5_DESIGN section 3 requires one best-effort pass on failure. */
static const struct golden rollback_golden[] = {
    {PRECHARGE,WORD,COMMAND}, {REFRESH,WORD,COMMAND}, {REFRESH,WORD,COMMAND},
    {AUTO,WORD,0}, {SELF,WORD,COMMAND}, {MUX,WORD,OLD_MUX&~MUX_MASK},
    {CTRL,WORD,OLD_CTRL}, {COEF,WORD,OLD_COEF}, {MUX,WORD,OLD_MUX},
    {SELF,WORD,0}, {AUTO,WORD,AUTO_BIT}
};
#define GOLDEN_WRITES (sizeof(golden)/sizeof(golden[0]))
#define RESTORE_WRITES (HEADS * 2U) /* index + data per saved SR1 */
#define SCRIPT_WRITES (GOLDEN_WRITES - RESTORE_WRITES)
#define ROLLBACK_WRITES (sizeof(rollback_golden)/sizeof(rollback_golden[0]))
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
    self_depth=0; protected_heads=active&~preblank; timeout_jump=false;
}
static void retained(void)
{
    assert(h.ctx.phase==GF5_FAULT && !h.ctx.accepting);
    assert(!h.calls[F5_RESUME] && !h.calls[F5_FREE] && !h.calls[F5_ALLOC]);
    assert(h.display.base.paused && !h.display.ctx.accepting);
    assert(h.ctx.rom.bytes==rom && h.ctx.rom.memory_program==&program);
    assert(h.ctx.rom.size==sizeof(rom) && h.ctx.deadline==DEADLINE);
}
/* Compare every write, including the failed attempt. A late SR1 index
 * failure omits that data write; reblank and final restore surround rollback.
 * failed_write is the one-based forward write site, or zero for script error. */
static void recovery_trace(u32 failed_write)
{
    u32 n=0, slept=0, settle_left=0;
    struct golden expected[GOLDEN_WRITES+RESTORE_WRITES*2U+ROLLBACK_WRITES];
    u32 count=0, prefix=failed_write?GOLDEN_WRITES:SCRIPT_WRITES;
    for (u32 i=0;i<prefix;++i) {
        expected[count++]=golden[i];
        /* sr_write returns immediately when its index write fails. */
        if (i+1U==failed_write && golden[i].off==
            SR_INDEX+((i-SCRIPT_WRITES)/SR_WRITES_PER_HEAD)*STRIDE) ++i;
    }
    if (failed_write) {
        for (u32 head=0;head<HEADS;++head) {
            expected[count++]=(struct golden){SR_INDEX+head*STRIDE,BYTE,SR1};
            expected[count++]=(struct golden){SR_DATA+head*STRIDE,BYTE,SR_OLD|BLANK};
        }
    }
    for (u32 i=0;i<ROLLBACK_WRITES;++i) expected[count++]=rollback_golden[i];
    for (u32 i=SCRIPT_WRITES;i<GOLDEN_WRITES;++i) expected[count++]=golden[i];
    for (u32 i=0;i<h.display.nevents;++i) {
        const struct gf4_host_event *e=&display_events[i];
        if (e->op==D_SLEEP) {
            assert(e->a[0] && e->a[0]<=GF2_WAIT_MAX_US && e->a[0]<=settle_left);
            slept+=e->a[0]; settle_left-=e->a[0];
        }
        if (e->op!=D_WRITE) continue;
        assert(!settle_left);
        assert(n<count);
        const struct golden *want=&expected[n];
        assert(e->a[0]==0 && e->a[1]==want->off);
        assert(e->a[2]==want->width && e->a[3]==want->value);
        if (want->off==COEF || (want->off==AUTO && want->value==AUTO_BIT))
            settle_left=SETTLE_US;
        ++n;
    }
    assert(n==count && !settle_left);
    /* ramnv40.c:145,152: two settles for each of forward and rollback. */
    assert(slept==2U*2U*SETTLE_US);
    assert(reg(CTRL)==OLD_CTRL && reg(COEF)==OLD_COEF && reg(MUX)==OLD_MUX);
    assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
    assert(h.display.sr[0][SR1]==SR_OLD && h.display.sr[1][SR1]==SR_OLD);
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
void _start(void)
{
    normal();
    /* Every successful-path I/O site can fail, including applied writes.
     * The first source rc survives every cleanup; no script is retried. */
    for (u32 nth=1;nth<=write_count;++nth) {
        setup(3,0,true);
        gf4_host_write_applied_error(&h.display,nth,-77);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        retained(); assert(h.ctx.source_rc==-77 && h.ctx.last_rc==GF5_EIO);
        assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
        assert(h.calls[F5_SCRIPT]<=1);
    }
    for (u32 nth=1;nth<=read_count;++nth) {
        setup(3,0,true);
        gf4_host_fail_at(&h.display,D_READ,nth,-78);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        assert(h.ctx.source_rc==-78 && !h.calls[F5_RESUME]);
        assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
    }
    for (u32 nth=1;nth<=sleep_count;++nth) {
        setup(3,0,true);
        gf4_host_fail_at(&h.display,D_SLEEP,nth,-79);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        retained(); assert(h.ctx.source_rc==-79);
        assert(reg(CTRL)==OLD_CTRL && reg(COEF)==OLD_COEF && reg(MUX)==OLD_MUX);
    }
    for (u32 nth=1;nth<=clock_count;++nth) {
        setup(3,0,true);
        gf4_host_fail_at(&h.display,D_CLOCK,nth,-80);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        retained(); assert(h.ctx.source_rc==-80);
        assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
    }
    setup(3,0,true);
    gf5_host_fail_at(&h,F5_SCRIPT,1,GF_SCRIPT_ETIME);
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_ETIME);
    retained(); assert(h.calls[F5_SCRIPT]==1 && h.ctx.source_rc==GF_SCRIPT_ETIME);
    recovery_trace(0);
    /* Each saved SR1 index/data write can report an applied I/O failure.
     * These run after the initial rollback decision; still restore PLL once. */
    for (u32 nth=SCRIPT_WRITES+1U;nth<=GOLDEN_WRITES;++nth) {
        setup(3,0,true);
        gf4_host_write_applied_error(&h.display,nth,-77);
        assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
        retained(); assert(h.ctx.source_rc==-77 && h.ctx.last_rc==GF5_EIO);
        recovery_trace(nth);
    }
    /* Rollback failure cannot suppress controller/SR1 cleanup or mask cause. */
    setup(3,0,true);
    gf5_host_fail_at(&h,F5_SCRIPT,1,-81);
    gf4_host_fail_at(&h.display,D_WRITE,SCRIPT_WRITES+1U,-82); /* rollback precharge */
    assert(gf5_ram_prog(&h.ctx,&plan)==GF5_EIO);
    retained(); assert(h.ctx.source_rc==-81 && !reg(SELF) && reg(AUTO)==AUTO_BIT);
    recovery_trace(0);
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
    assert(h.display.sr[0][1]==SR_OLD && h.display.sr[1][1]==SR_OLD);
    for (u32 i=0;i<h.display.nevents;++i)
        assert(display_events[i].op!=D_WRITE || display_events[i].a[1]!=PRECHARGE);
    assert(!reg(SELF) && reg(AUTO)==AUTO_BIT);
    gf2_host_exit(0);
}
