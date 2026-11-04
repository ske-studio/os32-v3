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
/*
 * Copyright 2012 Red Hat Inc.
 * Copyright 2021 Red Hat Inc.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
/*
 * Copyright 2008 Stuart Bennett
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* C2-recover, GF_FEAT4_DESIGN X1-X4, N2/N3 (2026-11-04).
 * MIT nouveau c03c1a1: nouveau_drm.c:944-1027 (display/fence/object
 * stop and reverse restart), nvkm/engine/fifo/uchan.c:272-298 (block/bind).
 * OS32 keeps resident reservation nodes and value loans instead of BOs;
 * no local quiesce, release, replay, or publication during restart. */
#include "api.h"
#include "../internal.h"
#include "../accel_service.h"
#include "../production.h"
#include "../../../drivers/video/geforce/geforce_module.h"

/* dispnv04/crtc.c:175-233, hw.h:110-140,238-240, nvreg.h:129-139.
 * Suspend uses physical DPMS ON without a transaction/publication. */
#define REC_CR 0x6013d4U
#define REC_SR 0x0c03c4U
#define REC_OWNER 0x44U
#define REC_OWNER_B 3U
#define REC_RPC 0x1aU
#define REC_MODE 0x17U
#define REC_CLOCK 1U
#define REC_SCREEN_OFF 0x20U
#define REC_MODE_ON 0x80U
#define REC_SYNC_MASK 0xc0U
#define REC_SETTLE_US 10000U
#define REC_RESET_BEGIN 1U
#define REC_RESET_END 3U

static i32 rec_result(struct gf4_ctx *c,i32 rc)
{
    if (c) c->last_rc=rc;
    return rc;
}
static i32 rec_check(struct gf4_ctx *c,struct gf3_service **service)
{
    if (!c || !c->ch || !c->ch->f || !c->accel || !c->state ||
        !c->legacy || !c->legacy->owner || !c->links || !c->links->admission ||
        !c->legacy->owner->foundation_stop || !c->legacy->owner->foundation_start ||
        !c->legacy->owner->validate || !c->legacy->owner->acquire ||
        !c->legacy->owner->release) return GF4_EINVAL;
    *service=gf3_service_get(c->ch);
    if (!*service) return GF4_EINVAL;
    if (c->changing) return GF4_EBUSY;
    if (c->accel!=&(*service)->production->accel ||
        c->accel->ch!=c->ch || c->legacy->display4!=c) return GF4_ESTATE;
    return 0;
}
static void rec_capture(struct gf4_ctx *c)
{
    struct gf4_saved *s=&c->hold_saved;
    s->version=GF4_SAVED_VERSION; s->size=sizeof(*s);
    s->wanted_fifo=c->ch->f->wanted_fifo;
    s->wanted_graph=c->ch->f->wanted_graph;
    s->wanted_heads=c->ch->f->wanted_heads;
    for (u8 h=0;h<GF4_HEADS;++h) {
        s->head[h]=c->state->head[h]; s->hold[h]=c->hold[h];
    }
}
static i32 rec_index(struct gf4_ctx *c,u32 port,u32 index,u32 *value,bool write)
{
    struct gf_io *io=&c->ch->f->io;
    i32 rc=io->write(io->opaque,GF_MODE_REGISTER_BAR,port,1,index);
    if (rc) return rc;
    return write ? io->write(io->opaque,GF_MODE_REGISTER_BAR,port+1U,1,*value) :
                   io->read(io->opaque,GF_MODE_REGISTER_BAR,port+1U,1,value);
}
static i32 rec_put(struct gf4_ctx *c,u32 port,u32 index,u32 value)
{ return rec_index(c,port,index,&value,true); }
static i32 rec_dpms_on(struct gf4_ctx *c,u8 h,u32 dl)
{
    struct gf_io *io=&c->ch->f->io;
    if (!io->read || !io->write || !io->timing.clock_us || !io->timing.wait_us)
        return GF4_EINVAL;
    u32 cr=REC_CR+h*RECOVER_HEAD_STRIDE,sr=REC_SR+h*RECOVER_HEAD_STRIDE;
    u32 rpc,clock,mode,now;
    const struct gf4_head_image *v=&c->state->head[h];
    i32 rc=rec_put(c,REC_CR,REC_OWNER,h ? REC_OWNER_B : 0);
    if (!rc) rc=rec_index(c,cr,REC_RPC,&rpc,false);
    if (!rc) rc=rec_put(c,sr,0,REC_RESET_BEGIN);
    if (!rc) rc=rec_index(c,sr,REC_CLOCK,&clock,false);
    /* F5 save has already blanked/hid physically. Never undo that blank. */
    if (!rc) rc=rec_put(c,sr,REC_CLOCK,(clock&~REC_SCREEN_OFF)|
                        ((c->saved_closed || v->blank) ? REC_SCREEN_OFF : 0));
    if (!rc) rc=rec_index(c,cr,REC_MODE,&mode,false);
    if (!rc) rc=io->timing.clock_us(io->opaque,&now);
    if (!rc && ((i32)(now-dl)>=0 || dl-now<REC_SETTLE_US)) rc=GF4_ETIME;
    if (!rc) rc=io->timing.wait_us(io->opaque,REC_SETTLE_US);
    if (!rc) rc=io->timing.clock_us(io->opaque,&now);
    if (!rc && (i32)(now-dl)>=0) rc=GF4_ETIME;
    if (!rc) rc=rec_put(c,cr,REC_MODE,mode|REC_MODE_ON);
    if (!rc) rc=rec_put(c,sr,0,REC_RESET_END);
    if (!rc) rc=rec_put(c,cr,REC_RPC,rpc&~REC_SYNC_MASK);
    if (!rc) switch (v->route.type) {
    case GF4_ANALOG: rc=gf4_analog_dpms(c,&v->route,GF4_ON,dl); break;
    case GF4_TMDS: case GF4_LVDS: rc=gf4_tmds_dpms(c,&v->route,GF4_ON,dl); break;
    case GF4_TV: rc=gf4_tv_dpms(c,&v->route,GF4_ON,dl); break;
    default: rc=GF4_EUNSUPPORTED; break;
    }
    return rc;
}
/* Only recover may continue past an old fault stop rc, after OFF and
 * successful close/cleanup. The public suspend still returns that rc. */
static i32 rec_suspend(struct gf4_ctx *c,struct gf3_service *s,u32 dl,bool *old_fault)
{
    bool fault=c->ch->fault.latched || c->ch->state==GF2_FAULT;
    c->deadline=dl; c->accepting=false;
    i32 rc=c->links->admission(c->links->owner,false);
    if (rc) return rc;
    /* N3: snapshot before pending convergence, physical hide or DPMS ON. */
    rec_capture(c);
    i32 first=0;
    if (!fault) {
        for (u8 h=0;h<GF4_HEADS;++h) if (c->hold[h].pending) {
            rc=gf4_flip_complete(c,h,dl);
            if (rc && !first) first=rc; /* hold survives until display stops */
        }
        struct gf2_fence fence;
        rc=gf2_fence_emit(c->ch,&fence,dl);
        if (!rc) rc=gf2_fence_wait(c->ch,fence,dl);
        if (rc && !first) first=rc;
    }
    for (u8 h=0;h<GF4_HEADS;++h) if (c->state->head[h].enabled) {
        rc=gf4_cursor_apply(c,h,false);
        if (rc && !first) first=rc;
        rc=rec_dpms_on(c,h,dl);
        if (rc && !first) first=rc;
    }
    rc=gf3_service_close(s);
    bool closed=!rc;
    if (rc && !first) first=rc;
    if (!closed && !c->ch->f->revoked) return first;
    if (closed) {
        rc=gf3_service_fini(s,dl);
        if (rc && !first) first=rc;
    }
    i32 stopped=gf2_stop(c->ch);
    if (c->ch->state!=GF2_OFF) return first ? first : stopped ? stopped : GF2_ESTOP;
    gf3_service_invalidate(s);
    c->ready=false;
    /* Direct owner call: never normal stop/local quiesce/reservation release. */
    rc=c->legacy->owner->foundation_stop(c->legacy);
    if (rc==-1 && c->ch->f->last_rc<0) rc=c->ch->f->last_rc;
    *old_fault=fault && !first && stopped<0 && !rc;
    if (!first) first=stopped;
    if (!first) first=rc;
    return first;
}
i32 gf4_service_suspend(struct gf4_ctx *c,u32 dl)
{
    struct gf3_service *s=NULL;
    i32 rc=rec_check(c,&s);
    if (!rc && (!c->ready || c->epoch!=c->ch->epoch ||
                (c->ch->state!=GF2_RUN && c->ch->state!=GF2_FAULT))) rc=GF4_ESTATE;
    bool old_fault=false;
    if (!rc) rc=rec_suspend(c,s,dl,&old_fault);
    return rec_result(c,rc);
}
static i32 rec_surface(struct gf4_ctx *c,const struct gf3_surface *v,bool required)
{
    if (!required && !v->reservation_id && !v->bytes) return 0;
    struct gf_vram_reservation live;
    return gf_vram_match(c->ch,NULL,v->reservation_id,v->generation,v->off,v->bytes,&live);
}
static i32 rec_node(struct gf4_ctx *c,const struct gf_vram_reservation_node *n)
{
    if (!n->value.owner) return 0;
    struct gf_vram_reservation live;
    const struct gf_vram_reservation *v=&n->value;
    return gf_vram_match(c->ch,v->owner,v->reservation_id,v->generation,v->off,v->bytes,&live);
}
static i32 rec_matches(struct gf4_ctx *c)
{
    for (u8 h=0;h<GF4_HEADS;++h) {
        const struct gf4_head_image *v=&c->hold_saved.head[h];
        const struct gf4_flip_hold *hold=&c->hold_saved.hold[h];
        i32 rc=rec_surface(c,&v->front,v->enabled);
        if (!rc) rc=rec_surface(c,&v->back,false);
        if (!rc) rc=rec_surface(c,&hold->old_front,hold->pending);
        if (!rc) rc=rec_surface(c,&hold->new_front,hold->pending);
        /* X1: use the shared display-format boundary, not only BYTES.
         * Ready/epoch are internal here; admission remains closed. */
        if (!rc && v->enabled) rc=gf4_surface_check(c,&v->front,&v->mode);
        if (!rc && v->enabled && v->back.bytes)
            rc=gf4_surface_check(c,&v->back,&v->mode);
        if (!rc && v->enabled && hold->pending)
            rc=gf4_surface_check(c,&hold->old_front,&v->mode);
        if (!rc && v->enabled && hold->pending)
            rc=gf4_surface_check(c,&hold->new_front,&v->mode);
        if (!rc && v->cursor_valid && !v->cursor.owner) rc=GF4_ESTATE;
        if (!rc && v->cursor.owner) {
            struct gf_vram_reservation live;
            rc=gf_vram_match(c->ch,v->cursor.owner,v->cursor.reservation_id,
                             v->cursor.generation,v->cursor.off,v->cursor.bytes,&live);
        }
        for (u8 b=0;!rc && b<2U;++b) {
            rc=rec_node(c,&c->scan[h][b]);
            if (!rc) rc=rec_node(c,&c->scan_next[h][b]);
        }
        if (!rc) rc=rec_node(c,&c->cursor[h]);
        if (rc) return rc;
    }
    return rec_node(c,&c->test);
}
i32 gf4_service_restart(struct gf4_ctx *c,u32 dl)
{
    struct gf3_service *s=NULL;
    i32 rc=rec_check(c,&s);
    if (!rc && (c->ready || c->accel->ready || c->ch->state!=GF2_OFF ||
                c->hold_saved.version!=GF4_SAVED_VERSION ||
                c->hold_saved.size!=sizeof(c->hold_saved))) rc=GF4_ESTATE;
    if (rc) return rec_result(c,rc);
    c->deadline=dl; c->accepting=false;
    rc=c->links->admission(c->links->owner,false);
    /* nouveau_drm.c:858,881: master precedes device initialization. */
    if (!rc) rc=gf_master_enable(c->ch->f);
    if (!rc && !c->legacy->initialized) {
        struct gf_display_context *l=c->legacy;
        rc=l->owner->acquire(l);
        if (!rc) rc=gf_init_run(l->init,l->program);
        if (!rc) rc=gf_display_baseline(l);
        l->owner->release(l);
        l->init=NULL; l->program=NULL;
        if (!rc) l->initialized=true;
    } else if (!rc) rc=c->legacy->owner->validate(c->legacy);
    if (!rc) {
        rc=c->legacy->owner->foundation_start(c->legacy);
        if (rc==-1 && c->ch->f->last_rc<0) rc=c->ch->f->last_rc;
    }
    if (!rc) rc=gf2_start(c->ch,c->ch->f);
    if (!rc) rc=gf3_service_objects_init(s,dl);
    if (!rc) {
        c->epoch=c->ch->epoch; c->ready=true;
        rc=rec_matches(c);
    }
    const struct gf4_head_image *h=&c->hold_saved.head[0];
    if (!rc) rc=gf3_service_surfaces(s,h->enabled ? &h->front : NULL,
                                    c->test.value.owner ? &s->test : NULL);
    if (rc) c->ready=false;
    return rec_result(c,rc);
}
i32 gf4_service_recover(struct gf4_ctx *c,u32 dl)
{
    struct gf3_service *s=NULL;
    i32 rc=rec_check(c,&s);
    if (!rc && (!c->ready || c->epoch!=c->ch->epoch ||
                (c->ch->state!=GF2_RUN && c->ch->state!=GF2_FAULT))) rc=GF4_ESTATE;
    if (rc) return rec_result(c,rc);
    bool saved_closed=c->saved_closed,old_fault=false;
    rc=rec_suspend(c,s,dl,&old_fault);
    c->recovery_rc=rc;
    if (rc && !old_fault) return rec_result(c,rc);
    rc=gf4_service_restart(c,dl);
    if (!rc) rc=gf4_display_restore(c,&c->hold_saved,dl);
    if (!rc) {
        c->saved_closed=saved_closed;
        if (!(saved_closed && c->diagnostic_saved==NULL))
            rc=c->links->admission(c->links->owner,true);
    }
    if (rc) {
        c->accepting=false;
        c->recovery_rc=rc;
    } else c->accepting=!(saved_closed && c->diagnostic_saved==NULL);
    return rec_result(c,rc);
}
/* C2-links binds links.failed to this typed callback with owner=gf4_ctx.
 * Normal stop owns release/local cleanup; backend alone selects PC-98. */
i32 gf4_service_failed(void *owner,i32 cause,u32 dl)
{
    struct gf4_ctx *c=owner;
    if (!c) return GF4_EINVAL;
    c->accepting=false; c->reusable=false;
    i32 rc=gf4_service_stop(c,dl);
    if (rc) c->recovery_rc=rc;
    geforce_display_failed();
    return rec_result(c,cause);
}
