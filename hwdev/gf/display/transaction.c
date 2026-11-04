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
/* OS32 local display transaction, extending the existing modeset recovery.
 * nouveau dispnv04/crtc.c:671-731 and hw.c:827-835: restore complete state.
 * F2 fences replace DRM BO pinning; no reservation is freed here. */
#ifdef GF_PRODUCTION_MODULE
#include "api.h"
#include "../foundation.h"

static int tx_admission(struct gf4_ctx *c,bool open)
{
    int rc=c->links->admission(c->links->owner,open);
    if (!rc) c->accepting=open;
    return rc;
}
static bool tx_publish(const struct gf4_ctx *c)
{
    /* Includes F5's saved_closed && !diagnostic_saved ownership predicate. */
    return !c->saved_closed;
}
static int tx_cursor(struct gf4_ctx *c,u8 head)
{
    const struct gf4_head_image *h=&c->state->next[head];
    return gf4_cursor_apply(c,head,h->enabled && h->cursor_visible &&
                            h->cursor_valid && !h->blank && h->dpms==GF4_ON);
}
static int tx_failed(struct gf4_ctx *c,int source,int recovery)
{
    c->recovery_rc=recovery;
    c->reusable=false; c->accepting=false;
    /* Keep the candidate/rollback/front/hold keys through failed stop. */
    c->changing=false;
    (void)c->links->failed(c->links->owner,source,c->deadline);
    return c->last_rc=source;
}
int gf4_change_begin(struct gf4_ctx *c,u32 dl)
{
    if (!c || !c->state || !c->ch || !c->ch->f || !c->links ||
        !c->links->admission || !c->links->failed) return GF4_EINVAL;
    if (c->changing) return GF4_EBUSY;
    if (!c->ready || !c->reusable || c->epoch!=c->ch->epoch) return GF4_ESTATE;
    c->deadline=dl; c->recovery_rc=0;
    bool reopen=c->accepting && tx_publish(c);
    c->accepting=false;
    int rc=tx_admission(c,false);
    for (u8 i=0;!rc && i<GF4_HEADS;i++) rc=gf4_flip_complete(c,i,dl);
    struct gf2_fence fence;
    if (!rc) rc=gf2_fence_emit(c->ch,&fence,dl);
    if (!rc) rc=gf2_fence_wait(c->ch,fence,dl);
    if (rc) {
        int recovery=reopen ? tx_admission(c,true) : 0;
        if (recovery) return tx_failed(c,rc,recovery);
        return c->last_rc=rc;
    }
    struct gf4_state *s=c->state;
    s->rollback.version=GF4_SAVED_VERSION; s->rollback.size=sizeof(s->rollback);
    s->rollback.wanted_fifo=c->ch->f->wanted_fifo;
    s->rollback.wanted_graph=c->ch->f->wanted_graph;
    s->rollback.wanted_heads=c->ch->f->wanted_heads;
    for (u8 i=0;i<GF4_HEADS;i++) {
        s->rollback.head[i]=s->head[i]; s->rollback.hold[i]=c->hold[i];
        s->rollback_tv[i]=s->tv[i]; s->next[i]=s->head[i];
        s->work[i]=s->committed[i];
        s->work[i].start=s->head[i].front.off;
    }
    c->changing=true;
    u8 hidden=0;
    for (;hidden<GF4_HEADS;hidden++) {
        if (!s->next[hidden].enabled) continue;
        rc=gf4_cursor_apply(c,hidden,false);
        if (rc) break;
    }
    if (rc) {
        /* A failing hide may have reached HW: include that head in undo. */
        int recovery=0;
        for (u8 i=0;i<=hidden;i++) {
            if (!s->next[i].enabled) continue;
            int r=tx_cursor(c,i);
            if (!recovery) recovery=r;
        }
        c->changing=false;
        if (!recovery && reopen) recovery=tx_admission(c,true);
        if (recovery) return tx_failed(c,rc,recovery);
        return c->last_rc=rc;
    }
    return 0;
}
static int tx_encoder(struct gf4_ctx *c,const struct gf4_route *r,
                       const struct gfinit_mode *m,enum gf4_dpms mode)
{
    u32 dl=c->deadline;
    switch (r->type) {
    case GF4_ANALOG: return m ? gf4_analog_load(c,r,m,dl) : gf4_analog_dpms(c,r,mode,dl);
    case GF4_TMDS: case GF4_LVDS: return m ? gf4_tmds_load(c,r,m,dl) : gf4_tmds_dpms(c,r,mode,dl);
    case GF4_TV: return m ? gf4_tv_load(c,r,m,dl) : gf4_tv_dpms(c,r,mode,dl);
    default: return GF4_EUNSUPPORTED;
    }
}
static int tx_restore(struct gf4_ctx *c)
{
    struct gf4_state *s=c->state;
    int rc=tx_admission(c,false);
    /* Shut down candidate encoders, including a head enabled by a failed start. */
    for (u8 i=0;!rc && i<GF4_HEADS;i++)
        if (s->next[i].enabled) rc=tx_encoder(c,&s->next[i].route,0,GF4_OFF);
    if (rc) return rc;
    for (u8 i=0;i<GF4_HEADS;i++) {
        s->next[i]=s->rollback.head[i]; s->tv[i]=s->rollback_tv[i];
        c->hold[i]=s->rollback.hold[i]; s->work[i]=s->committed[i];
    }
    bool any=false;
    for (u8 i=0;i<GF4_HEADS;i++) any|=s->next[i].enabled;
    /* No F4 committed HW image exists on an initial start failure. C2's
     * failed adapter runs legacy stop/local recovery, keeping ownership. */
    if (!any) return GF4_ESTATE;
    u32 pllsel;
    rc=gf4_pllsel_calc(s->next,&pllsel);
    for (u8 i=0;!rc && i<GF4_HEADS;i++) {
        const struct gf4_head_image *h=&s->next[i];
        if (!h->enabled) continue;
        if (!c->legacy || !c->limits_valid[i]) return GF4_ESTATE;
        rc=gf4_mode_calc(&h->mode,&h->route,&c->legacy->baseline,&c->limits[i],&h->front,&s->work[i]);
        if (!rc) {
            s->work[i].pllsel=pllsel;
            s->work[i].sel_clk=s->committed[i].sel_clk;
            rc=gf4_mode_load(c,i,&s->work[i],c->deadline);
        }
        if (!rc) rc=tx_encoder(c,&h->route,&s->work[i],h->dpms);
        if (!rc) rc=tx_encoder(c,&h->route,0,h->dpms);
        if (!rc) rc=tx_cursor(c,i);
    }
    if (!rc) {
        c->ch->f->wanted_fifo=s->rollback.wanted_fifo;
        c->ch->f->wanted_graph=s->rollback.wanted_graph;
        rc=gf4_heads_apply(c,s->rollback.wanted_heads);
    }
    if (!rc) for (u8 i=0;i<GF4_HEADS;i++) s->head[i]=s->rollback.head[i];
    if (!rc && c->legacy) c->legacy->blank=s->rollback.head[0].blank;
    return rc;
}
int gf4_change_end(struct gf4_ctx *c,int operation_rc)
{
    if (!c || !c->state || !c->changing) return GF4_ESTATE;
    struct gf4_state *s=c->state;
    int rc=operation_rc;
    u32 heads=0;
    for (u8 i=0;i<GF4_HEADS;i++)
        if (s->next[i].enabled && s->next[i].dpms==GF4_ON) heads|=1U<<i;
    if (!rc) rc=gf4_heads_apply(c,heads);
    /* A change on head B must also undo the temporary hide on head A. */
    for (u8 i=0;!rc && i<GF4_HEADS;i++)
        if (s->next[i].enabled) rc=tx_cursor(c,i);
    if (!rc && tx_publish(c)) rc=tx_admission(c,true);
    if (rc) {
        int recovery=tx_restore(c);
        if (!recovery && tx_publish(c)) recovery=tx_admission(c,true);
        if (recovery) return tx_failed(c,rc,recovery);
        c->changing=false; c->recovery_rc=0;
        return c->last_rc=rc;
    }
    /* Nothing fallible follows the last publication callback. */
    for (u8 i=0;i<GF4_HEADS;i++) {
        s->head[i]=s->next[i]; s->committed[i]=s->work[i];
    }
    if (c->legacy) c->legacy->blank=s->head[0].blank;
    c->changing=false;
    return c->last_rc=0;
}
#endif
