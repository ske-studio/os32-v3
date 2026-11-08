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
#include "stage.h"
#include "probes.h"
#include "../../../include/kprintf.h"
#include "../internal.h"
#include "../accel_service.h"
#include "../production.h"
#include "../../../drivers/video/geforce/geforce_module.h"
#define GF_ROM_BIT_ONLY
#include "../rom.inc"
#undef GF_ROM_BIT_ONLY

static struct probe_slot detect_pre, mode_post;
static const struct probe_decl probe_decls[] = {
    {"gf4.detect.pre", &detect_pre},
    {"gf4.mode.post", &mode_post}
};
i32 gf4_get_probe_decls(const struct probe_decl **table, u32 *count)
{
    *table = probe_decls;
    *count = sizeof(probe_decls) / sizeof(probe_decls[0]);
    return 0;
}

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

/* The stopped management loan belongs to this module, independently of
 * links.c's live admission table. dispnv04/disp.c:189-207 stops display
 * use before destroying its saved state; failed releases remain retryable. */
static i32 svc_management_last_rc(struct gf4_ctx *c)
{
    return c ? c->last_rc : GF4_EINVAL;
}
static struct gf4_restore_ops svc_management={
    .restore=gf4_diagnostic_restore,.last_rc=svc_management_last_rc,
    .saved_release=gf4_display_saved_release,.abandon=gf4_diagnostic_abandon
};

void gf4_service_cleanup_reset(struct gf4_ctx *c)
{
    struct gf3_service *s=gf3_service_get(c->ch);
    for (u8 h=0;h<GF4_HEADS;++h) {
        c->state->head[h]=(struct gf4_head_image){0};
        c->hold[h]=(struct gf4_flip_hold){0};
        c->scan_bank[h]=0;
    }
    c->saved_closed=false;
    c->hold_saved.version=0;
    /* OFF invalidated F3; end its value loans without reopening objects. */
    s->scan=(struct gf3_surface){0};
    s->test=(struct gf3_surface){0};
    if (geforce_context && geforce_context->display_restore_out &&
        *geforce_context->display_restore_out &&
        (*geforce_context->display_restore_out)->ctx==c)
        *geforce_context->display_restore_out=0;
    c->cleanup_pending=false;
    c->legacy->stop_ok=true;
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
    c->memory_prepare_attempted=false; c->memory_prepare_rc=0;
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
            c->memory_prepare_attempted=true; c->memory_prepare_rc=source;
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
static i32 svc_rom(struct gf4_ctx *c,struct gf_foundation *f,const char **stage)
{
    struct gf_display_context *l=c->legacy;
    *stage="rom_acquire";
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
        *stage="dcb_parse";
        rc=gf4_dcb_parse(rom,size,bmp,&c->dcb);
        for (u8 h=0;!rc && h<GF4_HEADS;++h) {
            *stage="pll_parse";
            rc=gfinit_pll_parse(rom,size,GF_INIT_PLL_TABLE,
                               h ? GF_PLL_VIDEO_B : GF_PLL_VIDEO,&c->limits[h]);
            c->limits_valid[h]=!rc;
            if (rc) rc=GF4_EPLL;
        }
        if (!rc) { *stage="devinit"; rc=gf_init_run(l->init,l->program); }
        if (!rc) { *stage="baseline"; rc=gf_display_baseline(l); }
        if (!rc) {
            *stage="feature5_deadline";
            rc=gf4_stage_deadline(c,f,&c->deadline);
        }
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
static i32 svc_initial(struct gf4_ctx *c,u32 dl,const char **stage)
{
    const struct gf4_route *route=0;
    const struct gf4_mode *mode=0;
    *stage="initial_mode";
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
    *stage="surfaces";
    i32 rc=gf3_service_surfaces(gf3_service_get(c->ch),&v->front,
                               c->test.value.owner ? &test : 0);
    if (!rc) {
        rc=gf4_display_restore_stage(c,initial,dl,stage);
        /* Initial mode only; the shared restore path has no probe. */
        if (!rc) PROBE_HIT(mode_post);
    }
    return rc;
}

/* Log before stop: F2 OFF and zero status fields seen afterwards describe
 * cleanup, not evidence that the channel never reached RUN. */
static i32 svc_start_failure(struct gf4_ctx *c,const char *stage,i32 rc)
{
    kprintf(KLOG_ATTR,"[gf4] start fail stage=%s rc=%d\n",stage,rc);
    return svc_result(c,rc);
}

/* Read-only evidence before detect/initial mode. NV04 pad acquisition has
 * no hardware callback (MIT nouveau i2c/padnv04.c:27-35). Observe the actual
 * DCB CR pair as well as the legacy pairs; never unlock or reroute here.
 * PMC bit0: devinit/nv04.c:406-412; ENGINE_CTRL bit4: crtc.c:496-499. */
#define SVC_ENGINE_CTRL 0x600860U
#define SVC_DDC_SENSE0 0x3eU
#define SVC_DDC_DRIVE0 0x3fU
#define SVC_DDC_SENSE1 0x36U
#define SVC_DDC_DRIVE1 0x37U
static void svc_detect_diagnostic(struct gf4_ctx *c,struct gf5 *g)
{
    struct gf_io *io=&c->ch->f->io;
    u32 index=0,cr1f=U32_MAX,cr44=U32_MAX,cr11=U32_MAX;
    u32 cr3e=U32_MAX,cr3f=U32_MAX,cr36=U32_MAX,cr37=U32_MAX;
    u32 pmc=U32_MAX,engine[GF4_HEADS]={U32_MAX,U32_MAX};
    i32 rc=io->read(io->opaque,GF_MODE_REGISTER_BAR,SVC_CR,1,&index);
    if (!rc) {
        rc=svc_index(io,SVC_CR,RECOVER_CR1F,&cr1f,false);
        if (!rc) rc=svc_index(io,SVC_CR,RECOVER_CR44,&cr44,false);
        if (!rc) rc=svc_index(io,SVC_CR,RECOVER_CR11,&cr11,false);
        if (!rc) rc=svc_index(io,SVC_CR,SVC_DDC_SENSE0,&cr3e,false);
        if (!rc) rc=svc_index(io,SVC_CR,SVC_DDC_DRIVE0,&cr3f,false);
        if (!rc) rc=svc_index(io,SVC_CR,SVC_DDC_SENSE1,&cr36,false);
        if (!rc) rc=svc_index(io,SVC_CR,SVC_DDC_DRIVE1,&cr37,false);
        i32 restore=io->write(io->opaque,GF_MODE_REGISTER_BAR,SVC_CR,1,index);
        if (!rc) rc=restore;
    }
    i32 mmio_rc=io->read(io->opaque,GF_MODE_REGISTER_BAR,RECOVER_PMC_ENABLE,4,&pmc);
    for (u32 h=0;!mmio_rc && h<GF4_HEADS;++h)
        mmio_rc=io->read(io->opaque,GF_MODE_REGISTER_BAR,
                         SVC_ENGINE_CTRL+h*RECOVER_HEAD_STRIDE,4,&engine[h]);
    kprintf(KLOG_ATTR,"[gf4] detect state cr1f=%x cr44=%x read_rc=%d m_attempted=%u m_rc=%d f5_rc=%d source_rc=%d cr11=%x cr3e=%x cr3f=%x cr36=%x cr37=%x pmc=%x ec0=%x ec1=%x mmio_rc=%d\n",
            cr1f,cr44,rc,c->memory_prepare_attempted,c->memory_prepare_rc,
            g->last_rc,g->source_rc,cr11,cr3e,cr3f,cr36,cr37,pmc,
            engine[0],engine[1],mmio_rc);
    for (u32 i=0;i<c->nroutes;++i) {
        const struct gf4_route *r=&c->routes[i];
        if (r->type==GF4_TV || r->i2c_index==U8_MAX) continue;
        u32 bus=r->i2c_index,drive=U32_MAX,sense=U32_MAX;
        struct gf4_i2c_port port={0};
        /* Inspect the same value loan as links_i2c_port, without changing
         * last_rc or invoking a transfer. Non-NV04 entries retain their rc. */
        i32 port_rc=bus<sizeof(c->dcb.i2c)/sizeof(c->dcb.i2c[0]) ?
                    c->dcb.i2c_rc[bus] : GF4_ENODEV;
        i32 read_rc=0;
        if (!port_rc) {
            port=c->dcb.i2c[bus];
            read_rc=io->read(io->opaque,GF_MODE_REGISTER_BAR,SVC_CR,1,&index);
            if (!read_rc) {
                read_rc=svc_index(io,SVC_CR,port.drive,&drive,false);
                if (!read_rc) read_rc=svc_index(io,SVC_CR,port.sense,&sense,false);
                i32 restore=io->write(io->opaque,GF_MODE_REGISTER_BAR,SVC_CR,1,index);
                if (!read_rc) read_rc=restore;
            }
        }
        kprintf(KLOG_ATTR,"[gf4] detect port conn=%u bus=%u port_rc=%d drive_cr=%x drive=%x sense_cr=%x sense=%x read_rc=%d\n",
                r->connector,bus,port_rc,port.drive,drive,port.sense,sense,read_rc);
    }
}

/* PC-98 has no VGA BIOS POST to select I2C before the first mode load.
 * MIT nouveau dispnv04/crtc.c:496-499, hw.c:674-680 select primary head A;
 * modeset.c uses the same GF_MODE_ENGINE_I2C value (POLICY_PCIDEV §6).
 * devinit/base.c:94,117 keeps extended CR unlocked until device destruction.
 * OS32 restores local owner state at leave, so renew the unlock each enter. */
static i32 svc_detect_prepare(struct gf_foundation *f)
{
    struct gf_io *io=&f->io;
    u32 unlock=RECOVER_CR1F_UNLOCK,engine;
    i32 rc=svc_index(io,SVC_CR,RECOVER_CR1F,&unlock,true);
    if (!rc) rc=io->read(io->opaque,0,SVC_ENGINE_CTRL,4,&engine);
    if (!rc) rc=io->write(io->opaque,0,SVC_ENGINE_CTRL,4,engine|GF_MODE_ENGINE_I2C);
    return rc;
}

/* Diagnostic only: compare DDC with the signal-producing initial mode.
 * Keep detect's modes, EDID and result; each probe gets its own budget. */
static void svc_probe_after_mode(struct gf4_ctx *c)
{
    i32 saved_rc=c->last_rc;
    u32 saved_deadline=c->deadline;
    for (u8 connector=0;connector<2;++connector) {
        for (u32 i=0;i<c->nroutes;++i) {
            const struct gf4_route *r=&c->routes[i];
            if (r->connector!=connector || r->type==GF4_TV ||
                r->i2c_index==U8_MAX) continue;
            u32 dl;
            i32 rc=gf4_stage_deadline(c,c->ch->f,&dl);
            if (!rc) rc=gf4_ddc_probe(c,r->i2c_index,dl);
            kprintf(KLOG_ATTR,"[gf4] probe-after-mode conn=%u rc=%d\n",connector,rc);
            break;
        }
    }
    c->last_rc=saved_rc;
    c->deadline=saved_deadline;
}

i32 gf4_service_start(struct gf4_ctx *c,u32 dl)
{
    const char *stage="service_check";
    struct gf3_service *s=0;
    i32 rc=svc_check(c,&s);
    if (rc) return svc_start_failure(c,stage,rc);
    struct gf5 *g=&s->production->feature5;
    if (gf5_stopping(g)) return svc_start_failure(c,"thermal_stop",GF4_ESTATE);
    struct gf_display_context *l=c->legacy;
    struct gf_foundation *f=&s->production->foundation;
    if (!c->data || !c->exec || !f->io.read || !f->io.write ||
        !f->io.timing.clock_us || !f->io.timing.wait_us ||
        !l->owner->acquire || !l->owner->release ||
        !l->owner->validate || !l->owner->foundation_start)
        return svc_start_failure(c,"service_ops",GF4_EINVAL);
    if (c->cleanup_pending)
        return svc_start_failure(c,"cleanup_pending",GF4_EBUSY);
    if (c->ready || c->accel->ready || c->saved_closed || c->diagnostic_saved ||
        l->engaged || !c->reusable || !l->reusable ||
        (c->ch->state!=GF2_OFF && c->ch->state!=GF2_RUN))
        return svc_start_failure(c,"service_state",GF4_ESTATE);
    gf5_plan_invalidate(g); /* Display start boundary, before admission/MMIO. */
    c->deadline=dl; c->accepting=false; c->tv_sense_valid=false;
    stage="admission_close";
    rc=c->links->admission(c->links->owner,false);
    if (rc) return svc_start_failure(c,stage,rc);
    l->engaged=true; l->stop_ok=false;
    if (c->ch->state==GF2_OFF) {
        stage="master_enable"; rc=gf_master_enable(f);
    }
    if (!rc) {
        stage="rom_validate";
        rc=l->initialized ? l->owner->validate(l) : svc_rom(c,f,&stage);
    }
    if (!rc && c->ch->state==GF2_OFF) {
        stage="foundation_start"; rc=l->owner->foundation_start(l);
    }
    if (!rc && c->ch->state==GF2_OFF) {
        /* A cold channel has no foundation until gf2_start. Lend the
         * already started F1 here so tile init uses the same real I/O. */
        c->ch->f=f;
        stage="tiles_init"; rc=gf5_tiles_init(&s->production->feature5);
        if (!rc) { stage="channel_start"; rc=gf2_start(c->ch,f); }
    }
    if (!rc) {
        stage="channel_state";
        if (c->ch->state!=GF2_RUN || !c->ch->epoch || c->ch->fault.latched) rc=GF4_ESTATE;
    }
    if (!rc) {
        c->epoch=c->ch->epoch;
        /* nouveau c03c1a1 therm/base.c:343-357, clk/base.c:591-598:
         * work belongs to one device lifetime. A new F2 epoch cannot own
         * the previous channel's pause or count its cooling samples.
         * Keep suppression ceilings and pending shutdown across leave/enter;
         * neither a display restart nor a saved image proves cooling. */
        if (g->heat.epoch!=c->epoch) {
            g->heat.critical_paused=false;
            g->heat.cool_samples=0;
            g->heat.epoch=c->epoch;
        }
        stage="scan_reserve"; rc=gf4_scan_reserve_initial(c,dl);
    }
    if (!rc) {
        stage="objects_init";
        rc=gf4_stage_deadline(c,f,&dl);
        if (!rc) rc=gf3_service_objects_init(s,dl);
    }
    if (!rc) {
        stage="detect_prepare";
        rc=svc_detect_prepare(f);
    }
    if (!rc) {
        c->ready=true;
        stage="detect";
        PROBE_HIT(detect_pre);
        svc_detect_diagnostic(c,g);
        rc=gf4_stage_deadline(c,f,&dl);
        if (!rc) rc=gf4_detect(c,dl);
    }
    i32 reason=c->last_rc; /* DDC fallback can succeed with a diagnostic. */
    if (!rc) {
        stage="initial_deadline";
        rc=gf4_stage_deadline(c,f,&dl);
        if (!rc) rc=svc_initial(c,dl,&stage);
        if (!rc) svc_probe_after_mode(c);
    }
    if (!rc && g->heat.down_active) {
        struct gf5_clock current;
        stage="heat_ceiling";
        rc=gf5_clock_read(g,&current);
        if (!rc && gf5_above_ceiling(g,&current)) rc=GF5_EBUSY;
    }
    if (!rc) {
        c->saved_closed=false;
        l->mode=c->state->committed[0];
        stage="admission_open"; rc=c->links->admission(c->links->owner,true);
    }
    if (!rc) {
        c->accepting=true; l->active=true; l->stop_ok=false;
        if (s->production->feature5.phase==GF5_RUN)
            s->production->feature5.accepting=true;
        (void)svc_result(c,reason);
        return 0;
    }
    (void)svc_start_failure(c,stage,rc);
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
    struct gf5 *g=&s->production->feature5;
    g->accepting=false;
    gf5_plan_invalidate(g); /* Display stop boundary, including failed stop. */
    /* R2-2: leave terminates the F5 snapshot even after failed save/restore.
     * free owns the single allocation; never copy or erase tile loan nodes. */
    if (g->image) gf5_state_free(g,g->image);
    g->busy=false;
    g->phase=GF5_FAULT;
    first=c->links->admission(c->links->owner,false);
    if (geforce_context && geforce_context->display_restore_out) {
        svc_management.ctx=c;
        *geforce_context->display_restore_out=&svc_management;
    }
    /* nouveau_drm.c:955-988. All display waits precede F2/F1 stop. A held
     * flip that cannot converge retains both keys until display use ends. */
    if (c->ch->state==GF2_OFF && !l->engaged && l->stop_ok && !c->cleanup_pending) {
        gf4_service_cleanup_reset(c); /* End the management loan without MMIO. */
        return svc_result(c,first);
    }
    c->cleanup_pending=true;
    bool running=c->ready && c->ch->state==GF2_RUN && !c->ch->fault.latched;
    for (u8 h=0;h<GF4_HEADS;++h) {
        i32 rc=0;
        if (running && c->hold[h].pending) {
            rc=gf4_stage_deadline(c,f,&dl);
            if (!rc) rc=gf4_flip_complete(c,h,dl);
            if (rc && !first) first=rc;
        }
        /* Cursor MMIO does not need a live push channel. Hide even after
         * failed recovery stopped F2; the owner bus still rejects revocation. */
        rc=gf4_cursor_apply(c,h,false);
        if (rc && !first) first=rc;
        if (running && c->state->head[h].enabled && !c->saved_closed) {
            rc=gf4_stage_deadline(c,f,&dl);
            if (!rc) rc=svc_dpms_on(c,h,dl);
            if (rc && !first) first=rc;
        }
    }
    i32 rc=gf3_service_close(s);
    bool closed=!rc;
    if (rc && !first) first=rc;
    if (rc && !f->revoked) return svc_result(c,first);
    if (closed) {
        rc=gf4_stage_deadline(c,f,&dl);
        if (!rc) rc=gf3_service_fini(s,dl);
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
        /* X3: only quiesce + OFF + closed windows ends display use.
         * Pending survives retire/release failures and diagnostic loans. */
        l->engaged=false;
        /* ROM/master failure can stop before the first gf2_start bound F1.
         * The proven stopped context still needs F1 I/O for tile retirement. */
        c->ch->f=f;
        if (!c->diagnostic_saved) c->saved_closed=false;
        rc=gf5_tiles_retire(g);
        if (!rc && !c->diagnostic_saved) rc=gf4_scan_release_all(c,dl);
        if (rc && !first) first=rc;
        if (!rc && !c->diagnostic_saved) gf4_service_cleanup_reset(c);
    }
    l->stop_ok=!first && !l->engaged && !c->cleanup_pending;
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
    if (!rc) rc=gf4_stage_deadline(c,&s->production->foundation,&dl);
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
