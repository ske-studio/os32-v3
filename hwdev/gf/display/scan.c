/*
 * Copyright 2007 Dave Airlied
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 * All Rights Reserved.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
/*
 * Authors: Dave Airlied <airlied@linux.ie>
 *	    Ben Skeggs   <darktama@iinet.net.au>
 *	    Jeremy Kolb  <jkolb@brandeis.edu>
 */

/* OS32 replaces BO pin/unpin (nouveau_bo.c:594-629) with resident V0
 * nodes. No node moves, HW writes, waits or publication during placement.
 * Scan geometry/identity follows dispnv04/crtc.c:838-903,1136-1217.
 * Fixed diagnostic geometry is the OS32 X1/N4 contract, not BO capacity. */
#ifdef GF_PRODUCTION_MODULE
#include "api.h"
#include "../accel_service.h"

#define SCAN_TEST_WIDTH 640U
#define SCAN_TEST_HEIGHT 480U
/* N4: 640 is already a multiple of F3's 64-byte pitch alignment. */
#define SCAN_TEST_PITCH 640U
#define SCAN_TEST_BYTES (SCAN_TEST_PITCH * SCAN_TEST_HEIGHT)

static i32 scan_result(struct gf4_ctx *c,i32 rc)
{
    if (c && rc) c->last_rc=rc;
    return rc;
}
static i32 scan_context(struct gf4_ctx *c)
{
    return !c || !c->state || !c->ch || !c->ch->f ? GF4_EINVAL : 0;
}
static struct gf_vram_reservation_node *scan_nodes(struct gf4_ctx *c,u8 head,u8 bank)
{
    return bank ? c->scan_next[head] : c->scan[head];
}
static bool scan_names(const struct gf3_surface *s,const struct gf_vram_reservation *v)
{
    return s->bytes && s->reservation_id==v->reservation_id &&
        s->generation==v->generation;
}
static bool scan_image_uses(const struct gf4_head_image *h,
                            const struct gf4_flip_hold *hold,
                            const struct gf_vram_reservation *v)
{
    return (h->enabled && scan_names(&h->front,v)) ||
        (hold->pending && (scan_names(&hold->old_front,v) || scan_names(&hold->new_front,v)));
}
static bool scan_used(struct gf4_ctx *c,const struct gf_vram_reservation *v)
{
    for (u8 h=0;h<GF4_HEADS;h++) {
        if (scan_image_uses(&c->state->head[h],&c->hold[h],v)) return true;
        if (c->changing && scan_image_uses(&c->state->next[h],&c->hold[h],v)) return true;
        if (c->diagnostic_saved &&
            (scan_names(&c->diagnostic_saved->head[h].front,v) ||
             scan_image_uses(&c->diagnostic_saved->head[h],&c->diagnostic_saved->hold[h],v) ||
             scan_names(&c->diagnostic_saved->head[h].back,v))) return true;
    }
    return false;
}
static struct gf3_surface scan_surface(const struct gf_vram_reservation_node *n,
                                       u32 pitch,u32 w,u32 h,enum gf3_fmt fmt)
{
    return (struct gf3_surface){.off=n->value.off,.bytes=n->value.bytes,
        .pitch=pitch,.width=w,.height=h,.fmt=fmt,
        .reservation_id=n->value.reservation_id,.generation=n->value.generation};
}
/* First page-aligned hole; enumerating does not mutate the registry.
 * V0 alone issues keys and detects identity exhaustion. Capacity failure is
 * distinguished before reserve, so test ID exhaustion is never nonfatal. */
static i32 scan_place(struct gf4_ctx *c,struct gf_vram_reservation_node *n,
                      u32 bytes,u32 *off)
{
    u32 mask=MEM_PAGE_SIZE-1U,start=*off;
    if (start>U32_MAX-mask) return GF4_ENOSPC;
    start=(start+mask)&~mask;
    for (;;) {
        if (start>c->ch->B || bytes>c->ch->B-start) return GF4_ENOSPC;
        u32 cursor=0,end=start+bytes,next=start;
        struct gf_vram_reservation v;
        i32 rc;
        while ((rc=gf_vram_next(c->ch,&cursor,&v))>0) {
            if (start<v.off+v.bytes && v.off<end && v.off+v.bytes>next)
                next=v.off+v.bytes;
        }
        if (rc<0) return rc;
        if (next==start) break;
        if (next>U32_MAX-mask) return GF4_ENOSPC;
        start=(next+mask)&~mask;
    }
    i32 rc=gf_vram_reserve(c->ch,n,start,bytes,n);
    if (!rc) *off=start;
    return rc;
}
static i32 scan_drop(struct gf4_ctx *c,struct gf_vram_reservation_node *n)
{
    if (!n->value.owner) return 0;
    if (n->value.owner!=n) return GF4_ESTATE;
    struct gf_vram_reservation old=n->value;
    i32 rc=gf_vram_release(c->ch,&old);
    if (!rc) for (u8 h=0;h<GF4_HEADS;h++)
        if (scan_names(&c->state->head[h].back,&old))
            c->state->head[h].back=(struct gf3_surface){0};
    return rc;
}
i32 gf4_scan_discard(struct gf4_ctx *c,u8 head,u32 dl)
{
    (void)dl; /* No waits: preserve the caller's absolute deadline. */
    i32 rc=scan_context(c);
    if (rc || head>=GF4_HEADS) return scan_result(c,GF4_EINVAL);
    if (c->changing || c->saved_closed) return scan_result(c,GF4_EBUSY);
    struct gf_vram_reservation_node *nodes=scan_nodes(c,head,c->scan_bank[head]^1U);
    for (u8 i=0;i<2;i++)
        if (nodes[i].value.owner && scan_used(c,&nodes[i].value))
            return scan_result(c,GF4_EBUSY);
    for (u8 i=0;!rc && i<2;i++) rc=scan_drop(c,&nodes[i]);
    return scan_result(c,rc);
}
i32 gf4_scan_prepare(struct gf4_ctx *c,u8 head,u32 mode_id,
                      struct gf3_surface out[2],u32 dl)
{
    i32 rc=scan_context(c);
    if (rc || !out || head>=GF4_HEADS) return scan_result(c,GF4_EINVAL);
    if (!c->ready || !c->reusable || c->epoch!=c->ch->epoch) return scan_result(c,GF4_ESTATE);
    if (c->changing || c->saved_closed || c->hold[head].pending) return scan_result(c,GF4_EBUSY);
    const struct gf4_mode *m=0;
    for (u32 i=0;i<c->state->nmodes;i++)
        if (c->state->modes[i].id==mode_id) m=&c->state->modes[i];
    if (!m) return scan_result(c,GF4_EINVAL);
    if (!m->pitch || !m->t.hd || !m->t.vd || m->t.vd>U32_MAX/m->pitch)
        return scan_result(c,GF4_ERANGE);
    struct gf_vram_reservation_node *nodes=scan_nodes(c,head,c->scan_bank[head]^1U);
    for (u8 i=0;i<2;i++)
        if (nodes[i].value.owner && scan_used(c,&nodes[i].value)) return scan_result(c,GF4_ENOSPC);
    rc=gf4_scan_discard(c,head,dl);
    struct gf3_surface result[2];
    /* Independent V0 windows must exclude the boot display's rounded pages. */
    u32 off=GF_MODE_VRAM_BYTES;
    for (u8 i=0;!rc && i<2;i++) {
        rc=scan_place(c,&nodes[i],m->pitch*m->t.vd,&off);
        if (!rc) {
            result[i]=scan_surface(&nodes[i],m->pitch,m->t.hd,m->t.vd,m->fmt);
            rc=gf4_surface_check(c,&result[i],m);
        }
    }
    if (rc) {
        /* New faces have never reached HW; failed releases retain their node. */
        i32 cleanup=gf4_scan_discard(c,head,dl);
        if (cleanup) c->recovery_rc=cleanup;
        return scan_result(c,rc);
    }
    out[0]=result[0]; out[1]=result[1];
    return 0;
}
i32 gf4_scan_reserve_initial(struct gf4_ctx *c,u32 dl)
{
    (void)dl;
    i32 rc=scan_context(c);
    if (rc) return scan_result(c,rc);
    rc=gf_vram_registry_init(c->ch);
    if (rc) return scan_result(c,rc);
    struct gf_vram_reservation_node *n=&c->scan[0][0];
    if (!n->value.owner)
        rc=gf_vram_reserve(c->ch,n,GF_MODE_VRAM_OFFSET,GF_MODE_VRAM_BYTES,n);
    else {
        struct gf_vram_reservation v;
        rc=gf_vram_match(c->ch,n,n->value.reservation_id,n->value.generation,
                         GF_MODE_VRAM_OFFSET,GF_MODE_VRAM_BYTES,&v);
    }
    if (rc) return scan_result(c,rc);
    c->state->head[0].front=scan_surface(n,GF_MODE_PITCH,GF_MODE_WIDTH,GF_MODE_HEIGHT,GF3_INDEX8);
    /* §2: scan first, then each head's cursor, then the fixed test face.
     * 4KiB alignment also satisfies crtc.c:938-975's 2KiB cursor address. */
    u32 off=GF_MODE_VRAM_BYTES;
    for (u8 h=0;!rc && h<GF4_HEADS;h++) {
        n=&c->cursor[h];
        if (!n->value.owner) rc=scan_place(c,n,GF4_CURSOR_BYTES,&off);
        else {
            struct gf_vram_reservation v;
            rc=gf_vram_match(c->ch,n,n->value.reservation_id,n->value.generation,
                             n->value.off,n->value.bytes,&v);
        }
        if (!rc) c->state->head[h].cursor=n->value;
    }
    if (rc) return scan_result(c,rc);
    if (!c->test.value.owner) {
        rc=scan_place(c,&c->test,SCAN_TEST_BYTES,&off);
        if (rc==GF4_ENOSPC) { c->last_rc=GF2_ENOSPC; return 0; }
    }
    if (!rc && c->test.value.owner) {
        struct gf_vram_reservation v;
        rc=gf_vram_match(c->ch,&c->test,c->test.value.reservation_id,
            c->test.value.generation,c->test.value.off,SCAN_TEST_BYTES,&v);
    }
    return scan_result(c,rc);
}
i32 gf4_scan_changed(void *owner,u8 head,const struct gf3_surface *front)
{
    struct gf4_ctx *c=owner;
    i32 rc=scan_context(c);
    if (rc || !front || head>=GF4_HEADS) return scan_result(c,GF4_EINVAL);
    if (!c->ready || !c->reusable || c->epoch!=c->ch->epoch) return scan_result(c,GF4_ESTATE);
    struct gf4_head_image *image=c->changing ? c->state->next : c->state->head;
    struct gf_vram_reservation v;
    rc=gf_vram_match(c->ch,0,front->reservation_id,front->generation,front->off,front->bytes,&v);
    if (!rc) rc=gf_vram_match(c->ch,v.owner,front->reservation_id,front->generation,
                              front->off,front->bytes,&v);
    if (!rc) rc=gf4_surface_check(c,front,&image[head].mode);
    if (rc) return scan_result(c,rc);
    if (!head) {
        struct gf3_service *s=gf3_service_get(c->ch);
        if (!s) return scan_result(c,GF4_ESTATE);
        struct gf3_surface test;
        const struct gf3_surface *borrow=0;
        if (c->test.value.owner) {
            rc=gf_vram_match(c->ch,&c->test,c->test.value.reservation_id,
                c->test.value.generation,c->test.value.off,SCAN_TEST_BYTES,&v);
            if (rc) return scan_result(c,rc);
            test=scan_surface(&c->test,SCAN_TEST_PITCH,SCAN_TEST_WIDTH,SCAN_TEST_HEIGHT,GF3_INDEX8);
            borrow=&test;
        }
        rc=gf3_service_surfaces(s,front,borrow);
        if (rc) return scan_result(c,rc);
    }
    /* B7 still owns old front and pending here. Never release either face.
     * crtc.c:1074-1087 commits only after the completion notification. */
    if (!scan_names(front,&(struct gf_vram_reservation){
            .reservation_id=image[head].front.reservation_id,
            .generation=image[head].front.generation})) image[head].back=image[head].front;
    for (u8 bank=0;bank<2;bank++) {
        struct gf_vram_reservation_node *nodes=scan_nodes(c,head,bank);
        for (u8 i=0;i<2;i++) if (nodes[i].value.owner==&nodes[i] && scan_names(front,&nodes[i].value))
            c->scan_bank[head]=bank;
    }
    if (!head && c->accepting && !c->changing && !c->saved_closed)
        gf_hook_scan_changed(head,front,image[head].mode.id);
    return 0;
}
i32 gf4_scan_release_all(struct gf4_ctx *c,u32 dl)
{
    (void)dl;
    i32 rc=scan_context(c);
    if (rc) return scan_result(c,rc);
    if (c->ch->state!=GF2_OFF || c->accepting || c->changing || c->saved_closed || c->diagnostic_saved)
        return scan_result(c,GF4_EBUSY);
    for (u8 h=0;h<GF4_HEADS;h++)
        if (c->cursor_view[h].mapped) return scan_result(c,GF4_EBUSY);
    rc=scan_drop(c,&c->test);
    for (u8 h=0;!rc && h<GF4_HEADS;h++) {
        rc=scan_drop(c,&c->cursor[h]);
        if (!rc) c->state->head[h].cursor=(struct gf_vram_reservation){0};
        for (u8 bank=0;!rc && bank<2;bank++) {
            struct gf_vram_reservation_node *nodes=scan_nodes(c,h,bank);
            for (u8 i=0;!rc && i<2;i++) rc=scan_drop(c,&nodes[i]);
        }
        if (!rc) {
            c->state->head[h].front=c->state->head[h].back=(struct gf3_surface){0};
            c->hold[h]=(struct gf4_flip_hold){0}; c->scan_bank[h]=0;
        }
    }
    return scan_result(c,rc);
}
#endif
