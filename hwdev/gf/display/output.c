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
 * see docs/design/hwdev/gf/display/output.md for the assignment ownership. */
#include "calc.h"
void gf_mode_output(struct gfinit_mode *o)
{
    o->fp_control = GF_MODE_FP_OFF;
    o->ramdac8c0 = GF_MODE_RAMDAC8C0;
    o->ramdaca24 = GF_MODE_RAMDACA24;
    o->ramdaca34 = 1;
    o->dacclk = (GF_MODE_HEAD << 8) | GF_MODE_DAC_ENABLE; o->test_control = GF_MODE_TEST_CONTROL;
}

/* nouveau dispnv04/dac.c:41-51,364-444,460-473; nvreg.h:329-330,348. */
#include "api.h"
#include "../foundation.h"

#define OUT_BAR 0U
#define OUT_DACCLK 0x68052cU
#define OUT_TEST 0x680608U
#define OUT_TEST_VALUE 0x00100000U
#define OUT_ENABLE 1U
#define OUT_HEAD_SELECT 0x100U
#define OUT_OR_B 2U
#define OUT_OR_C 4U
#define OUT_OR_D 8U
#define OUT_B_OFFSET 0x2000U
#define OUT_C_OFFSET 0x68U

static u32 output_offset(u8 mask)
{
    return ((mask & (OUT_OR_B | OUT_OR_D)) ? OUT_B_OFFSET : 0U) +
           ((mask & (OUT_OR_C | OUT_OR_D)) ? OUT_C_OFFSET : 0U);
}

static bool route_equal(const struct gf4_route *a,const struct gf4_route *b)
{
    return a->connector==b->connector && a->type==b->type &&
        a->or_mask==b->or_mask && a->i2c_index==b->i2c_index &&
        a->location==b->location && a->head_mask==b->head_mask &&
        a->ext_type==b->ext_type && a->dual_link==b->dual_link &&
        a->max_khz==b->max_khz && a->tv_norm==b->tv_norm;
}

i32 gf4_route_check(struct gf4_ctx *ctx,const struct gf4_route *route)
{
    if (!ctx || !ctx->state || !route || (ctx->nroutes && !ctx->routes)) return GF4_EINVAL;
    if (route->head>=GF4_HEADS) return GF4_EINVAL;
    if (!(route->head_mask & (1U<<route->head)) ||
        !route->or_mask || (route->or_mask & (route->or_mask-1U)) || route->or_mask>OUT_OR_D)
        return GF4_ENODEV;
    bool found=false;
    for (u32 i=0;i<ctx->nroutes;i++)
        if (route_equal(route,&ctx->routes[i])) { found=true; break; }
    if (!found) return GF4_ENODEV;
    if (route->location || route->ext_type || route->type>GF4_LVDS) return GF4_EUNSUPPORTED;
    const struct gf4_head_image *heads=ctx->changing ? ctx->state->next : ctx->state->head;
    for (u32 i=0;i<GF4_HEADS;i++)
        if (i!=route->head && heads[i].enabled && (heads[i].route.or_mask & route->or_mask))
            return GF4_EBUSY;
    return 0;
}

i32 gf4_dac_set(struct gf4_ctx *ctx,const struct gf4_route *route,bool on)
{
    if (!ctx || !ctx->state || !ctx->ch || !ctx->ch->f || !route ||
        route->head>=GF4_HEADS || !route->or_mask || route->or_mask>OUT_OR_D ||
        (route->or_mask & (route->or_mask-1U))) return GF4_EINVAL;
    struct gf_io *io=&ctx->ch->f->io;
    if (!io->read || !io->write) return GF4_EINVAL;
    const struct gf4_head_image *heads=ctx->changing ? ctx->state->next : ctx->state->head;
    bool used=on;
    for (u32 i=0;i<GF4_HEADS;i++)
        if (i!=route->head && heads[i].enabled && heads[i].dpms==GF4_ON &&
            (heads[i].route.or_mask & route->or_mask) &&
            (heads[i].route.type==GF4_ANALOG || heads[i].route.type==GF4_TV)) used=true;
    u32 value,off=OUT_DACCLK+output_offset(route->or_mask);
    int rc=io->read(io->opaque,OUT_BAR,off,4,&value);
    if (rc) return rc;
    value=(value & ~OUT_ENABLE) | (used ? OUT_ENABLE : 0U);
    return io->write(io->opaque,OUT_BAR,off,4,value);
}

i32 gf4_analog_dpms(struct gf4_ctx *ctx,const struct gf4_route *route,enum gf4_dpms dpms,u32 dl)
{
    (void)dl;
    if ((unsigned)dpms>GF4_OFF || !route || route->type!=GF4_ANALOG) return GF4_EINVAL;
    return gf4_dac_set(ctx,route,dpms==GF4_ON);
}

i32 gf4_analog_load(struct gf4_ctx *ctx,const struct gf4_route *route,const struct gfinit_mode *mode,u32 dl)
{
    if (!mode || !route || route->type!=GF4_ANALOG) return GF4_EINVAL;
    int rc=gf4_route_check(ctx,route);
    if (rc) return rc;
    rc=gf4_analog_dpms(ctx,route,GF4_OFF,dl);
    if (rc) return rc;
    struct gf_io *io=&ctx->ch->f->io;
    u32 off=OUT_DACCLK+output_offset(route->or_mask),value;
    rc=io->read(io->opaque,OUT_BAR,off,4,&value);
    if (!rc) rc=io->write(io->opaque,OUT_BAR,off,4,
                           (value & ~(OUT_HEAD_SELECT|OUT_ENABLE)) |
                           (route->head ? OUT_HEAD_SELECT : 0U) | OUT_ENABLE);
    if (!rc) rc=io->write(io->opaque,OUT_BAR,OUT_TEST+output_offset(route->or_mask),4,OUT_TEST_VALUE);
    if (!rc) rc=gf4_analog_dpms(ctx,route,GF4_ON,dl);
    return rc;
}
