/*
 * Copyright 2009 Red Hat Inc.
 * Copyright 2012 Red Hat Inc.
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
 */
/* C2-service, GF_FEAT4_DESIGN X1-X4 / final N1-N4. MIT nouveau c03c1a1:
 * dispnv04/disp.c:247-294 (routes), nvkm/subdev/bios/base.c:180-182 (BMP),
 * nouveau_drm.c:944-988 (display, idle, objects stop order).
 * OS32 substitutes resident reservation nodes and borrowed dispatch slots
 * for DRM BOs. No hardware register/method is introduced in this adapter. */
#include "api.h"
#include "../internal.h"
#include "../accel_service.h"
#include "../production.h"
#include "../../../drivers/video/geforce/geforce_module.h"
#define GF_ROM_BIT_ONLY
#include "../rom.inc"
#undef GF_ROM_BIT_ONLY

/* C2-service owns the address; C4/kernel owns the borrowed state/helpers. */
static struct gf4_ctx svc_production_ctx;

static i32 svc_owner_start(void *,u32);
static i32 svc_owner_stop(void *,u32);
static i32 svc_owner_recover(void *,u32);
/* C3 borrows this exact address, including across complete video stops. */
static struct gf3_service_owner svc_owner={
    .start=svc_owner_start,.stop=svc_owner_stop,.recover=svc_owner_recover
};

const struct gf3_service_owner *gf4_service_owner_table(void)
{
    /* C3 lends this table before service_init; F3 requires a non-NULL
     * owner already then. Preserve a context installed by the bind API. */
    if (!svc_owner.owner) svc_owner.owner=&svc_production_ctx;
    return &svc_owner;
}

static i32 svc_result(struct gf4_ctx *c,i32 rc)
{
    if (c) {
        c->last_rc=rc;
        if (c->legacy) c->legacy->last_rc=rc;
    }
    return rc;
}

/* crtc.c:175-233; hw.h:119-140,238-240; nvreg.h:129-139.
 * Stop must not use the public DPMS transaction: its successful end could
 * reopen slots and restore cursor visibility. Only the physical ON column
 * is needed here, with the old independent blank demand preserved. */
#define SVC_CR 0x6013d4U
#define SVC_SR 0x0c03c4U
#define SVC_OWNER 0x44U
#define SVC_RPC 0x1aU
#define SVC_CONTROL 0x17U
#define SVC_RESET 0U
#define SVC_CLOCK 1U
#define SVC_SCREEN_OFF 0x20U
#define SVC_CONTROL_ON 0x80U
#define SVC_SYNC_MASK 0xc0U
#define SVC_SETTLE_US 10000U
static i32 svc_index(struct gf_io *io,u32 port,u32 index,u32 *value,bool write)
{
    i32 rc=io->write(io->opaque,GF_MODE_REGISTER_BAR,port,1,index);
    if (rc) return rc;
    return write ? io->write(io->opaque,GF_MODE_REGISTER_BAR,port+1U,1,*value) :
                   io->read(io->opaque,GF_MODE_REGISTER_BAR,port+1U,1,value);
}
static i32 svc_dpms_on(struct gf4_ctx *c,u8 head,u32 dl)
{
    struct gf_io *io=&c->ch->f->io;
    const struct gf4_head_image *h=&c->state->head[head];
    u32 cr=SVC_CR+head*RECOVER_HEAD_STRIDE,sr=SVC_SR+head*RECOVER_HEAD_STRIDE;
    u32 owner=head ? 3U : 0,rpc,clock,control,reset=1,now;
    i32 rc=svc_index(io,SVC_CR,SVC_OWNER,&owner,true);
    if (!rc) rc=svc_index(io,cr,SVC_RPC,&rpc,false);
    if (!rc) rc=svc_index(io,sr,SVC_RESET,&reset,true);
    if (!rc) rc=svc_index(io,sr,SVC_CLOCK,&clock,false);
    if (!rc) {
        clock=(clock&~SVC_SCREEN_OFF)|(h->blank ? SVC_SCREEN_OFF : 0);
        rc=svc_index(io,sr,SVC_CLOCK,&clock,true);
    }
    if (!rc) rc=svc_index(io,cr,SVC_CONTROL,&control,false);
    if (!rc) rc=io->timing.clock_us(io->opaque,&now);
    if (!rc && ((i32)(now-dl)>=0 || dl-now<SVC_SETTLE_US)) rc=GF4_ETIME;
    if (!rc) rc=io->timing.wait_us(io->opaque,SVC_SETTLE_US);
    if (!rc) rc=io->timing.clock_us(io->opaque,&now);
    if (!rc && (i32)(now-dl)>=0) rc=GF4_ETIME;
    if (!rc) {
        control|=SVC_CONTROL_ON; reset=3; rpc&=~SVC_SYNC_MASK;
        rc=svc_index(io,cr,SVC_CONTROL,&control,true);
    }
    if (!rc) rc=svc_index(io,sr,SVC_RESET,&reset,true);
    if (!rc) rc=svc_index(io,cr,SVC_RPC,&rpc,true);
    if (!rc) switch (h->route.type) {
    case GF4_ANALOG: rc=gf4_analog_dpms(c,&h->route,GF4_ON,dl); break;
    case GF4_TMDS: case GF4_LVDS: rc=gf4_tmds_dpms(c,&h->route,GF4_ON,dl); break;
    case GF4_TV: rc=gf4_tv_dpms(c,&h->route,GF4_ON,dl); break;
    default: rc=GF4_EUNSUPPORTED; break;
    }
    return rc;
}

static i32 svc_check(struct gf4_ctx *c,struct gf3_service **out)
{
    if (!c || !c->legacy || !c->ch || !c->accel || !c->state ||
        !c->links || !c->links->admission || !c->legacy->owner ||
        !c->legacy->owner->foundation_stop) return GF4_EINVAL;
    struct gf3_service *s=gf3_service_get(c->ch);
    if (svc_owner.owner!=c || !s || !s->production ||
        &s->production->display!=c->legacy || &s->production->accel!=c->accel ||
        s->owner!=&svc_owner || c->legacy->display4!=c) return GF4_ESTATE;
    *out=s;
    return c->changing ? GF4_EBUSY : 0;
}

i32 gf4_service_bind(struct gf4_ctx *c,struct gf_display_context *legacy,
                     const struct gf4_links *links)
{
    if (!c || !legacy || !links || !links->admission || !links->i2c_port ||
        !links->failed || !links->scan_changed) return svc_result(c,GF4_EINVAL);
    if (svc_owner.owner && svc_owner.owner!=c) return GF4_ESTATE;
    if ((legacy->display4 && legacy->display4!=c) ||
        (c->legacy && c->legacy!=legacy) || (c->links && c->links!=links))
        return svc_result(c,GF4_ESTATE);
    if (legacy->display4==c && c->legacy==legacy && c->links==links) return 0;
    if (c->ready || c->changing) return svc_result(c,GF4_ESTATE);
    c->legacy=legacy; c->links=links;
    c->accepting=false; c->reusable=true;
    if (!c->next_mode_id) c->next_mode_id=1;
    legacy->display4=c;
    svc_owner.owner=c;
    return 0;
}

/* Final C-group declaration / links-exec addendum (2026-11-04).
 * dispnv04/disp.c:247-294 creates the display objects before connecting
 * their dependencies. This resident ctx/module-loan adapter is OS32-only:
 * no ROM acquisition, register access, start or slot publication here. */
i32 gf4_service_init(struct gf_production *p,struct gf_display_context *legacy)
{
    if (!p || !legacy || legacy!=&p->display || !geforce_context ||
        !geforce_context->display_data || !geforce_context->display_state)
        return GF4_EINVAL;
    struct gf4_links *links=gf4_links_table();
    const struct gf4_exec_ops *exec=gf4_exec_table();
    if (!links || !exec || !links->admission || !links->i2c_port ||
        !links->failed || !links->scan_changed) return GF4_EINVAL;
    /* Reinitialization must never erase reservation nodes or a saved loan.
     * The module lends the same objects for the entire binding lifetime. */
    if ((svc_owner.owner && svc_owner.owner!=&svc_production_ctx) ||
        (legacy->display4 && legacy->display4!=&svc_production_ctx)) return GF4_ESTATE;
    if (svc_production_ctx.legacy) {
        if (svc_production_ctx.legacy!=legacy || svc_production_ctx.ch!=&p->chan ||
            svc_production_ctx.accel!=&p->accel || svc_production_ctx.links!=links || svc_production_ctx.exec!=exec ||
            svc_production_ctx.data!=geforce_context->display_data ||
            svc_production_ctx.state!=geforce_context->display_state) return GF4_ESTATE;
        return gf4_service_bind(&svc_production_ctx,legacy,links);
    }
    svc_production_ctx=(struct gf4_ctx){
        .ch=&p->chan,.accel=&p->accel,
        .state=geforce_context->display_state,.data=geforce_context->display_data,
        .links=links,.exec=exec
    };
    links->owner=&svc_production_ctx;
    return gf4_service_bind(&svc_production_ctx,legacy,links);
}

/* F5 borrows the same production owner as F3/F4. Prepare an independent M
 * program before releasing devinit scratch (nvkm/subdev/fb/ramnv40.c:154-156,
 * nvkm/subdev/bios/bit.c:29-55). No script execution or new register writes.
 * A missing M script still permits full-ROM acquisition. PM decision 8:
 * F5 failure is diagnostic only; the completed F4 baseline remains valid. */
#define SVC_MEMORY_PC_BYTES 2U /* ramnv40.c:156: nvbios_rd16(bios, M.offset). */
static void svc_feature5(struct gf4_ctx *c)
{
    struct gf3_service *s=gf3_service_get(c->ch);
    struct gf5 *g=&s->production->feature5;
    struct gf_init_context *init=c->legacy->init;
    struct gf_init_program *memory=0;
    void *allocation=0;
    u32 table=0,length=0,pc=0;
    u8 version=0;
    i32 source=gf_rom_bit_entry(init->rom,init->rom_size,'M',&table,&length,&version);
    i32 rc=source<0 ? GF5_EBIOS : 0;
    g->deadline=c->deadline;
    if (!source) {
        if (length<SVC_MEMORY_PC_BYTES) { source=GF_SCRIPT_EIO; rc=GF5_EBIOS; }
        else pc=(u32)init->rom[table]|(u32)init->rom[table+1]<<8;
    }
    if (!rc && pc>=init->rom_size && pc) {
        source=GF_SCRIPT_EIO; rc=GF5_EBIOS;
    }
    if (!rc && pc) {
        source=g->links.alloc(g->links.owner,sizeof(*memory),&allocation);
        if (source) rc=gf5_rc_map(source);
        else {
            memory=allocation;
            source=gf_init_script_prepare(init,memory,pc,0,0,c->deadline);
            if (source) rc=source==GF_SCRIPT_EIO ? GF5_EBIOS : gf5_rc_map(source);
        }
    }
    if (!rc) {
        /* acquire maps its public return but retains the original allocator
         * failure in source_rc. Do not replace that with the mapped value
         * when recording this optional F5 failure (PM decisions 8 and 22). */
        rc=gf5_rom_acquire(g,init->rom,init->rom_size,memory);
        if (rc) source=g->source_rc;
    }
    if (allocation) g->links.free(g->links.owner,allocation);
    if (rc) { g->last_rc=rc; g->source_rc=source; }
}

/* F4 keeps values only; F5 copies the full ROM and prepared M separately.
 * BMP signature: nvkm/subdev/bios/base.c:180-182. Limits: existing parser,
 * nvkm/subdev/bios/pll.c:162-175 (first entry fallback), :301-331. */
static i32 svc_rom(struct gf4_ctx *c)
{
    struct gf_display_context *l=c->legacy;
    i32 rc=l->owner->acquire(l);
    if (!rc && (!l->init || !l->program || !l->init->rom)) rc=GF4_EINVAL;
    if (!rc) {
        const u8 *rom=l->init->rom;
        u32 size=l->init->rom_size,bmp=0;
        static const u8 signature[]={0xff,0x7f,'N','V',0};
        for (u32 i=0;size>=sizeof(signature) && i<=size-sizeof(signature);++i) {
            u32 j=0;
            while (j<sizeof(signature) && rom[i+j]==signature[j]) ++j;
            if (j==sizeof(signature)) { bmp=i; break; }
        }
        rc=gf4_dcb_parse(rom,size,bmp,&c->dcb);
        for (u8 h=0;!rc && h<GF4_HEADS;++h) {
            rc=gfinit_pll_parse(rom,size,GF_INIT_PLL_TABLE,
                               h ? GF_PLL_VIDEO_B : GF_PLL_VIDEO,&c->limits[h]);
            c->limits_valid[h]=!rc;
            if (rc) rc=GF4_EPLL;
        }
        if (!rc) rc=gf_init_run(l->init,l->program);
        if (!rc) rc=gf_display_baseline(l);
        if (!rc) svc_feature5(c);
    }
    /* acquire may have allocated scratch even when it returns an error. */
    l->owner->release(l);
    l->init=0; l->program=0;
    if (!rc) {
        l->initialized=true;
        c->routes=c->dcb.routes; c->nroutes=c->dcb.nroutes;
    }
    return rc;
}

static struct gf3_surface svc_surface(const struct gf_vram_reservation_node *n,
                                      const struct gf4_mode *m)
{
    return (struct gf3_surface){
        .off=n->value.off,.bytes=n->value.bytes,.pitch=m->pitch,
        .width=m->t.hd,.height=m->t.vd,.fmt=m->fmt,
        .reservation_id=n->value.reservation_id,.generation=n->value.generation
    };
}

/* §3 keeps the existing VGA INDEX8 default even if EDID prefers another
 * timing. Initial mode loading uses the closed low-level restore path;
 * unlike set_mode it does not require a temporarily published service. */
static i32 svc_initial(struct gf4_ctx *c,u32 dl)
{
    const struct gf4_route *route=0;
    const struct gf4_mode *mode=0;
    for (u32 i=0;i<c->nroutes;++i) {
        const struct gf4_route *r=&c->routes[i];
        if (r->type==GF4_ANALOG && !r->location && (r->head_mask&1U) && r->or_mask==1U) {
            route=r; break;
        }
    }
    if (!route) return GF4_ENODEV;
    for (u32 i=0;i<c->state->nmodes;++i) {
        const struct gf4_mode *m=&c->state->modes[i];
        if (c->state->mode_connector[i]==route->connector && m->fmt==GF3_INDEX8 &&
            m->t.khz==GF_MODE_CLOCK_KHZ && m->t.hd==GF_MODE_WIDTH &&
            m->t.hs==GF_MODE_HSYNC_START && m->t.he==GF_MODE_HSYNC_END &&
            m->t.ht==GF_MODE_HTOTAL && m->t.vd==GF_MODE_HEIGHT &&
            m->t.vs==GF_MODE_VSYNC_START && m->t.ve==GF_MODE_VSYNC_END &&
            m->t.vt==GF_MODE_VTOTAL && m->t.flags==(GF4_NEG_H|GF4_NEG_V)) { mode=m; break; }
    }
    if (!mode) return GF4_ENODEV;
    struct gf4_saved *initial=&c->state->rollback;
    *initial=(struct gf4_saved){.version=GF4_SAVED_VERSION,.size=sizeof(*initial),
        .wanted_fifo=c->ch->f->wanted_fifo,.wanted_graph=c->ch->f->wanted_graph,
        .wanted_heads=1U};
    for (u8 h=0;h<GF4_HEADS;++h) {
        struct gf4_head_image *v=&initial->head[h];
        v->dpms=GF4_ON; v->cursor=c->cursor[h].value;
        /* crtc.c:778-834: identity 16-bit ramp, separately from INDEX8. */
        for (u32 rgb=0;rgb<3;++rgb) for (u32 i=0;i<256;++i)
            v->gamma[rgb][i]=(u16)(i*(U16_MAX/U8_MAX));
    }
    struct gf4_head_image *v=&initial->head[0];
    v->enabled=true; v->mode=*mode; v->route=*route; v->route.head=0;
    v->blank=c->legacy->blank;
    for (u32 i=0;i<sizeof(v->palette);++i) v->palette[i]=c->legacy->palette[i];
    v->front=svc_surface(&c->scan[0][0],mode);
    v->back=svc_surface(&c->scan[0][1],mode);
    struct gf3_surface test=svc_surface(&c->test,mode);
    i32 rc=gf3_service_surfaces(gf3_service_get(c->ch),&v->front,
                               c->test.value.owner ? &test : 0);
    if (!rc) rc=gf4_display_restore(c,initial,dl);
    return rc;
}

i32 gf4_service_start(struct gf4_ctx *c,u32 dl)
{
    struct gf3_service *s=0;
    i32 rc=svc_check(c,&s);
    if (rc) return svc_result(c,rc);
    struct gf_display_context *l=c->legacy;
    struct gf_foundation *f=&s->production->foundation;
    if (!c->data || !c->exec || !f->io.read || !f->io.write ||
        !f->io.timing.clock_us || !f->io.timing.wait_us ||
        !l->owner->acquire || !l->owner->release ||
        !l->owner->validate || !l->owner->foundation_start) return svc_result(c,GF4_EINVAL);
    if (c->ready || c->accel->ready || c->saved_closed || c->diagnostic_saved ||
        l->engaged || !c->reusable || !l->reusable ||
        (c->ch->state!=GF2_OFF && c->ch->state!=GF2_RUN)) return svc_result(c,GF4_ESTATE);
    c->deadline=dl; c->accepting=false; c->tv_sense_valid=false;
    rc=c->links->admission(c->links->owner,false);
    if (rc) return svc_result(c,rc);
    l->engaged=true; l->stop_ok=false;
    if (c->ch->state==GF2_OFF) rc=gf_master_enable(f);
    if (!rc) rc=l->initialized ? l->owner->validate(l) : svc_rom(c);
    if (!rc && c->ch->state==GF2_OFF) rc=l->owner->foundation_start(l);
    if (!rc && c->ch->state==GF2_OFF) rc=gf2_start(c->ch,f);
    if (!rc && (c->ch->state!=GF2_RUN || !c->ch->epoch || c->ch->fault.latched)) rc=GF4_ESTATE;
    if (!rc) {
        c->epoch=c->ch->epoch;
        rc=gf4_scan_reserve_initial(c,dl);
    }
    if (!rc) rc=gf3_service_objects_init(s,dl);
    if (!rc) {
        c->ready=true;
        rc=gf4_detect(c,dl);
    }
    i32 reason=c->last_rc; /* DDC fallback can succeed with a diagnostic. */
    if (!rc) rc=svc_initial(c,dl);
    if (!rc) {
        c->saved_closed=false;
        l->mode=c->state->committed[0];
        rc=c->links->admission(c->links->owner,true);
    }
    if (!rc) {
        c->accepting=true; l->active=true; l->stop_ok=false;
        (void)svc_result(c,reason);
        return 0;
    }
    /* The failed start is never public; cleanup errors are separately kept.
     * Keep both loans through any partial cleanup, including OFF failures. */
    c->recovery_rc=gf4_service_stop(c,dl);
    return svc_result(c,rc);
}

i32 gf4_service_stop(struct gf4_ctx *c,u32 dl)
{
    struct gf3_service *s=0;
    i32 first=svc_check(c,&s);
    if (first) return svc_result(c,first);
    struct gf_display_context *l=c->legacy;
    struct gf_foundation *f=&s->production->foundation;
    c->deadline=dl; c->accepting=false; l->active=false;
    first=c->links->admission(c->links->owner,false);
    /* nouveau_drm.c:955-988. All display waits precede F2/F1 stop. A held
     * flip that cannot converge retains both keys until display use ends. */
    if (c->ready && c->ch->state==GF2_RUN && !c->ch->fault.latched) {
        for (u8 h=0;h<GF4_HEADS;++h) {
            i32 rc=0;
            if (c->hold[h].pending) rc=gf4_flip_complete(c,h,dl);
            if (rc && !first) first=rc;
            if (!c->state->head[h].enabled) continue;
            rc=gf4_cursor_apply(c,h,false);
            if (rc && !first) first=rc;
            if (!c->saved_closed) {
                rc=svc_dpms_on(c,h,dl);
                if (rc && !first) first=rc;
            }
        }
    }
    i32 rc=gf3_service_close(s);
    bool closed=!rc;
    if (rc && !first) first=rc;
    if (rc && !f->revoked) return svc_result(c,first);
    if (closed) {
        rc=gf3_service_fini(s,dl);
        if (rc && !first) first=rc;
    }
    rc=gf2_stop(c->ch);
    if (rc && !first) first=rc;
    bool off=c->ch->state==GF2_OFF;
    if (off) { gf3_service_invalidate(s); c->ready=false; }
    else if (!first) first=GF2_ESTOP;
    if (!off && !f->revoked) return svc_result(c,first);
    rc=gf_display_quiesce(l);
    if (rc && !first) first=rc;
    if (!rc && off && closed) {
        /* X3: quiesce leaves ownership flags intact. This caller confirms
         * local cleanup, F2 OFF and closed windows before ending display use;
         * a later release failure must not make cleanup pending again. */
        l->engaged=false;
        rc=gf4_scan_release_all(c,dl);
        if (rc && !first) first=rc;
        if (!rc && !c->diagnostic_saved) {
            for (u8 h=0;h<GF4_HEADS;++h) {
                c->state->head[h]=(struct gf4_head_image){0};
                c->hold[h]=(struct gf4_flip_hold){0};
            }
            c->saved_closed=false;
            c->hold_saved.version=0; /* Normal stop ended the retained loan. */
            /* surfaces() requires ready F3. OFF invalidated the objects;
             * discard the now-ended value loans without reinitializing it. */
            s->scan=(struct gf3_surface){0};
            s->test=(struct gf3_surface){0};
            for (u8 h=0;h<GF4_HEADS;++h) c->scan_bank[h]=0;
        }
    }
    l->stop_ok=!first && !l->engaged;
    /* N1: display4, legacy, links and owner table survive until module exit. */
    return svc_result(c,first);
}

static i32 svc_owner_stop(void *owner,u32 dl)
{
    struct gf4_ctx *c=owner;
    struct gf3_service *s=0;
    i32 rc=svc_check(c,&s);
    if (rc) return svc_result(c,rc);
    if (!c->ready || c->epoch!=c->ch->epoch || c->ch->state==GF2_OFF)
        return svc_result(c,GF4_ESTATE);
    /* N2/N3: snapshot before suspend mutates DPMS/hold/IRQ. This is a value
     * copy in recover's resident storage, never a diagnostic save loan. */
    c->hold_saved=(struct gf4_saved){.version=GF4_SAVED_VERSION,.size=sizeof(c->hold_saved),
        .wanted_fifo=c->ch->f->wanted_fifo,.wanted_graph=c->ch->f->wanted_graph,
        .wanted_heads=c->ch->f->wanted_heads};
    for (u8 h=0;h<GF4_HEADS;++h) {
        c->hold_saved.head[h]=c->state->head[h];
        c->hold_saved.hold[h]=c->hold[h];
    }
    return svc_result(c,gf4_service_suspend(c,dl));
}

static i32 svc_owner_start(void *owner,u32 dl)
{
    struct gf4_ctx *c=owner;
    struct gf3_service *s=0;
    i32 rc=svc_check(c,&s);
    if (rc) return svc_result(c,rc);
    if (c->ready || c->ch->state!=GF2_OFF ||
        c->hold_saved.version!=GF4_SAVED_VERSION || c->hold_saved.size!=sizeof(c->hold_saved))
        return svc_result(c,GF4_ESTATE);
    bool saved_closed=c->saved_closed;
    rc=gf4_service_restart(c,dl);
    if (!rc) rc=gf4_display_restore(c,&c->hold_saved,dl);
    if (!rc) {
        c->saved_closed=saved_closed;
        if (!(saved_closed && !c->diagnostic_saved)) {
            rc=c->links->admission(c->links->owner,true);
            if (!rc) c->accepting=true;
        }
    }
    return svc_result(c,rc);
}

static i32 svc_owner_recover(void *owner,u32 dl)
{
    return gf4_service_recover(owner,dl);
}
