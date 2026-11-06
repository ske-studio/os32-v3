/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
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
/* C2-save-a/b. MIT nouveau c03c1a1, paths relative to nouveau/:
 * dispnv04/crtc.c:671-706 (save/restore), hw.c:827-835 (ordered load).
 * OS32 snapshots logical values and retains reservation identities instead
 * of DRM BOs. No reservation, window or dispatch table is copied/released. */
#include "api.h"
#include "../internal.h"
#include "../accel_service.h"
#include "../production.h"
/* PM integrates i64 into types.h; no 64-bit division. */
typedef long long i64;

/* crtc.c:175-233; hw.h:110-140,238-240; nvreg.h:129-139.
 * Restore uses the same CRTC DPMS sequence without nesting C0 transactions. */
#define SAVE_CR 0x6013d4U
#define SAVE_SR 0x0c03c4U
#define SAVE_OWNER 0x44U
#define SAVE_OWNER_B 3U
#define SAVE_RPC 0x1aU
#define SAVE_MODE 0x17U
#define SAVE_RESET 0U
#define SAVE_CLOCK 1U
#define SAVE_SCREEN_OFF 0x20U
#define SAVE_MODE_ON 0x80U
#define SAVE_SYNC_H 0x80U
#define SAVE_SYNC_V 0x40U
#define SAVE_SETTLE_US 10000U
#define SAVE_RESET_BEGIN 1U
#define SAVE_RESET_END 3U
/* cursor.c:39-62; hw.h:341-349,372-385; nvreg.h:257-264,301-303. */
#define SAVE_CURSOR_CFG 0x600810U
#define SAVE_CURSOR_POS 0x680300U
#define SAVE_CURSOR_HI 0x30U
#define SAVE_CURSOR_LO 0x31U
#define SAVE_CURSOR_TOP 0x2fU
#define SAVE_CURSOR_ADDRESS 0x80U
#define SAVE_CURSOR_DOUBLE 2U
#define SAVE_CURSOR_MIN (-32768L)
#define SAVE_CURSOR_MAX 32767L

static i32 save_result(struct gf4_ctx *c,i32 rc)
{
    if (c) c->last_rc=rc;
    return rc;
}
static i32 save_check(struct gf4_ctx *c)
{
    if (!c || !c->state || !c->ch || !c->ch->f || !c->links ||
        !c->links->admission || !c->ch->f->io.read || !c->ch->f->io.write ||
        !c->ch->f->io.timing.clock_us || !c->ch->f->io.timing.wait_us) return GF4_EINVAL;
    if (!c->ready || c->epoch!=c->ch->epoch) return GF4_ESTATE;
    return c->changing ? GF4_EBUSY : 0;
}
static i32 save_time(struct gf4_ctx *c,u32 dl)
{
    struct gf_io *io=&c->ch->f->io;
    u32 now;
    if (!io->timing.clock_us) return GF4_EINVAL;
    i32 rc=io->timing.clock_us(io->opaque,&now);
    return rc ? rc : (i32)(now-dl)>=0 ? GF4_ETIME : 0;
}
static i32 save_write(struct gf4_ctx *c,u8 h,u32 off,u32 width,u32 value)
{
    struct gf_io *io=&c->ch->f->io;
    return io->write(io->opaque,GF_MODE_REGISTER_BAR,
                     off+h*RECOVER_HEAD_STRIDE,width,value);
}
static i32 save_read(struct gf4_ctx *c,u8 h,u32 off,u32 width,u32 *value)
{
    struct gf_io *io=&c->ch->f->io;
    return io->read(io->opaque,GF_MODE_REGISTER_BAR,
                    off+h*RECOVER_HEAD_STRIDE,width,value);
}
static i32 save_index(struct gf4_ctx *c,u8 h,u32 port,u32 index,u32 value)
{
    i32 rc=save_write(c,h,port,1,index);
    return rc ? rc : save_write(c,h,port+1U,1,value);
}
static i32 save_index_read(struct gf4_ctx *c,u8 h,u32 port,u32 index,u32 *value)
{
    i32 rc=save_write(c,h,port,1,index);
    return rc ? rc : save_read(c,h,port+1U,1,value);
}
/* Physical blank only: never change the saved logical blank demand. */
static i32 save_screen(struct gf4_ctx *c,u8 h,bool off)
{
    u32 value;
    i32 rc=save_index(c,0,SAVE_CR,SAVE_OWNER,h ? SAVE_OWNER_B : 0);
    if (!rc) rc=save_index_read(c,h,SAVE_SR,SAVE_CLOCK,&value);
    if (!rc) rc=save_index(c,h,SAVE_SR,SAVE_RESET,SAVE_RESET_BEGIN);
    if (!rc) rc=save_index(c,h,SAVE_SR,SAVE_CLOCK,
                          (value&~SAVE_SCREEN_OFF)|(off ? SAVE_SCREEN_OFF : 0));
    if (!rc) rc=save_index(c,h,SAVE_SR,SAVE_RESET,SAVE_RESET_END);
    return rc;
}
/* admission controls F4 dispatch/scan hooks only; close the actual CPU
 * windows after a reference drain (vram_view.h:100-119). */
static i32 save_close(struct gf4_ctx *c,u32 dl)
{
    c->deadline=dl;
    c->accepting=false;
    i32 rc=c->links->admission(c->links->owner,false);
    struct gf2_fence fence;
    if (!rc) rc=save_time(c,dl);
    if (!rc) rc=gf2_fence_emit(c->ch,&fence,dl);
    if (!rc) rc=gf2_fence_wait(c->ch,fence,dl);
    if (!rc) rc=gf_vram_views_close(c->ch);
    return rc;
}
i32 gf4_display_save(struct gf4_ctx *c,struct gf4_saved *out,u32 dl)
{
    i32 rc=save_check(c);
    if (!rc && !out) rc=GF4_EINVAL;
    if (!rc && c->diagnostic_saved) rc=GF4_EBUSY;
    if (!rc && c->saved_closed) rc=GF4_ESTATE;
    if (rc) return save_result(c,rc);
    rc=save_close(c,dl);
    if (rc) return save_result(c,rc);
    u32 fifo=c->ch->f->wanted_fifo,graph=c->ch->f->wanted_graph;
    u32 heads=c->ch->f->wanted_heads;
    for (u8 h=0;h<GF4_HEADS;++h) {
        if (c->state->head[h].enabled) {
            rc=gf4_cursor_apply(c,h,false);
            if (!rc) rc=save_screen(c,h,true);
            if (rc) return save_result(c,rc);
        }
    }
    rc=gf4_heads_apply(c,0);
    if (rc) {
        /* irq.c:108-110 publishes wanted fields before fallible writes.
         * Preserve the original request for a save retry/management restore. */
        c->ch->f->wanted_fifo=fifo; c->ch->f->wanted_graph=graph;
        c->ch->f->wanted_heads=heads;
        return save_result(c,rc);
    }
    /* The caller may use rollback as its destination. Write no output byte
     * before every fallible step has succeeded. */
    out->version=GF4_SAVED_VERSION; out->size=sizeof(*out);
    out->wanted_fifo=fifo; out->wanted_graph=graph; out->wanted_heads=heads;
    for (u8 h=0;h<GF4_HEADS;++h) {
        out->head[h]=c->state->head[h]; out->hold[h]=c->hold[h];
    }
    c->saved_closed=true;
    return save_result(c,0);
}
static i32 save_surface(struct gf4_ctx *c,const struct gf3_surface *s,bool required)
{
    if (!required && !s->reservation_id && !s->bytes) return 0;
    struct gf_vram_reservation live;
    return gf_vram_match(c->ch,NULL,s->reservation_id,s->generation,s->off,s->bytes,&live);
}
static i32 save_matches(struct gf4_ctx *c,const struct gf4_saved *s)
{
    if (s->version!=GF4_SAVED_VERSION || s->size!=sizeof(*s)) return GF4_EINVAL;
    for (u8 h=0;h<GF4_HEADS;++h) {
        const struct gf4_head_image *v=&s->head[h];
        i32 rc=save_surface(c,&v->front,v->enabled);
        if (!rc) rc=save_surface(c,&v->back,false);
        if (!rc && v->cursor.owner) {
            struct gf_vram_reservation live;
            rc=gf_vram_match(c->ch,v->cursor.owner,v->cursor.reservation_id,
                             v->cursor.generation,v->cursor.off,v->cursor.bytes,&live);
        }
        if (!rc && v->cursor_valid && !v->cursor.owner) rc=GF4_ESTATE;
        if (!rc) rc=save_surface(c,&s->hold[h].old_front,s->hold[h].pending);
        if (!rc) rc=save_surface(c,&s->hold[h].new_front,s->hold[h].pending);
        if (rc) return rc;
    }
    return 0;
}
static i32 save_encoder(struct gf4_ctx *c,const struct gf4_head_image *v,
                        enum gf4_dpms dpms,u32 dl)
{
    switch (v->route.type) {
    case GF4_ANALOG: return gf4_analog_dpms(c,&v->route,dpms,dl);
    case GF4_TMDS: case GF4_LVDS: return gf4_tmds_dpms(c,&v->route,dpms,dl);
    case GF4_TV: return gf4_tv_dpms(c,&v->route,dpms,dl);
    default: return GF4_EUNSUPPORTED;
    }
}
static i32 save_dpms(struct gf4_ctx *c,u8 h,const struct gf4_head_image *v,u32 dl)
{
    u32 rpc,mode,clock,now;
    struct gf_io *io=&c->ch->f->io;
    u32 sync=v->dpms==GF4_STANDBY ? SAVE_SYNC_H :
             v->dpms==GF4_SUSPEND ? SAVE_SYNC_V :
             v->dpms==GF4_OFF ? SAVE_SYNC_H|SAVE_SYNC_V : 0;
    i32 rc=save_index(c,0,SAVE_CR,SAVE_OWNER,h ? SAVE_OWNER_B : 0);
    if (!rc) rc=save_index_read(c,h,SAVE_CR,SAVE_RPC,&rpc);
    if (!rc) rc=save_index(c,h,SAVE_SR,SAVE_RESET,SAVE_RESET_BEGIN);
    if (!rc) rc=save_index_read(c,h,SAVE_SR,SAVE_CLOCK,&clock);
    if (!rc) rc=save_index(c,h,SAVE_SR,SAVE_CLOCK,(clock&~SAVE_SCREEN_OFF)|
                          ((v->blank || v->dpms!=GF4_ON) ? SAVE_SCREEN_OFF : 0));
    if (!rc) rc=save_index_read(c,h,SAVE_CR,SAVE_MODE,&mode);
    if (!rc) rc=io->timing.clock_us(io->opaque,&now);
    if (!rc && ((i32)(now-dl)>=0 || dl-now<SAVE_SETTLE_US)) rc=GF4_ETIME;
    if (!rc) rc=io->timing.wait_us(io->opaque,SAVE_SETTLE_US);
    if (!rc) rc=save_time(c,dl);
    if (!rc) rc=save_index(c,h,SAVE_CR,SAVE_MODE,
                          (mode&~SAVE_MODE_ON)|(v->dpms==GF4_OFF ? 0 : SAVE_MODE_ON));
    if (!rc) rc=save_index(c,h,SAVE_SR,SAVE_RESET,SAVE_RESET_END);
    if (!rc) rc=save_index(c,h,SAVE_CR,SAVE_RPC,(rpc&~(SAVE_SYNC_H|SAVE_SYNC_V))|sync);
    if (!rc) rc=save_encoder(c,v,v->dpms,dl);
    return rc;
}
static i32 save_cursor(struct gf4_ctx *c,u8 h,const struct gf4_head_image *v)
{
    if (!v->cursor_valid) return gf4_cursor_apply(c,h,false);
    /* Check subtraction in wider signed arithmetic, before narrowing. */
    i64 x=(i64)v->x-v->hot_x,y=(i64)v->y-v->hot_y;
    if (x<SAVE_CURSOR_MIN || x>SAVE_CURSOR_MAX ||
        y<SAVE_CURSOR_MIN || y>SAVE_CURSOR_MAX) return GF4_ERANGE;
    u32 off=v->cursor.off,pos;
    /* crtc.c:510-516: retain the timing-derived DBLSCAN configuration. */
    i32 rc=save_write(c,h,SAVE_CURSOR_CFG,4,c->state->work[h].cursor_cfg);
    if (!rc) rc=save_index(c,h,SAVE_CR,SAVE_CURSOR_HI,SAVE_CURSOR_ADDRESS|((off>>17)&0x7fU));
    if (!rc) rc=save_index(c,h,SAVE_CR,SAVE_CURSOR_LO,((off>>11)&0x3fU)<<2 |
                          ((v->mode.t.flags&GF4_DBLSCAN) ? SAVE_CURSOR_DOUBLE : 0));
    if (!rc) rc=save_index(c,h,SAVE_CR,SAVE_CURSOR_TOP,off>>24);
    if (!rc) rc=save_read(c,h,SAVE_CURSOR_POS,4,&pos);
    if (!rc) rc=save_write(c,h,SAVE_CURSOR_POS,4,pos);
    if (!rc) rc=save_write(c,h,SAVE_CURSOR_POS,4,((u32)(u16)y<<16)|(u16)x);
    if (!rc) rc=gf4_cursor_apply(c,h,v->cursor_visible && v->dpms==GF4_ON && !v->blank);
    return rc;
}
/* Only initial video start supplies a stage sink. Ordinary restores keep
 * their existing diagnostics and never emit a start-failure message. */
i32 gf4_display_restore_stage(struct gf4_ctx *c,const struct gf4_saved *s,u32 dl,
                              const char **stage)
{
    const char *ignored;
    if (!stage) stage=&ignored;
    *stage="restore_check";
    i32 rc=save_check(c);
    if (!rc && !s) rc=GF4_EINVAL;
    if (rc) return save_result(c,rc);
    *stage="restore_drain";
    rc=save_close(c,dl);
    if (!rc) { *stage="restore_matches"; rc=save_matches(c,s); }
    if (rc) return save_result(c,rc);
    if (!c->legacy) { *stage="restore_legacy"; return save_result(c,GF4_ESTATE); }
    /* Before any HW restore, prevent all nested operations from publishing. */
    c->saved_closed=true;
    c->changing=true;
    for (u8 h=0;h<GF4_HEADS;++h) {
        c->state->rollback_tv[h]=c->state->tv[h];
        c->state->next[h]=s->head[h];
        c->state->work[h]=c->state->committed[h];
    }
    for (u8 h=0;h<GF4_HEADS;++h) {
        /* G3: restore the old front, never replay an unfinished flip. */
        if (s->hold[h].pending) c->state->next[h].front=s->hold[h].old_front;
        if (!s->head[h].enabled) continue;
        *stage="mode_calc";
        if (!c->limits_valid[h]) { rc=GF4_EPLL; break; }
        rc=gf4_route_check(c,&c->state->next[h].route);
        if (!rc) rc=gf4_surface_check(c,&c->state->next[h].front,&s->head[h].mode);
        if (!rc) rc=gf4_mode_calc(&s->head[h].mode,&s->head[h].route,
                    &c->legacy->baseline,&c->limits[h],&c->state->next[h].front,&c->state->work[h]);
        if (rc) break;
    }
    u32 pllsel;
    if (!rc) { *stage="pllsel_calc"; rc=gf4_pllsel_calc(c->state->next,&pllsel); }
    /* dfp.c:204-244: rebuild both digital bindings before either head
     * loads the shared SEL_CLK; preserve unrelated baseline clock bits. */
    u32 sel_clk=c->legacy->baseline.sel_clk&~GF_MODE_SEL_CLK_CLEAR;
    for (u8 h=0;!rc && h<GF4_HEADS;++h) {
        const struct gf4_head_image *v=&c->state->next[h];
        if (!v->enabled || v->route.location ||
            (v->route.type!=GF4_TMDS && v->route.type!=GF4_LVDS)) continue;
        u32 binding=(v->route.or_mask&1U) ? (1U<<16) : (1U<<18);
        if (h) sel_clk|=binding;
        else sel_clk&=~binding;
        if (v->route.type==GF4_LVDS && (c->legacy->baseline.sel_clk&0xf0U)) {
            u32 shift=(c->legacy->baseline.sel_clk&0x50U) ? 0U : 1U;
            sel_clk=(sel_clk&~0xf0U)|((h ? 0x40U : 0x10U)<<shift);
        }
    }
    if (!rc) for (u8 h=0;h<GF4_HEADS;++h) {
        c->state->work[h].pllsel=pllsel;
        c->state->work[h].sel_clk=sel_clk;
    }
    /* Shut down old outputs before switching shared clocks/routes. */
    for (u8 h=0;!rc && h<GF4_HEADS;++h) if (c->state->head[h].enabled) {
        *stage="output_disable";
        rc=gf4_cursor_apply(c,h,false);
        if (!rc) rc=save_screen(c,h,true);
        if (!rc) rc=save_encoder(c,&c->state->head[h],GF4_OFF,dl);
    }
    for (u8 h=0;!rc && h<GF4_HEADS;++h) {
        struct gf4_head_image *v=&c->state->next[h];
        if (!v->enabled) continue;
        *stage="mode_deadline";
        rc=save_time(c,dl);
        /* The saved route can differ from the currently selected route.
         * crtc.c:710-733 / dfp.c:245-278: prepare the target encoder too;
         * on retry it may already be on after a partial earlier restore. */
        if (!rc) { *stage="encoder_off"; rc=save_encoder(c,v,GF4_OFF,dl); }
        if (!rc) { *stage="mode_load"; rc=gf4_mode_load(c,h,&c->state->work[h],dl); }
        if (!rc) {
            *stage="encoder_load";
            switch (v->route.type) {
            case GF4_ANALOG: *stage="analog_load"; rc=gf4_analog_load(c,&v->route,&c->state->work[h],dl); break;
            case GF4_TMDS: case GF4_LVDS: *stage="tmds_load"; rc=gf4_tmds_load(c,&v->route,&c->state->work[h],dl); break;
            case GF4_TV: *stage="tv_load"; rc=gf4_tv_load(c,&v->route,&c->state->work[h],dl); break;
            default: rc=GF4_EUNSUPPORTED; break;
            }
        }
        if (!rc) { *stage="lut_load"; rc=gf4_lut_load(c,h); }
        if (!rc) { *stage="dpms"; rc=save_dpms(c,h,v,dl); }
        if (!rc) { *stage="cursor"; rc=save_cursor(c,h,v); }
    }
    if (!rc) {
        *stage="heads_irq";
        rc=gf_irq_enable(c->ch->f,s->wanted_fifo!=0,s->wanted_graph!=0,s->wanted_heads);
    }
    if (!rc) {
        c->ch->heads=s->wanted_heads;
        for (u8 h=0;h<GF4_HEADS;++h) {
            c->state->head[h]=c->state->next[h];
            c->state->committed[h]=c->state->work[h];
            c->hold[h]=(struct gf4_flip_hold){0};
        }
        c->legacy->blank=c->state->head[0].blank;
        for (u32 i=0;i<sizeof(c->legacy->palette);++i)
            c->legacy->palette[i]=c->state->head[0].palette[i];
    }
    if (rc) for (u8 h=0;h<GF4_HEADS;++h)
        c->state->tv[h]=c->state->rollback_tv[h];
    c->changing=false;
    return save_result(c,rc);
}
i32 gf4_display_restore(struct gf4_ctx *c,const struct gf4_saved *s,u32 dl)
{
    return gf4_display_restore_stage(c,s,dl,NULL);
}

i32 gf4_diagnostic_save(struct gf4_ctx *c,struct gf4_saved *out,u32 dl)
{
    i32 rc=save_check(c);
    if (!rc && !out) rc=GF4_EINVAL;
    if (!rc && c->diagnostic_saved) rc=GF4_EBUSY;
    if (!rc && c->saved_closed) rc=GF4_ESTATE;
    if (rc) return save_result(c,rc);
    rc=save_close(c,dl);
    for (u8 h=0;!rc && h<GF4_HEADS;++h) if (c->state->head[h].cursor_valid) {
        rc=gf4_cursor_apply(c,h,false);
        if (!rc) c->state->head[h].cursor_valid=false;
    }
    if (!rc) rc=gf4_display_save(c,out,dl);
    if (!rc) c->diagnostic_saved=out;
    return save_result(c,rc);
}
i32 gf4_diagnostic_restore(struct gf4_ctx *c,const struct gf4_saved *s,u32 dl)
{
    i32 rc=save_check(c);
    if (!rc && !s) rc=GF4_EINVAL;
    if (!rc && c->saved_closed && !c->diagnostic_saved) rc=GF4_ESTATE;
    if (!rc && c->diagnostic_saved && c->diagnostic_saved!=s) rc=GF4_ESTATE;
    if (rc) return save_result(c,rc);
    /* A management restore of a caller-owned image also borrows that image
     * for retries. This keeps a failed diagnostic restore distinct from F5. */
    if (!c->diagnostic_saved) {
        rc=save_matches(c,s);
        if (rc) return save_result(c,rc);
        c->diagnostic_saved=s;
    }
    rc=gf4_display_restore(c,s,dl);
    if (!rc) {
        c->saved_closed=false;
        rc=c->links->admission(c->links->owner,true);
        if (!rc) c->accepting=true;
        else c->saved_closed=true;
    }
    return save_result(c,rc);
}
i32 gf4_display_saved_release(struct gf4_ctx *c,const struct gf4_saved *s)
{
    if (!c || !s) return save_result(c,GF4_EINVAL);
    if (c->diagnostic_saved!=s) return save_result(c,GF4_ESTATE);
    c->diagnostic_saved=NULL;
    return save_result(c,0);
}

/* MIT nouveau c03c1a1 dispnv04/disp.c:189-207 destroys the display image
 * after stopping display use, even when restoring it failed. OS32's fallible
 * releases keep their resident nodes and pending flag until a later retry. */
i32 gf4_diagnostic_abandon(struct gf4_ctx *c,const struct gf4_saved *s)
{
    if (!c || !s || !c->ch || !c->legacy || !c->state) return GF4_EINVAL;
    /* A released image is no longer borrowed or dereferenced. Only pending
     * cleanup may continue without a diagnostic pointer; a completed or stale
     * request must not release another session's reservations. */
    if (c->diagnostic_saved!=s &&
        (c->diagnostic_saved || !c->cleanup_pending)) return GF4_ESTATE;
    if (c->legacy->engaged || c->ch->state!=GF2_OFF || c->ready)
        return GF4_EBUSY;
    if (c->changing) return GF4_EBUSY;
    struct gf3_service *service=gf3_service_get(c->ch);
    if (!service || !service->production ||
        &service->production->display!=c->legacy ||
        &service->production->chan!=c->ch) return GF4_ESTATE;
    struct gf5 *g=&service->production->feature5;
    if (g->busy || g->image || g->phase==GF5_SAVING ||
        g->phase==GF5_SAVED || g->phase==GF5_RESTORING) return GF4_EBUSY;
    i32 original=c->last_rc;
    c->cleanup_pending=true;
    i32 rc=gf5_tiles_retire(g);
    if (!rc && c->diagnostic_saved) rc=gf4_display_saved_release(c,s);
    if (!rc) {
        c->saved_closed=false;
        rc=gf4_scan_release_all(c,c->deadline);
    }
    if (!rc) {
        gf4_service_cleanup_reset(c);
        c->cleanup_pending=false;
    }
    /* Cleanup status is the return value; never overwrite the restore cause
     * with saved_release's success or a subsequent release failure. */
    c->last_rc=original;
    return rc;
}
