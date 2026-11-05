/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 * Copyright 2007-2009 Stuart Bennett
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/* Ordering/conditional waits belong only to gf_load_state. */
#include "../internal.h"
static int load_section(void *v, u32 section)
{
    struct gf_display_context *c=v;
    c->section=section;
    return 0;
}
static int load_row(void *v, const struct gfinit_mode *m, u32 ordinal)
{ return gf_load_row(v,m,ordinal); }
static int load_wait(void *v, u32 ms, bool fp_only)
{
    struct gf_display_context *c=v;
    return fp_only && !c->fp_on ? 0 : gf_init_wait(c->load_context,ms*RECOVER_US_PER_MS);
}
int gf_display_modeset(struct gf_display_context *c)
{
    struct gf_init_context tx={.io=c->io};
    static const struct gf_load_io io={load_section,load_row,load_wait};
    if (c->io.timing.clock_us(c->io.opaque,&tx.started_us)) return -1;
    c->load_context=&tx;
    c->pll_gate_valid=false;
    int rc=gf_load_state(&c->mode,c,&io);
    /* A failed PLL never leaves its snapshots referring to an expired stack. */
    if (rc && c->io.restore(c->io.opaque,&tx)) c->reusable=false;
    c->load_context=0;
    return rc;
}


#ifdef GF_PRODUCTION_MODULE
#include "api.h"
#include "../foundation.h"
#include "../../../include/kprintf.h"

/* nouveau dispnv04/hw.h:70-124, hw.c:75-106,468-535,665-835.
 * Encoder-specific DACCLK/script/FP programming belongs to B9/B10/B11.
 * The descriptor order and its conditional waits still belong to gf_load_state. */
#define MS_STRIDE 0x2000U
#define MS_CR 0x6013d4U
#define MS_OWNER 0x44U
#define MS_SR 0x0c03c4U
#define MS_DACCLK 0x68052cU
#define MS_DACCLK_B 0x68252cU
#define MS_TEST 0x680608U
#define MS_SEL_CLK 0x680524U
#define MS_PLLSEL 0x68050cU
#define MS_SCREEN_OFF 0x20U
/* crtc.c:334-337: SR1 character clock and optional divide by two. */
#define MS_SR_CLOCK 1U
#define MS_CLKDIV2 0x08U
/* tvnv17.h:54-70: route.tv_norm uses this frozen ordinal order. */
enum ms_tv_norm { MS_PAL, MS_PAL_M, MS_PAL_N, MS_PAL_NC, MS_NTSC_M,
    MS_NTSC_J, MS_HD480I, MS_HD480P, MS_HD576I, MS_HD576P,
    MS_HD720P, MS_HD1080I };
/* tvnv17.c:360-363; tvmodesnv17.c:148-200 (CTV output clocks). */
#define MS_TV_LD_KHZ 90000U
#define MS_TV_SD_KHZ 27000U
#define MS_TV_HD_KHZ 74250U
#define MS_MODE_ENABLE 0x80U
#define MS_SYNC_MASK 0xc0U
/* dfp.c:204-244: per-OR SEL_CLK fields (16/17 and 18/19). */
#define MS_SEL_A 0x10000U
#define MS_SEL_B 0x40000U
/* crtc.c:519-526: BIOS output classification scratch values. */
#define MS_SCRATCH_LVDS 0x11U
#define MS_SCRATCH_TMDS 0x88U

int gf4_mode_calc(const struct gf4_mode *mode,const struct gf4_route *route,
                 const struct gfinit_mode_saved *saved,const struct gfinit_pll_limits *limits,
                 const struct gf3_surface *front,struct gfinit_mode *out)
{
    if (!mode || !route || !saved || !limits || !front || !out || route->head>=GF4_HEADS)
        return GF4_EINVAL;
    if (route->max_khz && mode->t.khz>route->max_khz) return GF4_ERANGE;
    const struct gfinit_mode_head_saved *h=&saved->head[route->head];
    struct gfinit_mode m={0};
    /* crtc.c:491-570: immutable baseline, never previous mode's CR52. */
    m.head=route->head;
    m.cr[GF_MODE_CR_ENH]=h->cr1c & ~GF_MODE_ENH_CLEAR;
    m.cr[GF_MODE_CR_LCD]=h->cr33 & ~GF_MODE_LCD_CLEAR;
    m.cr[GF_MODE_CR_SCRATCH3]=route->type==GF4_LVDS ? MS_SCRATCH_LVDS :
        route->type==GF4_TMDS ? MS_SCRATCH_TMDS : GF_MODE_ANALOG_SCRATCH;
    m.cr[GF_MODE_CR_SCRATCH4]=h->cr3c;
    m.cr[GF_MODE_CR_SCRATCH4B]=h->cr4b | (route->head ? 0 : GF_MODE_HEAD_A_SCRATCH);
    m.cr[GF_MODE_CR_LATENCY]=saved->head[0].cr52 + (route->head ? 0 : GF_MODE_LATENCY_ADD);
    m.cr[GF_MODE_CR_9F]=1; m.cr[GF_MODE_CR_85]=GF_MODE_BYTE_MASK; m.cr[GF_MODE_CR_86]=1;
    m.cr[GF_MODE_CR_CURSOR]=GF_MODE_CURSOR_HIDDEN;
    m.engine_ctrl=route->head ? 0 : GF_MODE_ENGINE_I2C;
    m.cursor_cfg=GF_MODE_CURSOR_CONFIG; m.cursync=GF_MODE_CURSYNC;
    m.gpio_ext=saved->head[0].gpio_ext; m.crtc850=saved->head[0].crtc850;
    m.fp_control=GF_MODE_FP_OFF; m.ramdac8c0=GF_MODE_RAMDAC8C0;
    m.ramdaca24=GF_MODE_RAMDACA24; m.ramdaca34=1;
    int rc=gf4_timing_calc(mode,&m);
    if (!rc) rc=gf4_format_calc(mode,front,&m);
    if (!rc) {
        struct gf4_mode adjusted=*mode;
        bool divide=false;
        if (route->type==GF4_TV) {
            /* tvnv17.c:360-363: PLL follows encoder clock, not input clock.
             * HD480I/HD576I also use TV_ENC_MODE (tvmodesnv17.c:123-145).
             * The three LD doublescan inputs are precisely the CLKDIV2
             * modes (tvmodesnv17.c:439-451); A0 carries DBLSCAN only. */
            switch (route->tv_norm) {
            case MS_HD480P: case MS_HD576P: adjusted.t.khz=MS_TV_SD_KHZ; break;
            case MS_HD720P: case MS_HD1080I: adjusted.t.khz=MS_TV_HD_KHZ; break;
            default: adjusted.t.khz=MS_TV_LD_KHZ;
                divide=(mode->t.flags&GF4_DBLSCAN)!=0; break;
            }
        }
        gf4_fifo_calc(&m);
        rc=gf4_pll_calc(&adjusted,route,saved,limits,&m);
        if (!rc) m.sr[MS_SR_CLOCK]=(m.sr[MS_SR_CLOCK]&~MS_CLKDIV2)|
            (divide ? MS_CLKDIV2 : 0);
    }
    if (!rc) { m.head=route->head; *out=m; }
    return rc;
}

struct mode_load {
    struct gf4_ctx *ctx;
    u8 head;
    bool fp_on,gate_valid;
    u32 gate;
    i32 rc;
    const char *at;
    u32 section,row,reg,value;
};
/* Capture before cleanup: successful gate/owner writes overwrite l.rc. */
static int ms_failure(const struct mode_load *l,int rc,int gate_rc,int owner_rc)
{
    kprintf(KLOG_ATTR,"[gf4] mode_load fail at=%s sec=%u row=%u reg=%08x val=%08x rc=%d l.rc=%d gate_rc=%d owner_rc=%d\n",
            l->at,l->section,l->row,l->reg,l->value,rc,l->rc,gate_rc,owner_rc);
    return rc;
}
static int ms_read(struct mode_load *l,u32 off,u32 width,u32 *v)
{
    struct gf_io *io=&l->ctx->ch->f->io;
    l->reg=off; l->value=0;
    if (l->row!=GF_LOAD_NO_INDEX) l->at="read";
    return l->rc=io->read(io->opaque,GF_MODE_REGISTER_BAR,off,width,v);
}
static int ms_write(struct mode_load *l,u32 off,u32 width,u32 v)
{
    struct gf_io *io=&l->ctx->ch->f->io;
    l->reg=off; l->value=v;
    if (l->row!=GF_LOAD_NO_INDEX) l->at="write";
    return l->rc=io->write(io->opaque,GF_MODE_REGISTER_BAR,off,width,v);
}
static int ms_section(void *owner,u32 section)
{
    struct mode_load *l=owner;
    l->section=section;
    return 0;
}
static int ms_wait(void *owner,u32 ms,bool fp_only)
{
    struct mode_load *l=owner;
    struct gf_io *io=&l->ctx->ch->f->io;
    u32 now,us=ms*RECOVER_US_PER_MS;
    if (fp_only && !l->fp_on) return 0;
    l->reg=0; l->value=us;
    l->at="wait_clock_before";
    l->rc=io->timing.clock_us(io->opaque,&now);
    if (!l->rc && ((i32)(now-l->ctx->deadline)>=0 || l->ctx->deadline-now<us)) {
        l->at="wait_budget"; l->rc=GF4_ETIME;
    }
    /* native.c accepts at most 1ms per call; preserve the full 10/50ms. */
    while (!l->rc && us) {
        u32 part=us>GF_INIT_WAIT_CHUNK_US ? GF_INIT_WAIT_CHUNK_US : us;
        l->at="wait_us";
        l->rc=io->timing.wait_us(io->opaque,part);
        if (!l->rc) {
            l->at="wait_clock_after";
            l->rc=io->timing.clock_us(io->opaque,&now);
        }
        if (!l->rc && (i32)(now-l->ctx->deadline)>=0) {
            l->at="wait_deadline"; l->rc=GF4_ETIME;
        }
        us-=part;
    }
    return l->rc;
}
static int ms_row(void *owner,const struct gfinit_mode *m,u32 ordinal)
{
    struct mode_load *l=owner;
    const struct gf_mode_row *r=&gf_mode_rows[ordinal];
    const struct gf4_head_image *h=&l->ctx->state->next[l->head];
    u32 reg=r->reg,value=r->bits,old=0;
    l->row=ordinal; l->reg=reg; l->value=value; l->at="row";
    /* dac.c:364-444: no hard-coded analog OR programming in the common load. */
    if (reg==MS_DACCLK || reg==MS_DACCLK_B || reg==MS_TEST) return 0;
    if (r->kind==GF_C_DAC_MASK) {
        l->at="lut_load"; l->reg=reg+l->head*MS_STRIDE;
        return l->rc=gf4_lut_load(l->ctx,l->head);
    }
    if (r->kind==GF_C_DAC_INDEX || r->kind==GF_C_DAC_DATA) return 0;
    /* Shared PLL registers have their own addresses, never head stride. */
    if (r->expr==GF_C_PLL) {
        const struct gfinit_pll_write *w=&m->sequence.writes[r->source];
        reg=w->reg;
        if (!r->source) {
            if (ms_read(l,reg,r->width,&l->gate)) return l->rc;
            l->gate_valid=true;
            old=l->gate;
        } else if (reg==GF_PLL_GATE) old=l->gate;
        else if (w->source && ms_read(l,w->source,r->width,&old)) return l->rc;
        return ms_write(l,reg,r->width,(old&w->keep)|w->value);
    }
    /* hw.h:70-124: PCRTC, PRAMDAC, PRMCIO, PRMVIO aliases. CR44 is primary. */
    bool owner_row=r->kind==GF_C_CR && r->index==MS_OWNER;
    if (!owner_row && reg!=MS_PLLSEL && reg!=MS_SEL_CLK &&
        ((reg>=0x600000U && reg<0x602000U) ||
         (reg>=0x680000U && reg<0x682000U) ||
         (reg>=0x0c0000U && reg<0x0c2000U))) reg+=l->head*MS_STRIDE;
    if (r->flags & GF_LOAD_FP_ON) {
        if (ms_read(l,reg,r->width,&old)) return l->rc;
        l->fp_on=(old&GF_LOAD_FP_ON_MASK)!=0;
        if (!l->fp_on) return 0;
    }
    if (r->kind==GF_C_CR || r->kind==GF_C_SR || r->kind==GF_C_GR) {
        if (ms_write(l,reg,1,r->index)) return l->rc;
        reg++;
    }
    if (r->expr==GF_C_STATE) {
        value=0;
        for (u32 j=0;j<r->width;j++) value|=(u32)((const u8 *)m)[r->source+j]<<(8*j);
    } else if (r->expr==GF_C_LIVE) {
        if (ms_read(l,reg,r->width,&old)) return l->rc;
        value=(old&r->keep)|r->bits;
    }
    if (owner_row) value=l->head ? 3U : 0;
    /* crtc.c:175-233: the final unprotect must respect DPMS and blank. */
    if (ordinal>=GF_MODE_ROW_GATE_ON) {
        if (r->kind==GF_C_SR && r->index==MS_SR_CLOCK)
            value=(m->sr[MS_SR_CLOCK]&~MS_SCREEN_OFF)|((h->blank || h->dpms!=GF4_ON) ? MS_SCREEN_OFF : 0);
        if (r->kind==GF_C_CR && r->index==GF_MODE_CR_MODE)
            value=(value&~MS_MODE_ENABLE)|(h->dpms==GF4_OFF ? 0 : MS_MODE_ENABLE);
        if (r->kind==GF_C_CR && r->index==GF_MODE_CR_RPC1)
            value=(m->cr[GF_MODE_CR_RPC1]&~MS_SYNC_MASK)|
                (h->dpms==GF4_STANDBY ? 0x80U : h->dpms==GF4_SUSPEND ? 0x40U : h->dpms==GF4_OFF ? MS_SYNC_MASK : 0);
    }
    if (r->kind==GF_C_AR || r->kind==GF_C_AR_GATE) {
        if (ms_read(l,GF_MODE_AR_STATUS+l->head*MS_STRIDE,1,&old)) return l->rc;
        if (r->kind==GF_C_AR && ms_write(l,reg,1,r->index)) return l->rc;
    }
    return ms_write(l,reg,r->width,value);
}
int gf4_mode_load(struct gf4_ctx *ctx,u8 head,const struct gfinit_mode *mode,u32 dl)
{
    struct mode_load l={.ctx=ctx,.head=head,.at="validate",
        .section=GF_LOAD_NO_INDEX,.row=GF_LOAD_NO_INDEX};
    if (!ctx || !ctx->state || !ctx->ch || !ctx->ch->f || !mode || head>=GF4_HEADS || mode->head!=head)
        return ms_failure(&l,GF4_EINVAL,0,0);
    if (!ctx->changing || ctx->deadline!=dl) return ms_failure(&l,GF4_ESTATE,0,0);
    struct gf_io *io=&ctx->ch->f->io;
    u32 now,owner;
    l.at="clock";
    int rc=l.rc=io->timing.clock_us(io->opaque,&now);
    if (!rc && (i32)(now-dl)>=0) { l.at="deadline"; rc=GF4_ETIME; }
    if (rc) return ms_failure(&l,rc,0,0);
    l.at="owner_index";
    rc=ms_write(&l,MS_CR,1,MS_OWNER);
    if (!rc) { l.at="owner_read"; rc=ms_read(&l,MS_CR+1U,1,&owner); }
    if (rc) return ms_failure(&l,rc,0,0);
    /* cursor.c:39-62, crtc.c:510-516: preserve the reserved cursor offset;
     * the physical enable is restored only after the image is complete. */
    struct gfinit_mode image=*mode;
    const struct gf4_head_image *h=&ctx->state->next[head];
    if (h->cursor_valid) {
        image.cr[0x30]=0x80U|((h->cursor.off>>17)&0x7fU);
        image.cr[0x31]=((h->cursor.off>>11)&0x3fU)<<2;
        if (h->mode.t.flags&GF4_DBLSCAN) image.cr[0x31]|=2U;
        image.cr[0x2f]=h->cursor.off>>24;
    }
    static const struct gf_load_io load={ms_section,ms_row,ms_wait};
    rc=gf_load_state(&image,&l,&load);
    struct mode_load failure=l;
    int gate_rc=0;
    if (rc && l.gate_valid) gate_rc=ms_write(&l,GF_PLL_GATE,4,l.gate);
    l.row=GF_LOAD_NO_INDEX; l.at="owner_restore_index";
    int cleanup=ms_write(&l,MS_CR,1,MS_OWNER);
    if (!cleanup) { l.at="owner_restore"; cleanup=ms_write(&l,MS_CR+1U,1,owner); }
    if (rc) return ms_failure(&failure,rc,gate_rc,cleanup);
    if (cleanup) return ms_failure(&l,cleanup,gate_rc,cleanup);
    ctx->state->work[head]=image;
    return 0;
}
static int ms_encoder(struct gf4_ctx *ctx,const struct gf4_route *r,bool load,
                      const struct gfinit_mode *m,enum gf4_dpms dpms,u32 dl)
{
    switch (r->type) {
    case GF4_ANALOG: return load ? gf4_analog_load(ctx,r,m,dl) : gf4_analog_dpms(ctx,r,dpms,dl);
    case GF4_TMDS: case GF4_LVDS: return load ? gf4_tmds_load(ctx,r,m,dl) : gf4_tmds_dpms(ctx,r,dpms,dl);
    case GF4_TV: return load ? gf4_tv_load(ctx,r,m,dl) : gf4_tv_dpms(ctx,r,dpms,dl);
    default: return GF4_EUNSUPPORTED;
    }
}
int gf4_set_mode(struct gf4_ctx *ctx,const struct gf4_route *route,u32 id,
                 const struct gf3_surface *front,u32 dl)
{
    if (!ctx || !ctx->state || !ctx->ch || !ctx->ch->f || !route || !front || route->head>=GF4_HEADS)
        return GF4_EINVAL;
    if (ctx->changing) return GF4_EBUSY;
    if (!ctx->ready || !ctx->reusable || !ctx->accepting || ctx->epoch!=ctx->ch->epoch) return GF4_ESTATE;
    int rc=gf4_route_check(ctx,route);
    const struct gf4_mode *mode=0;
    for (u32 i=0;i<ctx->state->nmodes;i++)
        if (ctx->state->modes[i].id==id && ctx->state->mode_connector[i]==route->connector)
            mode=&ctx->state->modes[i];
    if (!rc && !mode) rc=GF4_EINVAL;
    if (!rc && !ctx->limits_valid[route->head]) rc=GF4_EPLL;
    if (!rc) rc=gf4_surface_check(ctx,front,mode);
    struct gfinit_mode prepared;
    if (!rc && !ctx->legacy) rc=GF4_ESTATE;
    if (!rc) rc=gf4_mode_calc(mode,route,&ctx->legacy->baseline,&ctx->limits[route->head],front,&prepared);
    if (rc) return ctx->last_rc=rc;
    rc=gf4_change_begin(ctx,dl);
    if (rc) return rc;
    u8 head=route->head;
    struct gf4_state *s=ctx->state;
    struct gf4_head_image *h=&s->next[head];
    if (h->enabled) rc=ms_encoder(ctx,&h->route,false,0,GF4_OFF,dl);
    h->enabled=true; h->mode=*mode; h->route=*route; h->front=*front;
    s->work[head]=prepared;
    u32 pllsel;
    if (!rc) rc=gf4_pllsel_calc(s->next,&pllsel);
    /* dfp.c:204-244: rebuild shared selection from both current requests. */
    u32 sel=ctx->legacy->baseline.sel_clk & ~(MS_SEL_A|MS_SEL_B);
    for (u32 i=0;i<GF4_HEADS;i++) {
        const struct gf4_head_image *n=&s->next[i];
        if (n->enabled && (n->route.type==GF4_TMDS || n->route.type==GF4_LVDS) && n->route.head)
            sel|=(n->route.or_mask&1U ? MS_SEL_A : MS_SEL_B);
        /* dfp.c:235-240: LVDS spread spectrum follows the saved mode pair. */
        if (n->enabled && n->route.type==GF4_LVDS && (ctx->legacy->baseline.sel_clk&0xf0U)) {
            u32 shift=(ctx->legacy->baseline.sel_clk&0x50U) ? 0 : 1;
            sel=(sel&~0xf0U)|((n->route.head ? 0x40U : 0x10U)<<shift);
        }
    }
    if (!rc) for (u32 i=0;i<GF4_HEADS;i++) { s->work[i].pllsel=pllsel; s->work[i].sel_clk=sel; }
    if (!rc) rc=ms_encoder(ctx,route,false,0,GF4_OFF,dl);
    if (!rc) rc=gf4_mode_load(ctx,head,&s->work[head],dl);
    if (!rc) rc=ms_encoder(ctx,route,true,&s->work[head],GF4_ON,dl);
    if (!rc) rc=ms_encoder(ctx,route,false,0,h->dpms,dl);
    if (!rc) rc=gf4_cursor_apply(ctx,head,h->cursor_visible && h->cursor_valid && !h->blank && h->dpms==GF4_ON);
    return gf4_change_end(ctx,rc);
}
#endif
