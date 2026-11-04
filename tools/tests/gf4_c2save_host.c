/* C2-save caller contracts, UNIT 14 (no fixture dependency symbols).
 * MIT nouveau c03c1a1 dispnv04/crtc.c:671-706, hw.c:827-835;
 * DPMS expectations below come independently from crtc.c:198-233.
 * Link the real F4 implementations, -nostdlib, entry _start.
 * Only lower F1/F2/F3 boundaries are injected here. C0 mode_calc/load
 * remain external until PM integrates modeset.c/transaction.c; never fake
 * those entry points. See report-fix1.md for the product link set. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#include "../../hwdev/gf/internal.h"

static struct gf4_host host;
static struct gf4_state state;
static struct gf_display_context legacy;
static struct gf4_saved saved, untouched;
/* Storage capacities, not expected I/O counts: allow full VGA/LUT loads. */
#define T_TRACE_CAP (GF4_HEADS * (GF4_HOST_LUT_BYTES + 16U * GF4_HOST_VGA_REGS))
static struct gf4_host_event events[T_TRACE_CAP], golden[T_TRACE_CAP];
static struct gf3_host_reg regs[GF4_HEADS * GF4_HOST_VGA_REGS];
static struct gf4_route routes[GF4_HEADS];
static struct gf_vram_reservation_node nodes[GF4_HEADS][3];
static struct gf4_head_image before[GF4_HEADS];
static struct gfinit_mode committed[GF4_HEADS];
static struct gf4_tv_state tv_before[GF4_HEADS];
static u32 golden_count;
/* Test storage patterns are distinct sentinels, never hardware values. */
#define T_SENTINEL 0xa5U
#define T_ERROR GF2_EIO
#define T_BUDGET GF3_TIMEOUT_US
#define T_SURFACE_PITCH GF_MODE_PITCH
#define T_SURFACE_WIDTH GF_MODE_WIDTH
#define T_SURFACE_HEIGHT GF_MODE_HEIGHT
#define T_SURFACE_BYTES ((T_SURFACE_PITCH * T_SURFACE_HEIGHT + MEM_PAGE_SIZE-1U) & ~(MEM_PAGE_SIZE-1U))
#define T_FIRST_OFF (GF_MODE_VRAM_BYTES + MEM_PAGE_SIZE)
/* Independent hardware oracle: crtc.c:198-233 and hw.h:238-240. */
#define T_CR_OWNER 0x44U
#define T_OWNER_B 3U
#define T_RPC 0x1aU
#define T_CR_MODE 0x17U
#define T_SR_CLOCK 1U
#define T_SCREEN_OFF 0x20U
#define T_SYNC_H 0x80U
#define T_SYNC_V 0x40U
#define T_CRTC_ON 0x80U
#define T_SETTLE_US 10000U
/* cursor.c:39-62, hw.h:341-349, nvreg.h:257-264,301-303. */
#define T_CURSOR_POS 0x680300U
#define T_CURSOR_HIGH 0x30U
#define T_CURSOR_LOW 0x31U
#define T_CURSOR_TOP 0x2fU
#define T_CURSOR_ENABLE_ADDRESS 0x80U
/* crtc.c:510-516, nvreg.h:159-165,257-264; cursor.c:46-51; hw.h:372-385. */
#define T_CURSOR_CFG 0x600810U
#define T_CURSOR_NORMAL ((4U<<24)|(1U<<16)|(1U<<12)|(1U<<8))
#define T_CURSOR_DBLSCAN 0x10U
#define T_CURSOR_DOUBLE_ADDRESS 2U
#define T_CURSOR_VISIBLE 1U
/* dac.c:364-444, nvreg.h:329-330,348. */
#define T_DACCLK 0x68052cU
#define T_DAC_TEST 0x680608U
#define T_DAC_TEST_VALUE 0x00100000U
#define T_DAC_ENABLE 1U
#define T_DAC_HEAD_B 0x100U
/* crtc.c:91-102,153-162; nvreg.h:311-323. */
#define T_PLLSEL 0x68050cU
/* Distinct saved shared-clock bits, including both heads' gate fields. */
#define T_PLL_GATE_SAVED 0xa50f0000U
/* crtc.c:838-848; nvreg.h:153. */
#define T_START 0x600800U
#define T_PLLSEL_BOTH ((1U<<28)|(1U<<9)|(1U<<29)|(1U<<11)|(1U<<2))
/* dfp.c:204-244: shared clock binding bits plus an unrelated sentinel. */
#define T_BIND_A (1U<<16)
#define T_BIND_B (1U<<18)
#define T_CLOCK_OTHER 1U
/* hw.c:795-810 / nvreg.h:433-437: indexed palette gate. */
#define T_LUT_GATE 0x20U

static struct gf4_host_event *begin(enum gf4_host_op op)
{ return gf4_host_begin(&host,op); }
static i32 end(struct gf4_host_event *e)
{ return gf4_host_end(&host,e); }
int gf2_fence_emit(struct gf2_chan *ch,struct gf2_fence *f,u32 dl)
{
    struct gf4_host_event *e=begin(D_FENCE);
    e->p[0]=ch; e->p[1]=f; e->a[0]=dl;
    assert(ch==host.ctx.ch && !host.ctx.accepting && dl==T_BUDGET && host.ctx.deadline==dl);
    if (!e->rc) *f=(struct gf2_fence){ch->epoch,++ch->seq};
    return end(e);
}
int gf2_fence_wait(struct gf2_chan *ch,struct gf2_fence f,u32 dl)
{
    struct gf4_host_event *e=begin(D_FENCE_WAIT);
    e->p[0]=ch; e->a[0]=f.epoch; e->a[1]=f.seq; e->a[2]=dl;
    assert(ch==host.ctx.ch && f.epoch==ch->epoch && f.seq==ch->seq);
    return end(e);
}
i32 gf_vram_views_close(struct gf2_chan *ch)
{
    struct gf4_host_event *e=begin(D_UNMAP); e->p[0]=ch;
    assert(host.calls[D_FENCE_WAIT] && !host.ctx.accepting);
    if (!e->rc) for (u8 h=0;h<GF4_HEADS;++h)
        host.ctx.cursor_view[h]=(struct gf_vram_view){0};
    return end(e);
}
int gf_vram_match(struct gf2_chan *ch,const void *owner,u32 id,u32 gen,
                  u32 off,u32 bytes,struct gf_vram_reservation *out)
{ return gf4_host_match(&host,ch,owner,id,gen,off,bytes,out); }
int gf_irq_enable(struct gf_foundation *f,bool fifo,bool graph,unsigned heads)
{
    struct gf4_host_event *e=begin(D_IRQ);
    e->p[0]=f; e->a[0]=fifo; e->a[1]=graph; e->a[2]=heads;
    assert(f==host.ctx.ch->f && !host.ctx.accepting);
    f->wanted_fifo=fifo ? U32_MAX : 0;
    f->wanted_graph=graph ? U32_MAX : 0; f->wanted_heads=heads;
    return end(e);
}
/* F3 validation is below F4's real surface_check, with live reservation keys. */
int gf3_surface_check(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,
                      enum gf3_surface_use use,struct gf3_format *fmt,struct gf3_span *span)
{
    struct gf4_host_event *e=begin(D_SURFACE);
    e->p[0]=c; e->p[1]=s;
    if (!e->rc) e->rc=gf3_host_surface(&host.base,c,s,r,use,fmt,span);
    return end(e);
}
static void setup(void)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf2_host_memset(&legacy,0,sizeof(legacy));
    gf2_host_memset(nodes,0,sizeof(nodes));
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=sizeof(events)/sizeof(events[0]);
    host.base.regs=regs; host.base.cregs=sizeof(regs)/sizeof(regs[0]);
    host.ctx.legacy=&legacy; legacy.display4=&host.ctx;
    for (u32 b=0;b<sizeof(u32);++b)
        gf4_host_reg_byte_set(&host,GF_MODE_REGISTER_BAR,GF_PLL_GATE+b,
                             T_PLL_GATE_SAVED>>(b*BITS_PER_BYTE));
    host.ctx.routes=routes; host.ctx.nroutes=GF4_HEADS;
    state.version=GF4_STATE_VERSION; state.size=sizeof(state);
    host.base.f.wanted_fifo=host.base.f.wanted_graph=U32_MAX;
    host.base.f.wanted_heads=host.base.ch.heads=(1U<<GF4_HEADS)-1U;
    u32 off=(T_FIRST_OFF+MEM_PAGE_SIZE-1U)&~(MEM_PAGE_SIZE-1U),id=0;
    for (u8 h=0;h<GF4_HEADS;++h) {
        struct gf4_head_image *v=&state.head[h];
        v->enabled=v->cursor_valid=v->cursor_visible=true;
        v->route=(struct gf4_route){.connector=h,.head=h,.type=GF4_ANALOG,.head_mask=1U<<h,.or_mask=1U<<h};
        routes[h]=v->route;
        v->mode.id=h+1U; v->mode.fmt=GF3_INDEX8; v->mode.pitch=T_SURFACE_PITCH;
        v->mode.t=(struct gf4_timing){GF_MODE_CLOCK_KHZ,
            GF_MODE_WIDTH,GF_MODE_HSYNC_START,GF_MODE_HSYNC_END,GF_MODE_HTOTAL,
            GF_MODE_HEIGHT,GF_MODE_VSYNC_START,GF_MODE_VSYNC_END,GF_MODE_VTOTAL,
            GF4_NEG_H|GF4_NEG_V};
        v->dpms=h ? GF4_SUSPEND : GF4_ON;
        v->blank=h!=0;
        v->x=-7; v->y=11; v->hot_x=2; v->hot_y=3;
        for (u32 i=0;i<sizeof(v->palette);++i) v->palette[i]=(u8)(i+h);
        for (u32 c=0;c<GF4_HOST_RGB;++c) for (u32 i=0;i<GF4_HOST_VGA_REGS;++i)
            v->gamma[c][i]=(u16)(i*(U16_MAX/U8_MAX)+c);
        for (u32 n=0;n<3;++n) {
            struct gf_vram_reservation_node *node=&nodes[h][n];
            node->value=(struct gf_vram_reservation){node,off,
                n==2 ? GF4_CURSOR_BYTES : T_SURFACE_BYTES,++id,1};
            node->next=host.base.reservations; host.base.reservations=node;
            off+=node->value.bytes;
        }
        v->front=(struct gf3_surface){.off=nodes[h][0].value.off,.bytes=T_SURFACE_BYTES,
            .pitch=T_SURFACE_PITCH,.width=T_SURFACE_WIDTH,.height=T_SURFACE_HEIGHT,
            .fmt=GF3_INDEX8,.reservation_id=nodes[h][0].value.reservation_id,.generation=1};
        v->back=v->front; v->back.off=nodes[h][1].value.off;
        v->back.reservation_id=nodes[h][1].value.reservation_id;
        v->cursor=nodes[h][2].value;
        /* Known VGA solver fixture: tools/tests/gfinit_mode_host.c:61-64. */
        host.ctx.limits[h]=(struct gfinit_pll_limits){
            {100000,400000,1000,27000,1,255,1,27},
            {400000,1000000,25000,200000,4,40,1,4},6,0,27000};
        host.ctx.limits_valid[h]=true;
        state.committed[h].start=T_SENTINEL+h;
        state.tv[h].norm=h;
        before[h]=*v; committed[h]=state.committed[h]; tv_before[h]=state.tv[h];
        host.ctx.cursor_view[h].mapped=true;
    }
    host.base.ch.B=off;
    host.ctx.hold[0]=(struct gf4_flip_hold){.pending=true,.base_written=true,
        .old_front=state.head[0].front,.new_front=state.head[0].back,.after=U32_MAX};
    gf2_host_memset(&saved,T_SENTINEL,sizeof(saved)); untouched=saved;
}
static void retained(void)
{
    assert(!host.ctx.accepting && !host.ctx.changing);
    for (u8 h=0;h<GF4_HEADS;++h) for (u32 n=0;n<3;++n) {
        assert(nodes[h][n].value.owner==&nodes[h][n]);
        assert(nodes[h][n].value.generation==1);
    }
}
static void record(void)
{
    golden_count=host.nevents;
    gf2_host_memcpy(golden,events,golden_count*sizeof(*events));
}
static void test_save(void)
{
    setup();
    assert(!gf4_display_save(&host.ctx,&saved,T_BUDGET));
    assert(host.ctx.saved_closed && !host.ctx.diagnostic_saved);
    assert(saved.version==GF4_SAVED_VERSION && saved.size==sizeof(saved));
    assert(saved.wanted_heads==(1U<<GF4_HEADS)-1U && saved.wanted_fifo==U32_MAX);
    assert(!gf2_host_memcmp(saved.head,before,sizeof(before)));
    assert(saved.hold[0].pending && saved.hold[0].after==U32_MAX);
    assert(events[0].op==D_ADMISSION && !events[0].a[0]);
    assert(host.calls[D_FENCE]==1 && host.calls[D_FENCE_WAIT]==1 && host.calls[D_UNMAP]==1);
    assert(!host.ctx.cursor_view[0].mapped && !host.ctx.cursor_view[1].mapped);
    for (u8 h=0;h<GF4_HEADS;++h) assert(host.sr[h][T_SR_CLOCK]&T_SCREEN_OFF);
    retained(); record();
    /* Every fallible call in a successful save: original rc, no subsequent
     * effects, unchanged output and all reservations/windows on failure. */
    for (u32 i=0;i<golden_count;++i) {
        enum gf4_host_op op=golden[i].op;
        setup(); gf4_host_fail_at(&host,op,golden[i].nth,T_ERROR);
        assert(gf4_display_save(&host.ctx,&saved,T_BUDGET)==T_ERROR);
        assert(!gf2_host_memcmp(&saved,&untouched,sizeof(saved)));
        assert(host.nevents==i+1U && host.ctx.last_rc==T_ERROR);
        assert(!host.ctx.saved_closed); retained();
        if (op==D_UNMAP || op==D_FENCE || op==D_FENCE_WAIT || op==D_ADMISSION)
            assert(host.ctx.cursor_view[0].mapped && host.ctx.cursor_view[1].mapped);
    }
    setup(); assert(!gf4_diagnostic_save(&host.ctx,&saved,T_BUDGET));
    assert(host.ctx.diagnostic_saved==&saved && host.ctx.saved_closed);
    for (u8 h=0;h<GF4_HEADS;++h)
        assert(!saved.head[h].cursor_valid && !state.head[h].cursor_valid && saved.head[h].cursor_visible);
    record();
    for (u32 i=0;i<golden_count;++i) {
        setup(); gf4_host_fail_at(&host,golden[i].op,golden[i].nth,T_ERROR);
        assert(gf4_diagnostic_save(&host.ctx,&saved,T_BUDGET)==T_ERROR);
        assert(host.nevents==i+1U && !host.ctx.diagnostic_saved);
        assert(!gf2_host_memcmp(&saved,&untouched,sizeof(saved))); retained();
    }
    setup(); assert(!gf4_diagnostic_save(&host.ctx,&saved,T_BUDGET));
    u32 count=host.nevents;
    assert(gf4_diagnostic_save(&host.ctx,&untouched,T_BUDGET)==GF4_EBUSY);
    assert(gf4_display_save(&host.ctx,&untouched,T_BUDGET)==GF4_EBUSY);
    assert(host.nevents==count);
    assert(gf4_display_saved_release(&host.ctx,&untouched)==GF4_ESTATE);
    assert(host.ctx.diagnostic_saved==&saved);
    assert(!gf4_display_saved_release(&host.ctx,&saved)); retained();
    assert(!host.ctx.diagnostic_saved && host.ctx.hold[0].pending);
    setup(); gf4_host_fail_at(&host,D_WRITE,1,T_ERROR);
    assert(gf4_diagnostic_save(&host.ctx,&saved,T_BUDGET)==T_ERROR);
    assert(!gf2_host_memcmp(&saved,&untouched,sizeof(saved)) && !host.ctx.diagnostic_saved);
    assert(!host.calls[D_IRQ] && state.head[0].cursor_valid); retained();
}
static void restore_setup(bool diagnostic)
{
    setup();
    assert(!gf4_display_save(&host.ctx,&saved,T_BUDGET));
    if (diagnostic) host.ctx.diagnostic_saved=&saved;
    gf4_host_trace_clear(&host);
}
static u32 mmio(u32 off)
{
    u32 value=0;
    for (u32 b=0;b<sizeof(value);++b)
        value|=gf4_host_reg_byte(&host,GF_MODE_REGISTER_BAR,off+b)<<(b*8U);
    return value;
}
static void expect_write(u32 i,u32 off,u32 width,u32 value)
{
    assert(i<host.nevents);
    const struct gf4_host_event *e=&events[i];
    assert(e->op==D_WRITE && !e->rc && e->a[0]==GF_MODE_REGISTER_BAR &&
           e->a[1]==off && e->a[2]==width && e->a[3]==value);
}
/* A failed C0 load restores its shared PLL gate and CR44 owner. The
 * successful trace locates the fallible call inside each load; cleanup
 * values/order come from hw.h:70-124 and the two-head fixture (B after blanking, A after head A's DPMS,
 * the saved shared gate), independently of the successful output values. */
static void restore_failure_tail(u32 failed)
{
    u32 tail=failed+1U,load_head=0;
    for (u32 start=1;start<golden_count;++start) {
        if (golden[start].op!=D_READ || golden[start].a[1]!=GF4_HOST_CR+1U ||
            golden[start-1U].op!=D_WRITE || golden[start-1U].a[1]!=GF4_HOST_CR ||
            golden[start-1U].a[3]!=T_CR_OWNER) continue;
        u32 end=start+1U;
        while (end<golden_count && !(golden[end].op==D_READ &&
               (golden[end].a[1]==T_DACCLK ||
                golden[end].a[1]==T_DACCLK+GF4_HOST_STRIDE))) ++end;
        assert(end>=start+3U && end<golden_count);
        /* The final two calls are CR44 selector/data cleanup. An error
         * in that cleanup itself must also stop at the failing call. */
        if (failed>start && failed<end-2U) {
            bool gate_saved=false;
            for (u32 i=start+1U;i<failed;++i)
                if (golden[i].op==D_READ && golden[i].a[1]==GF_PLL_GATE)
                    gate_saved=true;
            if (gate_saved) expect_write(tail++,GF_PLL_GATE,4,T_PLL_GATE_SAVED);
            expect_write(tail++,GF4_HOST_CR,1,T_CR_OWNER);
            expect_write(tail++,GF4_HOST_CR+1U,1,load_head ? 0 : T_OWNER_B);
            break;
        }
        ++load_head;
    }
    assert(host.nevents==tail);
}
/* Independent sequences, never replay a product-generated golden trace.
 * golden[] above is used only to enumerate fallible call positions. */
static void restore_oracle(void)
{
    for (u8 h=0;h<GF4_HEADS;++h) {
        const struct gf4_head_image *v=&saved.head[h];
        u32 delta=h*GF4_HOST_STRIDE,off=v->cursor.off;
        bool visible=v->cursor_visible && v->dpms==GF4_ON && !v->blank;
        u32 cfg=T_CURSOR_NORMAL|((v->mode.t.flags&GF4_DBLSCAN) ? T_CURSOR_DBLSCAN : 0);
        u32 low=((off>>11)&0x3fU)<<2 |
                ((v->mode.t.flags&GF4_DBLSCAN) ? T_CURSOR_DOUBLE_ADDRESS : 0);
        u32 pos=((u32)(u16)(v->y-v->hot_y)<<16)|(u16)(v->x-v->hot_x);
        assert(mmio(T_CURSOR_CFG+delta)==cfg);
        assert(state.committed[h].cursor_cfg==cfg);
        assert(mmio(T_CURSOR_POS+delta)==pos);
        assert(host.cr[h][T_CURSOR_LOW]==(low|(visible ? T_CURSOR_VISIBLE : 0)));
        /* cursor.c:39-62: final config -> address bytes -> workaround -> pos.
         * Locate the last config write, then require each immediate write. */
        u32 cursor=U32_MAX,lut=U32_MAX,dac=U32_MAX;
        for (u32 i=0;i<host.nevents;++i) if (events[i].op==D_WRITE) {
            if (events[i].a[1]==T_CURSOR_CFG+delta) cursor=i;
            if (events[i].a[1]==GF4_HOST_DAC_INDEX+delta) lut=i;
            if (events[i].a[1]==T_DAC_TEST+delta) dac=i;
        }
        assert(cursor!=U32_MAX && lut!=U32_MAX && dac!=U32_MAX);
        u32 cursor_begin=cursor;
        expect_write(cursor++,T_CURSOR_CFG+delta,4,cfg);
        const u32 indices[]={T_CURSOR_HIGH,T_CURSOR_LOW,T_CURSOR_TOP};
        const u32 values[]={T_CURSOR_ENABLE_ADDRESS|((off>>17)&0x7fU),low,off>>24};
        for (u32 i=0;i<sizeof(indices)/sizeof(indices[0]);++i) {
            expect_write(cursor++,GF4_HOST_CR+delta,1,indices[i]);
            expect_write(cursor++,GF4_HOST_CR+delta+1U,1,values[i]);
        }
        assert(events[cursor].op==D_READ && events[cursor].a[1]==T_CURSOR_POS+delta);
        ++cursor; /* NV40 workaround read/write precedes the new position. */
        assert(events[cursor].op==D_WRITE && events[cursor].a[1]==T_CURSOR_POS+delta);
        ++cursor;
        expect_write(cursor,T_CURSOR_POS+delta,4,pos);
        /* hw.c:795-810: complete INDEX8 palette in RGB byte order. */
        expect_write(lut++,GF4_HOST_DAC_INDEX+delta,1,0);
        for (u32 i=0;i<sizeof(v->palette);++i)
            expect_write(lut++,GF4_HOST_DAC_DATA+delta,1,v->palette[i]);
        assert(events[lut].op==D_READ && events[lut].a[1]==GF4_HOST_STATUS+delta);
        expect_write(lut+1U,GF4_HOST_AR+delta,1,T_LUT_GATE);
        assert(!gf2_host_memcmp(host.lut[h],v->palette,sizeof(v->palette)));
        /* crtc.c:198-233: DPMS settling is between LUT and cursor restore,
         * independently of any waits in C0's mode load. */
        u32 settle_count=0;
        for (u32 i=lut+2U;i<cursor_begin;++i) if (events[i].op==D_SLEEP) {
            assert(events[i].a[0]==T_SETTLE_US);
            ++settle_count;
        }
        assert(settle_count==1U);
        /* dac.c:364-444: encoder route and test precede the final cursor. */
        expect_write(dac,T_DAC_TEST+delta,4,T_DAC_TEST_VALUE);
        assert(dac<lut && lut<cursor);
        assert(mmio(T_DACCLK+delta)==((h ? T_DAC_HEAD_B : 0)|
            (v->dpms==GF4_ON ? T_DAC_ENABLE : 0)));
        assert(state.committed[h].pllsel==T_PLLSEL_BOTH);
        assert(mmio(T_PLLSEL)==T_PLLSEL_BOTH);
        const struct gf3_surface *front=saved.hold[h].pending ?
            &saved.hold[h].old_front : &v->front;
        assert(mmio(T_START+delta)==front->off);
    }
}
static void test_dblscan(void)
{
    /* Both heads and both states catch a lost bit and a spuriously set bit.
     * The cursor remains valid even on the physically hidden head B. */
    for (u32 flags=0;flags<=GF4_DBLSCAN;flags+=GF4_DBLSCAN) {
        restore_setup(true);
        for (u8 h=0;h<GF4_HEADS;++h) saved.head[h].mode.t.flags|=flags;
        assert(!gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET));
        restore_oracle();
    }
}
static void test_restore(void)
{
    restore_setup(true);
    assert(!gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET));
    assert(host.ctx.accepting && !host.ctx.saved_closed && host.ctx.diagnostic_saved==&saved);
    assert(!host.ctx.hold[0].pending && !host.ctx.hold[1].pending);
    assert(state.head[0].front.off==saved.hold[0].old_front.off);
    assert(host.base.f.wanted_heads==saved.wanted_heads && host.base.ch.heads==saved.wanted_heads);
    assert(legacy.blank==saved.head[0].blank && !gf2_host_memcmp(legacy.palette,saved.head[0].palette,sizeof(legacy.palette)));
    assert(host.local_now>=GF4_HEADS*T_SETTLE_US);
    assert(!(host.sr[0][T_SR_CLOCK]&T_SCREEN_OFF));
    assert(host.sr[1][T_SR_CLOCK]&T_SCREEN_OFF);
    assert((host.cr[1][T_RPC]&(T_SYNC_H|T_SYNC_V))==T_SYNC_V);
    for (u8 h=0;h<GF4_HEADS;++h) {
        assert(host.cr[h][T_CR_MODE]&T_CRTC_ON);
        u32 off=saved.head[h].cursor.off;
        assert(host.cr[h][T_CURSOR_HIGH]==(T_CURSOR_ENABLE_ADDRESS|((off>>17)&0x7fU)));
        assert(host.cr[h][T_CURSOR_LOW]==((((off>>11)&0x3fU)<<2)|
            (saved.head[h].cursor_visible && saved.head[h].dpms==GF4_ON &&
             !saved.head[h].blank ? T_CURSOR_VISIBLE : 0)));
        assert(host.cr[h][T_CURSOR_TOP]==(off>>24));
        u32 pos=0; for (u32 b=0;b<4;++b)
            pos|=gf4_host_reg_byte(&host,GF_MODE_REGISTER_BAR,T_CURSOR_POS+h*GF4_HOST_STRIDE+b)<<(b*8U);
        assert(pos==((u32)(u16)(saved.head[h].y-saved.head[h].hot_y)<<16 |
                     (u16)(saved.head[h].x-saved.head[h].hot_x)));
    }
    restore_oracle();
    assert(events[host.nevents-1].op==D_ADMISSION && events[host.nevents-1].a[0]);
    record();
    for (u32 i=0;i<golden_count;++i) {
        enum gf4_host_op op=golden[i].op;
        restore_setup(true);
        gf4_host_fail_at(&host,op,golden[i].nth,T_ERROR);
        assert(gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET)==T_ERROR);
        restore_failure_tail(i); retained();
        assert(host.ctx.saved_closed && host.ctx.diagnostic_saved==&saved);
        /* Admission is last: its failure follows a complete HW/logical
         * commit; every earlier failure leaves the old logical image. */
        if (i+1U<golden_count) {
            assert(!gf2_host_memcmp(state.head,before,sizeof(before)));
            assert(!gf2_host_memcmp(state.committed,committed,sizeof(committed)));
            assert(!gf2_host_memcmp(state.tv,tv_before,sizeof(tv_before)));
        }
        gf4_host_fail_at(&host,op,0,0); gf4_host_trace_clear(&host);
        assert(!gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET));
        assert(host.ctx.accepting && !host.ctx.saved_closed);
    }
    restore_setup(false);
    assert(gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET)==GF4_ESTATE);
    assert(!host.nevents);
    assert(!gf4_display_restore(&host.ctx,&saved,T_BUDGET));
    assert(host.ctx.saved_closed && !host.ctx.accepting && !host.ctx.diagnostic_saved);
    /* The same management restore also works while the normal slot is open. */
    restore_setup(true); assert(!gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET));
    gf4_host_trace_clear(&host);
    assert(!gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET));
    assert(host.ctx.accepting && host.ctx.diagnostic_saved==&saved);
    assert(!gf4_display_saved_release(&host.ctx,&saved));
    assert(host.ctx.accepting && state.head[0].front.reservation_id==saved.head[0].front.reservation_id);
    /* Each DPMS value keeps its own sync pattern; blank is an independent
     * screen-off demand. Expected rows are crtc.c:198-233, not save.c. */
    const u32 sync[]={0,T_SYNC_H,T_SYNC_V,T_SYNC_H|T_SYNC_V};
    for (u32 mode=GF4_ON;mode<=GF4_OFF;++mode) {
        restore_setup(true); saved.head[1].dpms=(enum gf4_dpms)mode;
        saved.head[1].blank=false;
        assert(!gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET));
        assert((host.cr[1][T_RPC]&(T_SYNC_H|T_SYNC_V))==sync[mode]);
        assert((host.cr[1][T_CR_MODE]&T_CRTC_ON)==(mode==GF4_OFF ? 0 : T_CRTC_ON));
        assert((host.sr[1][T_SR_CLOCK]&T_SCREEN_OFF)==(mode==GF4_ON ? 0 : T_SCREEN_OFF));
    }
    /* dfp.c:204-244: the B binding is bit18 (OR B), and both work images
     * must carry it even though head A loads first. Non-binding bit stays. */
    restore_setup(true);
    legacy.baseline.sel_clk=T_BIND_A|T_BIND_B|T_CLOCK_OTHER;
    saved.head[0].route.type=GF4_TMDS; saved.head[1].route.type=GF4_TMDS;
    for (u8 h=0;h<GF4_HEADS;++h) routes[h]=saved.head[h].route;
    assert(!gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET));
    for (u8 h=0;h<GF4_HEADS;++h) assert(state.committed[h].sel_clk==(T_BIND_B|T_CLOCK_OTHER));
    /* Restore a caller-owned image from RUN with no existing loan; failed
     * HW restoration remains distinguishable from an F5-owned save. */
    assert(!gf4_display_saved_release(&host.ctx,&saved));
    gf4_host_trace_clear(&host); gf4_host_fail_at(&host,D_WRITE,1,T_ERROR);
    assert(gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET)==T_ERROR);
    assert(host.ctx.saved_closed && host.ctx.diagnostic_saved==&saved); retained();
}
static void test_invalid(void)
{
    restore_setup(true); saved.version++;
    assert(gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET)==GF4_EINVAL);
    assert(!host.calls[D_WRITE]); retained();
    restore_setup(true); saved.size--;
    assert(gf4_display_restore(&host.ctx,&saved,T_BUDGET)==GF4_EINVAL);
    assert(!host.calls[D_WRITE]); retained();
    /* Stale generation, wrong owner, front/back/cursor and both held faces. */
    for (u32 key=0;key<5;++key) {
        restore_setup(true);
        if (key==0) saved.head[0].front.generation++;
        if (key==1) saved.head[1].back.generation++;
        if (key==2) saved.head[1].cursor.owner=&saved;
        if (key==3) saved.hold[0].old_front.generation++;
        if (key==4) saved.hold[0].new_front.generation++;
        assert(gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET)==GF3_ESTATE);
        assert(!host.calls[D_WRITE]); retained();
    }
    setup(); host.ctx.changing=true;
    assert(gf4_diagnostic_save(&host.ctx,&saved,T_BUDGET)==GF4_EBUSY && !host.nevents);
    setup(); host.ctx.epoch++;
    assert(gf4_display_save(&host.ctx,&saved,T_BUDGET)==GF4_ESTATE && !host.nevents);
    setup(); assert(gf4_display_save(&host.ctx,NULL,T_BUDGET)==GF4_EINVAL && !host.nevents);
    setup(); assert(gf4_display_save(&host.ctx,&saved,0)==GF4_ETIME);
    assert(!host.calls[D_FENCE] && !host.calls[D_WRITE]); retained();
}
void _start(void)
{
    test_save(); test_restore(); test_dblscan(); test_invalid(); gf2_host_exit(0);
}
