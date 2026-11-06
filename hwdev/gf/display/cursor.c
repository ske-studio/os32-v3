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
/* NV44A analog: MIT nouveau c03c1a1 dispnv04/crtc.c:245-606;
 * see docs/design/hwdev/gf/display/cursor.md for the assignment ownership. */
#include "calc.h"
void gf_mode_cursor(struct gfinit_mode *o)
{
    o->cr[GF_MODE_CR_CURSOR] = GF_MODE_CURSOR_HIDDEN;
    o->cursor_cfg = GF_MODE_CURSOR_CONFIG;
    o->cursync = GF_MODE_CURSYNC;
}

#ifndef HWDEV_MODULE
#include "api.h"
/* PM integrates this typedef into types.h; no 64-bit division here. */
typedef long long i64;

/* B6-a: nouveau c03c1a1 dispnv04/hw.h:341-349,372-385,
 * cursor.c:21-28; nvreg.h:129-139,257-264,301-303. */
#define CUR_CR_PORT (RECOVER_PRMCIO + RECOVER_CRTC)
#define CUR_ADDR0 0x30U
#define CUR_ADDR1 0x31U
#define CUR_ADDR2 0x2fU
#define CUR_ENABLE 1U
#define CUR_DBLSCAN 2U
#define CUR_POSITION 0x680300U
#define CUR_CONFIG 0x600810U
#define CUR_SIDE 64U
#define CUR_SMALL 32U
#define CUR_PIXEL_BYTES 4U
#define CUR_MIN (-32768L)
#define CUR_MAX 32767L

static i32 cursor_result(struct gf4_ctx *c,i32 rc)
{
    if (c) c->last_rc=rc;
    return rc;
}
static i32 cursor_context(struct gf4_ctx *c,u8 head,bool public)
{
    if (!c || !c->ch || !c->ch->f || !c->state || head>=GF4_HEADS ||
        !c->ch->f->io.read || !c->ch->f->io.write) return GF4_EINVAL;
    if (public && (!c->ready || !c->accepting || !c->reusable ||
        c->changing || c->epoch!=c->ch->epoch)) return GF4_ESTATE;
    return 0;
}
static struct gf4_head_image *cursor_head(struct gf4_ctx *c,u8 head)
{
    return &(c->changing ? c->state->next : c->state->head)[head];
}
static i32 cursor_read(struct gf4_ctx *c,u8 head,u32 off,u32 width,u32 *v)
{
    struct gf_io *io=&c->ch->f->io;
    return io->read(io->opaque,GF_MODE_REGISTER_BAR,
                    off+head*RECOVER_HEAD_STRIDE,width,v);
}
static i32 cursor_write(struct gf4_ctx *c,u8 head,u32 off,u32 width,u32 v)
{
    struct gf_io *io=&c->ch->f->io;
    return io->write(io->opaque,GF_MODE_REGISTER_BAR,
                     off+head*RECOVER_HEAD_STRIDE,width,v);
}
static i32 cursor_cr(struct gf4_ctx *c,u8 head,u32 index,u32 v)
{
    i32 rc=cursor_write(c,head,CUR_CR_PORT,1,index);
    return rc ? rc : cursor_write(c,head,CUR_CR_PORT+1U,1,v);
}
static i32 cursor_fix(struct gf4_ctx *c,u8 head)
{
    u32 pos;
    i32 rc=cursor_read(c,head,CUR_POSITION,4,&pos);
    return rc ? rc : cursor_write(c,head,CUR_POSITION,4,pos);
}
static i32 cursor_position(i32 x,i32 y,i32 hot_x,i32 hot_y,u32 *out)
{
    i64 px=(i64)x-hot_x,py=(i64)y-hot_y;
    if (px<CUR_MIN || px>CUR_MAX || py<CUR_MIN || py>CUR_MAX) return GF4_ERANGE;
    *out=((u32)(u16)py<<16)|(u16)px;
    return 0;
}
i32 gf4_cursor_apply(struct gf4_ctx *c,u8 head,bool visible)
{
    i32 rc=cursor_context(c,head,false);
    if (rc) return cursor_result(c,rc);
    struct gf4_head_image *s=cursor_head(c,head);
    if (visible && (!s->enabled || s->dpms!=GF4_ON || s->blank || !s->cursor_valid))
        return cursor_result(c,GF4_ESTATE);
    u32 ctl;
    rc=cursor_write(c,head,CUR_CR_PORT,1,CUR_ADDR1);
    if (!rc) rc=cursor_read(c,head,CUR_CR_PORT+1U,1,&ctl);
    if (!rc) rc=cursor_cr(c,head,CUR_ADDR1,visible ? ctl|CUR_ENABLE : ctl&~CUR_ENABLE);
    if (!rc) rc=cursor_fix(c,head);
    return cursor_result(c,rc);
}
int gf4_cursor_show(struct gf4_ctx *c,u8 head,bool show)
{
    i32 rc=cursor_context(c,head,true);
    if (rc) return cursor_result(c,rc);
    struct gf4_head_image *s=cursor_head(c,head);
    if (show && (!s->enabled || !s->cursor_valid)) return cursor_result(c,GF4_ESTATE);
    rc=gf4_cursor_apply(c,head,show && s->dpms==GF4_ON && !s->blank);
    if (!rc) s->cursor_visible=show;
    return cursor_result(c,rc);
}
int gf4_cursor_move(struct gf4_ctx *c,u8 head,i32 x,i32 y)
{
    u32 pos;
    i32 rc=cursor_context(c,head,true);
    if (rc) return cursor_result(c,rc);
    struct gf4_head_image *s=cursor_head(c,head);
    rc=cursor_position(x,y,s->hot_x,s->hot_y,&pos);
    if (!rc) rc=cursor_write(c,head,CUR_POSITION,4,pos);
    if (!rc) { s->x=x; s->y=y; }
    return cursor_result(c,rc);
}

/* B6-b: crtc.c:938-1015 (PM alpha correction and complete image),
 * cursor.c:39-62 (address registers), hw.h:341-349 (NV40 workaround),
 * nvreg.h:158-166 (64x64 PM config). 32x32 padding is GF4 design §6. */
static i32 cursor_input(const struct gf4_cursor *im,u32 *pos,const struct gf4_head_image *s)
{
    if (!im || !im->argb_pm) return GF4_EINVAL;
    if (!((im->w==CUR_SMALL && im->h==CUR_SMALL) ||
          (im->w==CUR_SIDE && im->h==CUR_SIDE))) return GF4_ERANGE;
    if ((im->pitch&(CUR_PIXEL_BYTES-1U)) || ((uptr)im->argb_pm&(CUR_PIXEL_BYTES-1U)))
        return GF4_EFORMAT;
    i64 row=(i64)im->w*CUR_PIXEL_BYTES;
    i64 span=(i64)(im->h-1U)*im->pitch+row;
    if (im->pitch<row || span>im->bytes ||
        (i64)(uptr)im->argb_pm+span-1>U32_MAX) return GF4_ERANGE;
    /* crtc.c:944-960 assumes PM input before applying alpha+1.
     * Validate the complete image before hiding or borrowing cursor VRAM. */
    for (u32 y=0;y<im->h;++y) {
        const u32 *src=(const u32 *)((const u8 *)im->argb_pm+y*im->pitch);
        for (u32 x=0;x<im->w;++x) {
            u32 pixel=src[x],alpha=pixel>>24;
            if (((pixel>>16)&0xffU)>alpha || ((pixel>>8)&0xffU)>alpha ||
                (pixel&0xffU)>alpha) return GF4_EFORMAT;
        }
    }
    return cursor_position(s->x,s->y,im->hot_x,im->hot_y,pos);
}
static i32 cursor_deadline(struct gf4_ctx *c,u32 dl)
{
    struct gf_io *io=&c->ch->f->io;
    u32 now;
    if (!io->timing.clock_us) return GF4_EINVAL;
    i32 rc=io->timing.clock_us(io->opaque,&now);
    return rc ? rc : ((i32)(now-dl)>=0 ? GF4_ETIME : 0);
}
static i32 cursor_offset(struct gf4_ctx *c,u8 head,u32 off,u32 flags,u32 pos)
{
    i32 rc=cursor_write(c,head,CUR_CONFIG,4,GF_MODE_CURSOR_CONFIG);
    if (!rc) rc=cursor_cr(c,head,CUR_ADDR0,0x80U|((off>>17)&0x7fU));
    if (!rc) rc=cursor_cr(c,head,CUR_ADDR1,(((off>>11)&0x3fU)<<2) |
                          ((flags&GF4_DBLSCAN) ? CUR_DBLSCAN : 0));
    if (!rc) rc=cursor_cr(c,head,CUR_ADDR2,off>>24);
    if (!rc) rc=cursor_fix(c,head);
    if (!rc) rc=cursor_write(c,head,CUR_POSITION,4,pos);
    return rc;
}
int gf4_cursor_image(struct gf4_ctx *c,u8 head,const struct gf4_cursor *im,u32 dl)
{
    u32 pos;
    i32 rc=cursor_context(c,head,true);
    if (rc) return cursor_result(c,rc);
    struct gf4_head_image *s=cursor_head(c,head);
    struct gf_vram_view *view=&c->cursor_view[head];
    struct gf_vram_reservation live;
    rc=cursor_input(im,&pos,s);
    if (rc) return cursor_result(c,rc);
    if (!s->enabled) return cursor_result(c,GF4_ESTATE);
    if (view->mapped) return cursor_result(c,GF4_EBUSY);
    if (!s->cursor.owner) return cursor_result(c,GF4_ESTATE);
    if (s->cursor.bytes<GF4_CURSOR_BYTES) return cursor_result(c,GF4_ENOSPC);
    if (s->cursor.off&(GF4_CURSOR_ALIGN-1U)) return cursor_result(c,GF4_ERANGE);
    rc=gf_vram_match(c->ch,s->cursor.owner,s->cursor.reservation_id,
                     s->cursor.generation,s->cursor.off,GF4_CURSOR_BYTES,&live);
    if (!rc) rc=cursor_deadline(c,dl);
    if (!rc) rc=gf4_cursor_apply(c,head,false);
    if (rc) return cursor_result(c,rc);
    volatile u8 *va;
    /* B6-c owns this per-call mapping and its close callback. */
    view->close=gf4_cursor_close; view->close_owner=c;
    rc=gf_vram_view_map(view,c->ch,s->cursor.off,GF4_CURSOR_BYTES,&va);
    if (rc) return cursor_result(c,rc);
    s->cursor_valid=false;
    volatile u32 *dst=(volatile u32 *)va;
    for (u32 y=0;y<CUR_SIDE;++y) {
        const u32 *src=y<im->h ? (const u32 *)((const u8 *)im->argb_pm+y*im->pitch) : NULL;
        for (u32 x=0;x<CUR_SIDE;++x) {
            u32 pixel=src && x<im->w ? src[x] : 0;
            u32 alpha=pixel>>24;
            if (alpha>0 && alpha<255) pixel=(pixel&0x00ffffffU)|((alpha+1U)<<24);
            dst[y*CUR_SIDE+x]=pixel;
        }
    }
    /* End CPU borrowing before the register sequence or re-show. */
    rc=gf4_cursor_close(c,view);
    if (!rc) rc=cursor_deadline(c,dl);
    if (!rc) rc=cursor_offset(c,head,s->cursor.off,s->mode.t.flags,pos);
    if (rc) return cursor_result(c,rc);
    s->cursor_valid=true;
    rc=gf4_cursor_apply(c,head,s->cursor_visible && s->dpms==GF4_ON && !s->blank);
    if (rc) {
        s->cursor_valid=false;
        (void)gf4_cursor_apply(c,head,false);
        return cursor_result(c,rc);
    }
    s->hot_x=im->hot_x; s->hot_y=im->hot_y;
    s->cursor_w=im->w; s->cursor_h=im->h;
    return cursor_result(c,0);
}

/* B6-c: V0 vram_view.h:100-119, nouveau_bo.c:669-689 (map lifetime).
 * UC posted-write drain follows the existing F2 push.c:79-81 sequence.
 * No VA is held separately from the caller-owned view. */
i32 gf4_cursor_close(void *owner,struct gf_vram_view *view)
{
    struct gf4_ctx *c=owner;
    if (!c || !view || (view!=&c->cursor_view[0] && view!=&c->cursor_view[1]))
        return cursor_result(c,GF4_EINVAL);
    /* F3 may already have closed it; never unmap an already closed view. */
    if (!view->mapped) return cursor_result(c,0);
    __asm__ volatile ("" ::: "memory");
    __sync_synchronize();
    (void)view->va[0];
    return cursor_result(c,gf_vram_view_unmap(view));
}

#endif /* !HWDEV_MODULE: image APIs use production-owned VRAM views. */
