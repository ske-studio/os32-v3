/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 * Copyright 2011 Red Hat Inc.
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
 */
/* C2-links-a, GF_FEAT4_DESIGN X1/X2. MIT nouveau c03c1a1:
 * nvkm/subdev/bios/i2c.c:64-76,120-155 (already parsed values),
 * nvkm/subdev/gpio/base.c:82-96 and gpio/nv10.c:55-88 (logical drive),
 * nvkm/engine/fifo/uchan.c:272-298 (block/allow).
 * Module slot publication and reservation loans are OS32 boundaries.
 * No ROM interpreter or TMDS script substitute belongs to this stage. */
#ifdef GF_PRODUCTION_MODULE
#include "api.h"
#include "../accel_service.h"
#include "../production.h"
#include "../../../drivers/video/geforce/geforce_module.h"

static struct gf4_links display_links;
static struct gf4_test_ops display_ops;
static struct gf4_restore_ops restore_ops;

static i32 links_result(struct gf4_ctx *c,i32 rc)
{
    if (c) c->last_rc=rc;
    return rc;
}
static int links_last_rc(struct gf4_ctx *c)
{
    return c ? c->last_rc : GF4_EINVAL;
}
static i32 links_i2c_port(void *owner,u8 bus,struct gf4_i2c_port *out)
{
    struct gf4_ctx *c=owner;
    if (!c || !out) return links_result(c,GF4_EINVAL);
    if (bus>=sizeof(c->dcb.i2c)/sizeof(c->dcb.i2c[0]))
        return links_result(c,GF4_ENODEV);
    i32 rc=c->dcb.i2c_rc[bus];
    if (rc) return links_result(c,rc);
    *out=c->dcb.i2c[bus];
    return 0;
}
static i32 links_admission(void *owner,bool open)
{
    struct gf4_ctx *c=owner;
    if (!c || !c->ch || !geforce_context ||
        !geforce_context->display_out || !geforce_context->display_restore_out)
        return links_result(c,GF4_EINVAL);
    /* Close every dispatch before validation; no window close, fini, wait,
     * reservation release or metadata publication is hidden in closure. */
    *geforce_context->display_out=0;
    *geforce_context->display_restore_out=0;
    c->accepting=false;
    struct gf3_service *s=gf3_service_get(c->ch);
    i32 rc=s ? gf3_service_admission(s,false) : GF4_ESTATE;
    if (!s) gf_hook_scan_bind(false,0);
    if (rc || !open) return links_result(c,rc);
    if (!c->state || !c->accel) return links_result(c,GF4_EINVAL);
    if (!c->ready || !c->reusable || !c->ch->epoch || c->epoch!=c->ch->epoch ||
        c->ch->state!=GF2_RUN || !c->ch->f || c->ch->fault.latched ||
        c->accel!=&s->production->accel || !c->accel->ready ||
        c->accel->ch!=c->ch || c->accel->epoch!=c->ch->epoch || !s->owner ||
        (c->saved_closed && !c->diagnostic_saved))
        return links_result(c,GF4_ESTATE);
    /* transaction.c opens before committing head: next is authoritative
     * while changing. Validate both heads before changing either loan. */
    const struct gf4_head_image *heads=c->changing ? c->state->next : c->state->head;
    for (u8 h=0;h<GF4_HEADS;++h) if (heads[h].enabled) {
        rc=gf4_surface_check(c,&heads[h].front,&heads[h].mode);
        if (rc) return links_result(c,rc);
    }
    const struct gf3_surface *scan=heads[0].enabled ? &heads[0].front : 0;
    const struct gf3_surface *test=s->test.bytes || s->test.reservation_id ? &s->test : 0;
    rc=gf3_service_surfaces(s,scan,test);
    if (rc) return links_result(c,rc);
    display_ops=(struct gf4_test_ops){
        .ctx=c,.set_mode=gf4_set_mode,.ddc_read=gf4_ddc_read,.flip=gf4_flip,
        .last_rc=links_last_rc,.modes=gf4_modes,.detect=gf4_detect,
        .dpms_set=gf4_dpms_set,.cursor_image=gf4_cursor_image,
        .cursor_move=gf4_cursor_move,.cursor_show=gf4_cursor_show,
        .vblank_wait=gf4_vblank_wait,.gamma=gf4_gamma,
        .display_save=gf4_diagnostic_save,.palette_set=gf4_palette_set,
        .scan_prepare=gf4_scan_prepare,.scan_discard=gf4_scan_discard
    };
    restore_ops=(struct gf4_restore_ops){c,gf4_diagnostic_restore,links_last_rc,
                                          gf4_display_saved_release};
    /* No waits or failing HW operations in this foreground publication
     * interval. F3 validates loans before it publishes its resident ops. */
    rc=gf3_service_admission(s,true);
    if (rc) return links_result(c,rc);
    if (scan) gf_hook_scan_changed(0,scan,heads[0].mode.id);
    c->accepting=true;
    *geforce_context->display_restore_out=&restore_ops;
    *geforce_context->display_out=&display_ops;
    return 0;
}
/* No external encoder type is approved: dfp.c:280-480 delegates these
 * operations to the encoder, never to a guessed register sequence. */
static i32 links_external_mode(void *owner,const struct gf4_route *route,
                              const struct gf4_mode *mode,u32 dl)
{
    (void)owner; (void)route; (void)mode; (void)dl;
    return GF4_EUNSUPPORTED;
}
static i32 links_external_dpms(void *owner,const struct gf4_route *route,
                              enum gf4_dpms state,u32 dl)
{
    (void)owner; (void)route; (void)state; (void)dl;
    return GF4_EUNSUPPORTED;
}
/* nv10.c:61-83: low pins have separate direction/output bits; the
 * extended banks pack each pair into a nibble. Preserve every other bit. */
enum { LINKS_GPIO_LOW=0x600818U, LINKS_GPIO_EXT=0x60081cU,
       LINKS_GPIO_HIGH=0x600850U, LINKS_GPIO_LOW_MASK=0x11U,
       LINKS_GPIO_PAIR_MASK=3U, LINKS_GPIO_LOW_BITS=16U,
       LINKS_GPIO_EXT_BITS=4U, LINKS_GPIO_LOW_END=2U,
       LINKS_GPIO_EXT_END=10U, LINKS_GPIO_END=14U };
static i32 links_tv_gpio(void *owner,bool on,u32 dl)
{
    /* exec.owner is the resident links: service_init supplies its ctx
     * afterwards. This avoids a second mutable owner/bind protocol. */
    const struct gf4_links *links=owner;
    struct gf4_ctx *c=links ? links->owner : 0;
    (void)dl; /* GPIO drive has neither a wait nor a renewed deadline. */
    if (!c || !c->ch || !c->ch->f) return links_result(c,GF4_EINVAL);
    struct gf_io *io=&c->ch->f->io;
    if (!io->read || !io->write) return links_result(c,GF4_EINVAL);
    for (u32 i=0;i<2U;++i)
        if (c->dcb.tvdac[i].present && c->dcb.tvdac[i].line>=LINKS_GPIO_END)
            return links_result(c,GF4_EINVAL);
    for (u32 i=0;i<2U;++i) {
        const struct gf4_gpio_value *g=&c->dcb.tvdac[i];
        if (!g->present) continue;
        u32 off,shift,mask,data=g->log[on ? 1 : 0],value;
        if (g->line<LINKS_GPIO_LOW_END) {
            off=LINKS_GPIO_LOW; shift=g->line*LINKS_GPIO_LOW_BITS;
            mask=LINKS_GPIO_LOW_MASK;
            data=((data&2U)<<3)|(data&1U);
        } else {
            bool ext=g->line<LINKS_GPIO_EXT_END;
            off=ext ? LINKS_GPIO_EXT : LINKS_GPIO_HIGH;
            shift=(g->line-(ext ? LINKS_GPIO_LOW_END : LINKS_GPIO_EXT_END))*LINKS_GPIO_EXT_BITS;
            mask=LINKS_GPIO_PAIR_MASK; data&=mask;
        }
        i32 rc=io->read(io->opaque,GF_MODE_REGISTER_BAR,off,4,&value);
        if (!rc) rc=io->write(io->opaque,GF_MODE_REGISTER_BAR,off,4,
                              (value&~(mask<<shift))|(data<<shift));
        if (rc) return links_result(c,rc);
    }
    return 0;
}
static struct gf4_links display_links={
    .i2c_port=links_i2c_port,.admission=links_admission,
    .failed=gf4_service_failed,.scan_changed=gf4_scan_changed
};
static const struct gf4_exec_ops display_exec={
    /* tmds_script remains NULL until the C2-links-b interpreter contract. */
    .external_mode=links_external_mode,.external_dpms=links_external_dpms,
    .tv_gpio=links_tv_gpio,.owner=&display_links
};
struct gf4_links *gf4_links_table(void) { return &display_links; }
const struct gf4_exec_ops *gf4_exec_table(void) { return &display_exec; }
#endif
