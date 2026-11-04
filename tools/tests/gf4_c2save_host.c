/* C2-save caller contracts, UNIT 14 (no fixture dependency symbols).
 * MIT nouveau c03c1a1 dispnv04/crtc.c:671-706, hw.c:827-835;
 * DPMS expectations below come independently from crtc.c:198-233.
 * Link this TU with hwdev/gf/display/save.c, -nostdlib, entry _start.
 * Local call oracles cover dependencies whose C2/C0 stages land separately. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#include "../../hwdev/gf/internal.h"

static struct gf4_host host;
static struct gf4_state state;
static struct gf_display_context legacy;
static struct gf4_saved saved, untouched;
static struct gf4_host_event events[4096], golden[4096];
static struct gf3_host_reg regs[64];
static struct gf_vram_reservation_node nodes[GF4_HEADS][3];
static struct gf4_head_image before[GF4_HEADS];
static struct gfinit_mode committed[GF4_HEADS];
static struct gf4_tv_state tv_before[GF4_HEADS];
static u32 golden_count;
/* Test storage patterns are distinct sentinels, never hardware values. */
#define T_SENTINEL 0xa5U
#define T_ERROR GF2_EIO
#define T_BUDGET GF3_TIMEOUT_US
#define T_SURFACE_BYTES MEM_PAGE_SIZE
#define T_SURFACE_PITCH 64U
#define T_SURFACE_WIDTH 16U
#define T_SURFACE_HEIGHT 16U
#define T_FIRST_OFF (GF_MODE_VRAM_BYTES + MEM_PAGE_SIZE)
/* Independent hardware oracle: crtc.c:198-233 and hw.h:238-240. */
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
i32 gf4_cursor_apply(struct gf4_ctx *c,u8 h,bool visible)
{
    struct gf4_host_event *e=begin(D_CURSOR_APPLY);
    e->p[0]=c; e->a[0]=h; e->a[1]=visible;
    assert(c==&host.ctx && h<GF4_HEADS && !c->accepting);
    const struct gf4_head_image *v=&(c->changing ? state.next : state.head)[h];
    if (!e->rc && visible && (!v->enabled || !v->cursor_valid || v->blank || v->dpms!=GF4_ON))
        e->rc=GF4_ESTATE;
    return end(e);
}
int gf_irq_enable(struct gf_foundation *f,bool fifo,bool graph,unsigned heads)
{
    struct gf4_host_event *e=begin(D_IRQ);
    e->p[0]=f; e->a[0]=fifo; e->a[1]=graph; e->a[2]=heads;
    assert(f==host.ctx.ch->f && !host.ctx.accepting);
    f->wanted_fifo=fifo ? U32_MAX : 0;
    f->wanted_graph=graph ? U32_MAX : 0; f->wanted_heads=heads;
    return end(e);
}
i32 gf4_heads_apply(struct gf4_ctx *c,u32 mask)
{
    i32 rc=gf_irq_enable(c->ch->f,c->ch->f->wanted_fifo!=0,
                          c->ch->f->wanted_graph!=0,mask);
    if (!rc) c->ch->heads=mask;
    return rc;
}
i32 gf4_route_check(struct gf4_ctx *c,const struct gf4_route *r)
{
    struct gf4_host_event *e=begin(D_ROUTE); e->p[0]=c; e->p[1]=r;
    assert(c->changing && !c->accepting); return end(e);
}
i32 gf4_surface_check(struct gf4_ctx *c,const struct gf3_surface *s,const struct gf4_mode *m)
{
    struct gf4_host_event *e=begin(D_SURFACE);
    e->p[0]=c; e->p[1]=s; e->p[2]=m; return end(e);
}
int gf4_mode_calc(const struct gf4_mode *m,const struct gf4_route *r,
                  const struct gfinit_mode_saved *base,const struct gfinit_pll_limits *limits,
                  const struct gf3_surface *front,struct gfinit_mode *out)
{
    struct gf4_host_event *e=begin(D_LOAD);
    e->p[0]=m; e->p[1]=r; e->p[2]=base; e->p[3]=limits; e->p[4]=front; e->p[5]=out;
    assert(base==&legacy.baseline && limits==&host.ctx.limits[r->head]);
    /* Opaque result, no product formulas. The expected base is the explicit
     * caller input; no clock/timing/register calculation is duplicated. */
    if (!e->rc) { *out=(struct gfinit_mode){0}; out->head=r->head; out->start=front->off; }
    return end(e);
}
i32 gf4_pllsel_calc(const struct gf4_head_image heads[GF4_HEADS],u32 *out)
{
    struct gf4_host_event *e=begin(D_PLLSEL); e->p[0]=heads; e->p[1]=out;
    if (!e->rc) *out=T_SENTINEL;
    return end(e);
}
int gf4_mode_load(struct gf4_ctx *c,u8 h,const struct gfinit_mode *m,u32 dl)
{
    struct gf4_host_event *e=begin(D_LOAD);
    e->p[0]=c; e->p[1]=m; e->a[0]=h; e->a[1]=dl;
    assert(c->changing && c->saved_closed && !c->accepting && m->pllsel==T_SENTINEL);
    assert(dl==T_BUDGET && c->deadline==dl);
    /* Model a TV helper touching candidate state, so failure rollback matters. */
    if (!e->rc) state.tv[h].norm=T_SENTINEL;
    return end(e);
}
#define ENCODER_LOAD(name) \
i32 name(struct gf4_ctx *c,const struct gf4_route *r,const struct gfinit_mode *m,u32 dl) \
{ struct gf4_host_event *e=begin(D_EXTERNAL); e->p[0]=c; e->p[1]=r; e->p[2]=m; \
  e->a[0]=dl; return end(e); }
ENCODER_LOAD(gf4_analog_load)
ENCODER_LOAD(gf4_tmds_load)
ENCODER_LOAD(gf4_tv_load)
#undef ENCODER_LOAD
#define ENCODER_DPMS(name) \
i32 name(struct gf4_ctx *c,const struct gf4_route *r,enum gf4_dpms mode,u32 dl) \
{ struct gf4_host_event *e=begin(D_DPMS); e->p[0]=c; e->p[1]=r; \
  e->a[0]=mode; e->a[1]=dl; return end(e); }
ENCODER_DPMS(gf4_analog_dpms)
ENCODER_DPMS(gf4_tmds_dpms)
ENCODER_DPMS(gf4_tv_dpms)
#undef ENCODER_DPMS
i32 gf4_lut_load(struct gf4_ctx *c,u8 h)
{
    struct gf4_host_event *e=begin(D_LUT); e->p[0]=c; e->a[0]=h;
    if (!e->rc) state.next[h].lut_dirty=false;
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
    state.version=GF4_STATE_VERSION; state.size=sizeof(state);
    host.base.f.wanted_fifo=host.base.f.wanted_graph=U32_MAX;
    host.base.f.wanted_heads=host.base.ch.heads=(1U<<GF4_HEADS)-1U;
    u32 off=(T_FIRST_OFF+MEM_PAGE_SIZE-1U)&~(MEM_PAGE_SIZE-1U),id=0;
    for (u8 h=0;h<GF4_HEADS;++h) {
        struct gf4_head_image *v=&state.head[h];
        v->enabled=v->cursor_valid=v->cursor_visible=true;
        v->route=(struct gf4_route){.head=h,.type=GF4_ANALOG,.head_mask=1U<<h,.or_mask=1U<<h};
        v->mode.id=h+1U; v->mode.fmt=GF3_INDEX8; v->mode.pitch=T_SURFACE_PITCH;
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
        host.ctx.limits_valid[h]=true;
        state.committed[h].start=T_SENTINEL+h;
        state.tv[h].norm=h;
        before[h]=*v; committed[h]=state.committed[h]; tv_before[h]=state.tv[h];
        host.ctx.cursor_view[h].mapped=true;
    }
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
    setup(); gf4_host_fail_at(&host,D_CURSOR_APPLY,1,T_ERROR);
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
static void test_restore(void)
{
    restore_setup(true);
    assert(!gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET));
    assert(host.ctx.accepting && !host.ctx.saved_closed && host.ctx.diagnostic_saved==&saved);
    assert(!host.ctx.hold[0].pending && !host.ctx.hold[1].pending);
    assert(state.head[0].front.off==saved.hold[0].old_front.off);
    assert(host.base.f.wanted_heads==saved.wanted_heads && host.base.ch.heads==saved.wanted_heads);
    assert(legacy.blank==saved.head[0].blank && !gf2_host_memcmp(legacy.palette,saved.head[0].palette,sizeof(legacy.palette)));
    assert(host.calls[D_LUT]==GF4_HEADS && host.calls[D_SLEEP]==GF4_HEADS);
    assert(host.local_now==GF4_HEADS*T_SETTLE_US);
    assert(!(host.sr[0][T_SR_CLOCK]&T_SCREEN_OFF));
    assert(host.sr[1][T_SR_CLOCK]&T_SCREEN_OFF);
    assert((host.cr[1][T_RPC]&(T_SYNC_H|T_SYNC_V))==T_SYNC_V);
    for (u8 h=0;h<GF4_HEADS;++h) {
        assert(host.cr[h][T_CR_MODE]&T_CRTC_ON);
        u32 off=saved.head[h].cursor.off;
        assert(host.cr[h][T_CURSOR_HIGH]==(T_CURSOR_ENABLE_ADDRESS|((off>>17)&0x7fU)));
        assert(host.cr[h][T_CURSOR_LOW]==(((off>>11)&0x3fU)<<2));
        assert(host.cr[h][T_CURSOR_TOP]==(off>>24));
        u32 pos=0; for (u32 b=0;b<4;++b)
            pos|=gf4_host_reg_byte(&host,GF_MODE_REGISTER_BAR,T_CURSOR_POS+h*GF4_HOST_STRIDE+b)<<(b*8U);
        assert(pos==((u32)(u16)8<<16 | (u16)-9));
    }
    assert(events[host.nevents-1].op==D_ADMISSION && events[host.nevents-1].a[0]);
    record();
    for (u32 i=0;i<golden_count;++i) {
        enum gf4_host_op op=golden[i].op;
        restore_setup(true);
        gf4_host_fail_at(&host,op,golden[i].nth,T_ERROR);
        assert(gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET)==T_ERROR);
        assert(host.nevents==i+1U); retained();
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
    legacy.baseline.sel_clk=(1U<<16)|(1U<<18)|1U;
    saved.head[0].route.type=GF4_TMDS; saved.head[1].route.type=GF4_TMDS;
    assert(!gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET));
    for (u8 h=0;h<GF4_HEADS;++h) assert(state.committed[h].sel_clk==((1U<<18)|1U));
    /* Restore a caller-owned image from RUN with no existing loan; failed
     * HW restoration remains distinguishable from an F5-owned save. */
    assert(!gf4_display_saved_release(&host.ctx,&saved));
    gf4_host_trace_clear(&host); gf4_host_fail_at(&host,D_LOAD,3,T_ERROR);
    assert(gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET)==T_ERROR);
    assert(host.ctx.saved_closed && host.ctx.diagnostic_saved==&saved); retained();
}
static void test_invalid(void)
{
    restore_setup(true); saved.version++;
    assert(gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET)==GF4_EINVAL);
    assert(!host.calls[D_WRITE] && !host.calls[D_LOAD]); retained();
    restore_setup(true); saved.size--;
    assert(gf4_display_restore(&host.ctx,&saved,T_BUDGET)==GF4_EINVAL);
    assert(!host.calls[D_WRITE] && !host.calls[D_LOAD]); retained();
    /* Stale generation, wrong owner, front/back/cursor and both held faces. */
    for (u32 key=0;key<5;++key) {
        restore_setup(true);
        if (key==0) saved.head[0].front.generation++;
        if (key==1) saved.head[1].back.generation++;
        if (key==2) saved.head[1].cursor.owner=&saved;
        if (key==3) saved.hold[0].old_front.generation++;
        if (key==4) saved.hold[0].new_front.generation++;
        assert(gf4_diagnostic_restore(&host.ctx,&saved,T_BUDGET)==GF3_ESTATE);
        assert(!host.calls[D_WRITE] && !host.calls[D_LOAD]); retained();
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
    test_save(); test_restore(); test_invalid(); gf2_host_exit(0);
}
