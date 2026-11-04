/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
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
/* docs/design/hwdev/gf/display/modes.md: nouveau dispnv04/crtc.c:643 (mode setting;
 * OS32's mode enumeration and information services have no nouveau analogue). */
#include "../internal.h"

#define GF_DISPLAY_MODE_ID 1U
#define GF_DISPLAY_MODE_INDEX 0U
#define GF_DISPLAY_MODE_COUNT 1U

int gf_display_query(void *context, struct video_info *out)
{
    struct gf_display_context *ctx = context;
    if (out) *out = (struct video_info){0};
    if (!ctx || !out) return OS32_ERR_INVAL;
    *out = (struct video_info){
        .caps = ctx->active ? VIDEO_CAP_PALETTE | VIDEO_CAP_BLANK : 0,
        .mode_count = GF_DISPLAY_MODE_COUNT,
        .current_mode = ctx->active ? GF_DISPLAY_MODE_ID : VIDEO_MODE_NONE,
        .active = ctx->active
    };
    return 0;
}

int gf_display_mode_info(void *context, u32 index, struct video_mode *out)
{
    if (out) *out = (struct video_mode){0};
    if (!context || !out || index != GF_DISPLAY_MODE_INDEX) return OS32_ERR_INVAL;
    *out = (struct video_mode){GF_DISPLAY_MODE_ID, GF_MODE_WIDTH, GF_MODE_HEIGHT,
        GF_MODE_PITCH, GFX_FMT_PACKED8, VIDEO_RGB_BITS, GF_MODE_CLOCK_KHZ};
    return 0;
}

int gf_display_framebuffer(void *context, struct video_fb *out)
{
    struct gf_display_context *ctx = context;
    if (out) *out = (struct video_fb){0};
    if (!ctx || !out) return OS32_ERR_INVAL;
    *out = (struct video_fb){ctx->bars[GF_MODE_VRAM_BAR].base,
        GF_MODE_VRAM_BYTES, GF_MODE_PITCH, GFX_FMT_PACKED8};
    return 0;
}

int gf_display_start(void *context, u32 byte_offset)
{
    if (!context) return OS32_ERR_INVAL;
    return byte_offset == GF_MODE_VRAM_OFFSET ? 0 : OS32_ERR_NOSYS;
}

/* F4 connector lists replace DRM's allocated mode list (GF_FEAT4_DESIGN §3
 * and B0 addendum). PLL admission: MIT nouveau c03c1a1
 * dispnv04/crtc.c:129-151; CRTC fields: :245-413; pitch/base: :838-903.
 * The existing diagnostic entry points above retain their fixed mode. */
#include "api.h"

typedef unsigned long long u64; /* PM integrates this type in types.h. */
#define GF4_PITCH_ALIGN 64U
#define GF4_PITCH_Q_MAX 0xfffU /* CR13/19/42, crtc.c:887-901. */
#define GF4_HFIELD_MAX 0x1ffU /* CR00-05/2d, crtc.c:340-406. */
#define GF4_VFIELD_MAX 0xfffU /* CR06/07/25/41, same source. */
#define GF4_HBLANK_MAX 0x7fU /* Wrapped HBE, crtc.c:348-353,391. */
#define GF4_HSYNC_MAX 0x1fU /* Wrapped HRE, crtc.c:353. */
#define GF4_VBLANK_MAX 0xffU /* Wrapped VBE, crtc.c:379. */
#define GF4_VSYNC_MAX 0xfU /* Wrapped VRE, crtc.c:371. */
#define GF4_CONNECTORS 256U /* DCB connector is u8. */
#define GF4_DEPTHS 3U
#define GF4_EDID_BYTES 128U
#define GF4_NO_I2C 0xffU
static const struct gf4_timing gf4_default_timing = {
    25175,640,656,752,800,480,490,492,525,GF4_NEG_H | GF4_NEG_V
}; /* §3 fallback, standard VGA 640x480 at 60 Hz. */

static u32 mode_cpp(enum gf3_fmt fmt)
{
    switch (fmt) {
    case GF3_INDEX8: return 1;
    case GF3_RGB565: return 2;
    case GF3_XRGB8888: return 4;
    default: return 0;
    }
}

static bool timing_equal(const struct gf4_timing *a,const struct gf4_timing *b)
{
    return a->khz==b->khz && a->hd==b->hd && a->hs==b->hs &&
        a->he==b->he && a->ht==b->ht && a->vd==b->vd && a->vs==b->vs &&
        a->ve==b->ve && a->vt==b->vt && a->flags==b->flags;
}

static bool timing_fits(const struct gf4_timing *t,const struct gf4_edid_state *e)
{
    /* C:245-256 truncates horizontal pixels to character clocks and permits
     * vs==vd. Keep the original pixels in the published mode. */
    if (!t->khz || !t->hd || t->vd<3U ||
        t->hd>=t->hs || t->hs>=t->he || t->he>t->ht ||
        t->vd>t->vs || t->vs>=t->ve || t->ve>t->vt ||
        t->ht<40U || t->vt<3U ||
        (t->hs>>3)+1U>GF4_HFIELD_MAX ||
        (t->hd>>3)-1U>GF4_HFIELD_MAX || (t->ht>>3)-5U>GF4_HFIELD_MAX ||
        t->vd-1U>GF4_VFIELD_MAX || t->vs-1U>GF4_VFIELD_MAX ||
        ((t->vt-2U)|((t->flags&GF4_INTERLACE)?1U:0U))>GF4_VFIELD_MAX ||
        (u32)((t->ht>>3)-(t->hd>>3))>GF4_HBLANK_MAX ||
        (u32)((t->he>>3)-(t->hs>>3))>GF4_HSYNC_MAX ||
        (u32)(t->vt-t->vd)>GF4_VBLANK_MAX || (u32)(t->ve-t->vs)>GF4_VSYNC_MAX ||
        (t->flags & ~(GF4_NEG_H|GF4_NEG_V|GF4_INTERLACE|GF4_DBLSCAN))) return false;
    if (!e->range_valid) return true;
    u64 pixels=(u64)t->ht*t->vt, hz=(u64)t->khz*1000U;
    /* Normalized interlace fields scan twice per frame; doublescan scans
     * twice per source row. Recover source vertical refresh for EDID. */
    if (t->flags&GF4_INTERLACE) pixels*=2U;
    if (t->flags&GF4_DBLSCAN) hz*=2U;
    return (!e->max_khz || t->khz<=e->max_khz) &&
        (!e->min_h_khz || t->khz>=(u64)e->min_h_khz*t->ht) &&
        (!e->max_h_khz || t->khz<=(u64)e->max_h_khz*t->ht) &&
        (!e->min_v_hz || hz>=pixels*e->min_v_hz) &&
        (!e->max_v_hz || hz<=pixels*e->max_v_hz);
}

/* MIT nouveau c03c1a1 dispnv04/tvnv17.c:310-349 mode_valid;
 * norm order/kind/size/rate: tvmodesnv17.c:33-215. Interlaced CTV
 * dimensions here are field-normalized, as required by data.h. */
#define GF4_TV_CLOCK_MAX 70000U
#define GF4_CTV_CLOCK_MAX 400000U
static bool tv_timing_fits(const struct gf4_timing *t,u32 norm)
{
    static const struct { u16 width,height; u8 hz; bool ctv,interlace; } norms[]={
        {720,576,50,false,false}, {720,480,60,false,false},
        {720,576,50,false,false}, {720,576,50,false,false},
        {720,480,60,false,false}, {720,480,60,false,false},
        {720,480,60,false,false}, {720,480,0,true,false},
        {720,576,50,false,false}, {720,576,0,true,false},
        {1280,720,0,true,false}, {1920,540,0,true,true}
    };
    if (norm>=sizeof(norms)/sizeof(norms[0])) return false;
    if (norms[norm].ctv)
        return t->khz<=GF4_CTV_CLOCK_MAX && t->hd<=norms[norm].width &&
            t->vd<=norms[norm].height &&
            !!(t->flags&GF4_INTERLACE)==norms[norm].interlace &&
            !(t->flags&GF4_DBLSCAN);
    if (t->khz>GF4_TV_CLOCK_MAX || (t->flags&GF4_INTERLACE)) return false;
    /* tvnv17.c:334-339 compares rounded integer Hz against 50000/59940
     * mHz with tolerance 600. Only 50/60 Hz respectively survive. Compare
     * the half-open rounding interval without a 64-bit division. DBLSCAN
     * has already doubled vt, so do not divide the refresh a second time. */
    u64 pixels=(u64)t->ht*t->vt, twice_hz=(u64)t->khz*2000U;
    u32 hz=norms[norm].hz;
    return twice_hz>=pixels*(2U*hz-1U) && twice_hz<pixels*(2U*hz+1U);
}

static i32 candidate_pll(struct gf4_ctx *c,u8 connector,const struct gf4_mode *m)
{
    struct gfinit_mode_saved saved={0};
    struct gfinit_mode out;
    for (u32 r=0;r<c->nroutes;r++) {
        struct gf4_route route=c->routes[r];
        if (route.connector!=connector) continue;
        /* max_khz=0 means no encoder-specific ceiling (PM decision). */
        if (route.max_khz && m->t.khz>route.max_khz) continue;
        for (u32 h=0;h<GF4_HEADS;h++) {
            if (!(route.head_mask&(1U<<h)) || !c->limits_valid[h]) continue;
            route.head=h;
            i32 rc=gf4_pll_calc(m,&route,&saved,&c->limits[h],&out);
            if (!rc) return 0; /* Connector modes are the union of heads. */
            if (rc!=GF4_EPLL && rc!=GF4_ERANGE && rc!=GF4_EUNSUPPORTED) return rc;
        }
    }
    return GF4_EPLL;
}

static i32 connector_timings(struct gf4_ctx *c,u8 connector,u32 dl,
                             struct gf4_edid_state *e,i32 *reason)
{
    *e=(struct gf4_edid_state){.version=GF4_EDID_VERSION,.size=sizeof(*e)};
    u8 block[GF4_EDID_BYTES];
    const struct gf4_route *ddc=NULL;
    for (u32 r=0;r<c->nroutes;r++) {
        const struct gf4_route *route=&c->routes[r];
        if (route->connector!=connector) continue;
        if (route->type==GF4_TV) {
            if (!c->data->tv_modes) return GF4_EINVAL;
            return c->data->tv_modes(route->tv_norm,e);
        }
        struct gf4_i2c_port port;
        if (!ddc && route->i2c_index!=GF4_NO_I2C && c->links && c->links->i2c_port &&
            !c->links->i2c_port(c->links->owner,route->i2c_index,&port)) ddc=route;
    }
    if (!ddc) { *reason=GF4_ENODEV; return 0; }
    i32 rc=gf4_ddc_read(c,ddc->i2c_index,0,block,dl);
    if (!rc) rc=c->data->edid_modes(block,0,e);
    if (rc || !e->base_valid) {
        *reason=rc ? rc : GF4_EEDID;
        *e=(struct gf4_edid_state){.version=GF4_EDID_VERSION,.size=sizeof(*e)};
        return 0;
    }
    /* EDID extension count is one byte; same absolute deadline throughout. */
    u32 extensions=e->extensions;
    if (extensions>255U) return GF4_EEDID;
    for (u32 b=1;b<=extensions;b++) {
        struct gf4_edid_state next=*e;
        rc=gf4_ddc_read(c,ddc->i2c_index,(u8)b,block,dl);
        if (!rc) rc=c->data->edid_modes(block,b,&next);
        if (rc) { *reason=rc; break; } /* Preserve the valid base/prefix. */
        *e=next;
    }
    return 0;
}

/* Evict only surplus rows, from the end, when a later connector needs its
 * first surviving timing. Thus earlier connectors retain first claim on
 * surplus capacity without a second DDC probe or heap storage. */
static i32 mode_append(struct gf4_ctx *c,u8 connector,struct gf4_mode m,
                      bool required,bool pinned[GF4_MODES],u32 *n,bool *truncated)
{
    if (*n==GF4_MODES) {
        if (!required) { *truncated=true; return 0; }
        u32 at=*n;
        while (at && pinned[at-1U]) at--;
        if (!at) return GF4_ENOSPC;
        at--;
        for (u32 i=at;i+1U<*n;i++) {
            c->state->modes[i]=c->state->modes[i+1U];
            c->state->mode_connector[i]=c->state->mode_connector[i+1U];
            pinned[i]=pinned[i+1U];
        }
        --*n;
        *truncated=true;
    }
    if (!c->next_mode_id) return GF4_ENOSPC;
    m.id=c->next_mode_id++;
    c->state->modes[*n]=m;
    c->state->mode_connector[*n]=connector;
    pinned[*n]=required;
    ++*n;
    return 0;
}

i32 gf4_detect(struct gf4_ctx *c,u32 dl)
{
    if (!c || !c->state || !c->ch || !c->data || !c->data->edid_modes ||
        (c->nroutes && !c->routes) || c->nroutes>32U) return GF4_EINVAL;
    if (c->state->version!=GF4_STATE_VERSION || c->state->size!=sizeof(*c->state) ||
        c->data->version!=GF4_DATA_VERSION || c->data->size!=sizeof(*c->data) ||
        c->epoch!=c->ch->epoch) return GF4_ESTATE;
    if (c->accepting || c->changing || c->saved_closed) return GF4_EBUSY;
    for (u32 h=0;h<GF4_HEADS;h++)
        if (c->state->head[h].enabled || c->hold[h].pending) return GF4_EBUSY;
    c->state->nmodes=0;
    u32 n=0;
    bool pinned[GF4_MODES]={false},truncated=false;
    i32 reason=0,rc=0;
    for (u32 connector=0;connector<GF4_CONNECTORS;connector++) {
        bool present=false,tv=false,first=true;
        u32 tv_norm=0;
        for (u32 r=0;r<c->nroutes;r++) if (c->routes[r].connector==connector) {
            present=true;
            if (c->routes[r].type==GF4_TV) {
                tv=true; tv_norm=c->routes[r].tv_norm;
            }
        }
        if (!present) continue;
        struct gf4_edid_state e;
        rc=connector_timings(c,(u8)connector,dl,&e,&reason);
        if (rc) goto fail;
        if (e.ntiming>GF4_TIMINGS) { rc=GF4_EEDID; goto fail; }
        truncated|=e.truncated;
        if (e.last_rc) reason=e.last_rc;
        /* An invalid preferred index simply means no preferred timing. */
        u32 preferred=e.preferred<e.ntiming ? e.preferred : e.ntiming;
        for (u32 pass=0;pass<=e.ntiming;pass++) {
            const struct gf4_timing *t;
            if (pass==e.ntiming) {
                if (tv) break; /* TV norms supply their own allowed list. */
                t=&gf4_default_timing;
            } else {
                u32 at=pass;
                if (preferred<e.ntiming) {
                    at=pass ? pass-1U : preferred;
                    if (pass && at>=preferred) at++;
                }
                t=&e.timing[at];
            }
            if (tv ? !tv_timing_fits(t,tv_norm) : !timing_fits(t,&e)) continue;
            bool surviving=false;
            for (u32 depth=0;depth<GF4_DEPTHS;depth++) {
                static const enum gf3_fmt formats[GF4_DEPTHS]={
                    GF3_INDEX8,GF3_RGB565,GF3_XRGB8888
                };
                struct gf4_mode m={.t=*t,.fmt=formats[depth]};
                u32 cpp=mode_cpp(m.fmt);
                m.pitch=((u32)t->hd*cpp+GF4_PITCH_ALIGN-1U)&~(GF4_PITCH_ALIGN-1U);
                if (m.pitch/8U>GF4_PITCH_Q_MAX || m.pitch>c->ch->B/t->vd) continue;
                bool duplicate=false;
                for (u32 i=0;i<n;i++) if (c->state->mode_connector[i]==connector &&
                    c->state->modes[i].fmt==m.fmt && timing_equal(&c->state->modes[i].t,t))
                    duplicate=true;
                if (duplicate) continue;
                rc=candidate_pll(c,(u8)connector,&m);
                if (rc==GF4_EPLL) continue;
                if (rc) goto fail;
                surviving=true;
                rc=mode_append(c,(u8)connector,m,first,pinned,&n,&truncated);
                if (rc) goto fail;
            }
            if (surviving) first=false;
        }
        c->state->edid=e;
    }
    c->state->edid.truncated=truncated;
    c->state->nmodes=n; /* The only publication point. */
    c->last_rc=reason;
    return 0;
fail:
    c->last_rc=rc;
    return rc;
}

int gf4_modes(struct gf4_ctx *c,u8 connector,const struct gf4_mode **out,u32 *count)
{
    if (!c || !c->state || !c->ch || !out || !count) return GF4_EINVAL;
    if (!c->ready || c->epoch!=c->ch->epoch || c->state->version!=GF4_STATE_VERSION ||
        c->state->size!=sizeof(*c->state) || !c->state->nmodes) return GF4_ESTATE;
    u32 start=0,n=0;
    while (start<c->state->nmodes && c->state->mode_connector[start]!=connector) start++;
    while (start+n<c->state->nmodes && c->state->mode_connector[start+n]==connector) n++;
    if (!n) return GF4_ENODEV;
    *out=&c->state->modes[start];
    *count=n;
    return 0;
}

i32 gf4_surface_check(struct gf4_ctx *c,const struct gf3_surface *s,const struct gf4_mode *m)
{
    if (!c || !c->ch || !c->accel || !s || !m) return GF4_EINVAL;
    if (!c->ready || c->epoch!=c->ch->epoch) return GF4_ESTATE;
    struct gf3_format format;
    struct gf3_span span;
    i32 rc=gf3_surface_check(c->accel,s,(struct gf3_rect){0,0,s->width,s->height},
                             GF3_USE_BYTES,&format,&span);
    if (rc) return rc;
    u32 cpp=mode_cpp(s->fmt);
    if (!cpp || s->fmt!=m->fmt) return GF4_EFORMAT;
    const struct gf4_edid_state no_range={0};
    if (!timing_fits(&m->t,&no_range)) return GF4_ERANGE;
    u32 pitch=((u32)m->t.hd*cpp+GF4_PITCH_ALIGN-1U)&~(GF4_PITCH_ALIGN-1U);
    if (!s->width || !s->height || s->width!=m->t.hd || s->height!=m->t.vd ||
        s->pitch!=m->pitch || s->pitch!=pitch || (s->off&3U) || (s->pitch&(GF4_PITCH_ALIGN-1U)) ||
        s->pitch/8U>GF4_PITCH_Q_MAX || s->width>U32_MAX/cpp ||
        s->pitch<s->width*cpp || s->off>c->ch->B || s->bytes>c->ch->B-s->off) return GF4_ERANGE;
    /* Explicitly reject display of cursor/test storage, even with a live key. */
    for (u32 h=0;h<=GF4_HEADS;h++) {
        const struct gf_vram_reservation *v=h==GF4_HEADS ? &c->test.value : &c->cursor[h].value;
        if (v->owner && v->bytes && s->off<v->off+v->bytes && v->off<s->off+s->bytes)
            return GF4_EBUSY;
    }
    return 0;
}
