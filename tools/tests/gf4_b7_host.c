/* B7 fixture: GF_FEAT4_DESIGN section 7 and 2026-11-04 B7 decision.
 * Literal addresses and bit are the nouveau nvreg.h:299 and hw.h:353-357 oracle. */
#define GF4_HOST_UNIT 7
#include "gf4_display_fake.h"
#define START 0x600800U
#define STATUS 0x6013daU
#define DEADLINE 500U
#define OLD_OFF 0x10000U
#define NEW_OFF 0x20000U
#define RETRACE_HIGH 8U /* nvreg.h:299, vertical retrace status bit. */
#define OTHER_OFF 0x30000U
#define CAP 256U /* Bound for the finite deadline and fixture event trace. */
static struct gf4_host h;
static struct gf4_state state;
static struct gf4_host_event events[CAP];
static struct gf3_host_reg regs[16];
static struct gf3_surface next = {.off=NEW_OFF};
static const struct gf4_host_read retrace_high[] = {
    {0,STATUS,1,RETRACE_HIGH}
};
static unsigned advance;
static void observe(struct gf4_host *host, const struct gf4_host_event *e)
{
    if (e->op == D_SLEEP && advance) ++host->irq.vblank[0];
}
static void reset(void)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf4_host_init(&h,&state);
    h.events=events; h.cevents=CAP; h.base.regs=regs; h.base.cregs=16;
    h.base.f.running=true; h.base.f.wanted_heads=1; h.base.ch.heads=1;
    state.head[0].enabled=true; state.head[0].dpms=GF4_ON;
    state.head[0].front.off=OLD_OFF;
    gf3_host_set(&h.base,0,START,OLD_OFF);
    h.observe=observe; advance=0;
}
void _start(void)
{
    u32 seen=99;
    reset();
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==0);
    assert(h.ctx.hold[0].pending && h.ctx.hold[0].base_verified);
    assert(state.head[0].front.off==OLD_OFF && h.calls[D_FENCE]==1 && h.calls[D_FENCE_WAIT]==1);
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==GF4_EBUSY);
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==GF4_ETIME);
    assert(h.ctx.hold[0].pending && state.head[0].front.off==OLD_OFF);
    ++h.irq.vblank[0];
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE+500)==0);
    assert(!h.ctx.hold[0].pending && state.head[0].front.off==NEW_OFF);
    assert(!h.ctx.hold[0].old_front.off && !h.ctx.hold[0].new_front.off &&
           !h.ctx.hold[0].base_written && !h.ctx.hold[0].base_verified);
    reset();
    gf4_host_write_applied_error(&h,1,-77);
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==-77);
    assert(h.ctx.hold[0].pending && !h.ctx.hold[0].base_verified);
    advance=1;
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==0);
    assert(state.head[0].front.off==NEW_OFF && !h.ctx.hold[0].pending);
    reset();
    gf4_host_fail_at(&h,D_WRITE,1,-78);
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==-78);
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==GF4_ESTATE);
    assert(!h.ctx.hold[0].pending && state.head[0].front.off==OLD_OFF);
    reset();
    gf4_host_fail_at(&h,D_WRITE,1,-78);
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==-78);
    gf3_host_set(&h.base,0,START,OTHER_OFF);
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==GF4_ESTATE && h.ctx.hold[0].pending);
    reset();
    gf4_host_fail_at(&h,D_WRITE,1,-78);
    assert(gf4_flip(&h.ctx,0,&state.head[0].front,false,DEADLINE)==-78);
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==0 && !h.ctx.hold[0].pending);
    reset();
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==0);
    ++h.irq.vblank[0];
    gf4_host_fail_at(&h,D_SCAN_CHANGED,1,-79);
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==-79 && h.ctx.hold[0].pending);
    gf4_host_fail_at(&h,D_SCAN_CHANGED,0,0);
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==0);
    reset();
    h.irq.vblank[0]=0; assert(gf4_vblank_wait(&h.ctx,0,U32_MAX,DEADLINE,&seen)==0 && seen==0);
    reset();
    h.ctx.epoch++; assert(gf4_vblank_wait(&h.ctx,0,0,DEADLINE,&seen)==GF4_ESTATE);
    reset();
    advance=1; gf4_host_reads(&h,retrace_high,sizeof(retrace_high)/sizeof(retrace_high[0]));
    assert(gf4_flip(&h.ctx,0,&next,true,DEADLINE)==0);
    assert(!h.ctx.hold[0].pending && state.head[0].front.off==NEW_OFF);
    reset();
    {
        static const struct gf4_host_read retrace[] = {
            {0,STATUS,1,0}, {0,STATUS,1,RETRACE_HIGH}
        };
        gf4_host_reads(&h,retrace,2); advance=1;
        assert(gf4_flip(&h.ctx,0,&next,true,DEADLINE)==0);
        assert(h.next_read==2 && h.base.waits>=3);
    }
    /* No write or ownership change after a rejected validation or drain. */
    reset();
    gf4_host_fail_at(&h,D_SURFACE,1,GF4_EFORMAT);
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==GF4_EFORMAT);
    assert(!h.calls[D_FENCE] && !h.calls[D_WRITE] && !h.ctx.hold[0].pending);
    reset();
    gf4_host_fail_at(&h,D_FENCE_WAIT,1,GF2_ETIME);
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==GF2_ETIME);
    assert(!h.calls[D_WRITE] && !h.ctx.hold[0].pending && state.head[0].front.off==OLD_OFF);
    reset();
    gf4_host_fail_at(&h,D_READ,1,-80);
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==-80);
    assert(h.ctx.hold[0].pending && !h.ctx.hold[0].base_verified);
    gf4_host_fail_at(&h,D_READ,0,0);
    advance=1;
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==0);
    assert(state.head[0].front.off==NEW_OFF && !h.ctx.hold[0].pending);
    /* A failed ownership notification keeps both surfaces for retry. */
    reset();
    gf4_host_fail_at(&h,D_WRITE,1,-78);
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==-78);
    gf4_host_fail_at(&h,D_SCAN_CHANGED,1,-79);
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==-79);
    assert(h.ctx.hold[0].pending && h.ctx.hold[0].old_front.off==OLD_OFF &&
           h.ctx.hold[0].new_front.off==NEW_OFF);
    gf4_host_fail_at(&h,D_SCAN_CHANGED,0,0);
    assert(gf4_flip_complete(&h.ctx,0,DEADLINE)==GF4_ESTATE);
    assert(!h.ctx.hold[0].pending && state.head[0].front.off==OLD_OFF);
    /* Public waiting completes a pending flip first, then honors its after. */
    reset();
    assert(gf4_flip(&h.ctx,0,&next,false,DEADLINE)==0);
    advance=1; seen=99;
    assert(gf4_vblank_wait(&h.ctx,0,1,DEADLINE,&seen)==0);
    assert(seen>1 && !h.ctx.hold[0].pending && state.head[0].front.off==NEW_OFF);
    reset();
    h.base.ch.heads=0; seen=99;
    assert(gf4_vblank_wait(&h.ctx,0,0,DEADLINE,&seen)==GF4_ESTATE && seen==99);
    reset();
    assert(gf4_vblank_wait(&h.ctx,0,0,DEADLINE,&seen)==GF4_ETIME && seen==99);
    reset();
    advance=1; gf4_host_reads(&h,retrace_high,sizeof(retrace_high)/sizeof(retrace_high[0]));
    assert(gf4_flip(&h.ctx,0,&next,true,DEADLINE)==0);
    for (u32 i=0;i<h.nevents;++i) {
        if (events[i].op==D_READ && events[i].a[1]==STATUS) {
            assert(i+1<h.nevents && events[i+1].op==D_WRITE);
            assert(events[i+1].a[1]==START && events[i+1].a[3]==NEW_OFF);
            break;
        }
    }
    gf2_host_exit(0);
}
