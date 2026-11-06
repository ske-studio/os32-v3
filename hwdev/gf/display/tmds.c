/*
 * Copyright 2003 NVIDIA, Corporation
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

/* $XConsortium: nvreg.h /main/2 1996/10/28 05:13:41 kaleb $ */
/*
 * Copyright 1996-1997  David J. McKay
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
 * DAVID J. MCKAY BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */



/* NV44A: nouveau c03c1a1 dispnv04/{dfp.c,hw.c,hw.h,nvreg.h}.
 * C0 owns rollback; work is a candidate, head/next owns the last ON value. */
#include "api.h"
#include "../foundation.h"
#include "timeout.h"

/* nvreg.h:129-139,248,267,368-428; hw.h:70-96,109-124. */
#define FP_BAR 0U
#define FP_STRIDE 0x2000U
#define FP_CR 0x6013d4U
#define FP_OWNER 0x44U
#define FP_PIXEL 0x28U
#define FP_LCD 0x33U
#define FP_SLAVED 0x80U
#define FP_LCD_MASK 0x3bU
#define FP_LCD_DIGITAL 3U
#define FP_LCD_HEAD_A 8U
#define FP_SEL_CLK 0x680524U
#define FP_SEL_A (1U << 16)
#define FP_SEL_C (1U << 18)
/* dfp.c:236-241: preserve the active LVDS spread-spectrum pair. */
#define FP_SPREAD_MASK 0xf0U
#define FP_SPREAD_FIRST 0x50U
#define FP_SPREAD_A 0x10U
#define FP_SPREAD_B 0x40U
#define FP_VERT 0x680800U
#define FP_HORIZ 0x680820U
#define FP_DITHER 0x68083cU
#define FP_TG 0x680848U
#define FP_MARGIN 0x68084cU
#define FP_PATTERN 0x680850U
#define FP_DEBUG 0x680880U
#define FP_TMDS_CONTROL 0x6808b0U
#define FP_TMDS_DATA 0x6808b4U
#define FP_LINK_STRIDE 8U
#define FP_BIND_INDEX 4U
#define FP_BIND_BASE 0x80U
#define FP_BIND_OTHER 8U
#define FP_BIND_LVDS 1U
#define FP_OR_A 1U
#define FP_OR_B 2U
#define FP_OR_C 4U
#define FP_OR_D 8U
/* dfp.c:48-58,323-366; nvreg.h:384-411. */
#define FP_ON_BITS ((1U << 28) | (1U << 4) | 1U)
#define FP_OFF_BITS ((2U << 28) | (2U << 4) | 2U)
#define FP_KEEP ((1U << 26) | (1U << 20))
#define FP_NATIVE (2U << 8)
#define FP_WIDE (1U << 24)
#define FP_DUAL (8U << 28)
#define FP_DUAL_KHZ 165000U
#define FP_DEBUG_DEFAULT ((1U << 24) | (1U << 20) | (1U << 12) | \
                          (1U << 8) | (8U << 4) | (1U << 4) | 1U)
/* dfp.c:340-341 (PEXTDEV_BOOT_0 STRAP_FP_IFACE_12BIT). */
#define FP_BOOT 0x101000U
#define FP_BOOT_12BIT (8U << 12)
/* dfp.c:465-470; dac.c:41-52: TEST follows physical OR, not CRTC. */
#define FP_TEST 0x680608U
#define FP_TEST_C_OFFSET 0x68U
#define FP_TEST_B_OFFSET 0x2000U
#define FP_TEST_VALUE 0x00100000U

static i32 fp_read(struct gf_io *io,u32 off,u32 width,u32 *value)
{
    return io->read(io->opaque,FP_BAR,off,width,value);
}
static i32 fp_write(struct gf_io *io,u32 off,u32 width,u32 value)
{
    return io->write(io->opaque,FP_BAR,off,width,value);
}
static i32 fp_index(struct gf_io *io,u32 port,u32 index,u32 value)
{
    i32 rc=fp_write(io,port,1,index);
    return rc ? rc : fp_write(io,port+1U,1,value);
}
static i32 fp_deadline(struct gf_io *io,u32 dl,u8 head,const char *stage)
{
    u32 now;
    i32 rc=io->timing.clock_us(io->opaque,&now);
    return rc ? rc : (i32)(now-dl)>=0 ? gf4_timeout(stage,head,dl,now,0,0) : GF4_OK;
}
static i32 fp_check(struct gf4_ctx *ctx,const struct gf4_route *route)
{
    if (!ctx || !ctx->state || !route || route->head>=GF4_HEADS) return GF4_EINVAL;
    if (!ctx->ready || !ctx->ch || !ctx->ch->f || ctx->epoch!=ctx->ch->epoch)
        return GF4_ESTATE;
    if (route->type!=GF4_TMDS && route->type!=GF4_LVDS) return GF4_EINVAL;
    struct gf_io *io=&ctx->ch->f->io;
    if (!io->read || !io->write || !io->timing.clock_us) return GF4_ESTATE;
    return gf4_route_check(ctx,route);
}
static struct gf4_head_image *fp_image(struct gf4_ctx *ctx,u8 head)
{
    return &(ctx->changing ? ctx->state->next : ctx->state->head)[head];
}

/* dfp.c:305-445: no separate panel/scaling property in A0. Native timing,
 * zero hskew, and saved dither (unknown panel bpc) use the unscaled branch. */
static void fp_calc(const struct gf4_head_image *image,const struct gf4_route *route,
                    u32 boot,struct gfinit_mode *out)
{
    const struct gf4_timing *t=&image->mode.t;
    const u32 horiz[]={t->hd-1U,t->ht-1U,t->hd,t->hs-1U,t->he-1U,0,t->hd-1U};
    const u32 vert[]={t->vd-1U,t->vt-1U,t->vt-6U,t->vs-1U,t->ve-1U,0,t->vd-1U};
    for (u32 i=0;i<7U;++i) { out->fp_horiz[i]=horiz[i]; out->fp_vert[i]=vert[i]; }
    out->fp_control=(out->fp_control&FP_KEEP)|(1U<<28)|FP_NATIVE;
    if (!(t->flags&GF4_NEG_H)) out->fp_control|=1U<<4;
    if (!(t->flags&GF4_NEG_V)) out->fp_control|=1U;
    if (boot&FP_BOOT_12BIT) out->fp_control|=FP_WIDE;
    if (route->type==GF4_LVDS ? route->dual_link : t->khz>FP_DUAL_KHZ)
        out->fp_control|=FP_DUAL;
    out->fp_debug[0]=FP_DEBUG_DEFAULT;
    out->fp_debug[1]=out->fp_debug[2]=out->fp_margin=0;
    out->cr[FP_PIXEL]|=FP_SLAVED;
    out->cr[FP_LCD]=(out->cr[FP_LCD]&~FP_LCD_MASK)|FP_LCD_DIGITAL|
        (route->head ? 0 : FP_LCD_HEAD_A);
}

/* hw.c:503-527: seven V/H pairs, dither and paired patterns, TG, debug,
 * margin. No PLL/TV/analog sequence is owned by this encoder. */
static i32 fp_load(struct gf_io *io,u8 head,const struct gfinit_mode *m)
{
    u32 delta=head*FP_STRIDE;
    i32 rc=0;
    for (u32 i=0;!rc && i<7U;++i) {
        rc=fp_write(io,FP_VERT+delta+i*4U,4,m->fp_vert[i]);
        if (!rc) rc=fp_write(io,FP_HORIZ+delta+i*4U,4,m->fp_horiz[i]);
    }
    if (!rc) rc=fp_write(io,FP_DITHER+delta,4,m->dither);
    for (u32 i=0;!rc && i<3U;++i) {
        rc=fp_write(io,FP_PATTERN+delta+i*4U,4,m->dither_regs[i]);
        if (!rc) rc=fp_write(io,FP_PATTERN+delta+(i+3U)*4U,4,m->dither_regs[i+3U]);
    }
    if (!rc) rc=fp_write(io,FP_TG+delta,4,m->fp_control);
    for (u32 i=0;!rc && i<3U;++i) rc=fp_write(io,FP_DEBUG+delta+i*4U,4,m->fp_debug[i]);
    if (!rc) rc=fp_write(io,FP_MARGIN+delta,4,m->fp_margin);
    return rc;
}

i32 gf4_tmds_load(struct gf4_ctx *ctx,const struct gf4_route *route,
                   const struct gfinit_mode *mode,u32 dl)
{
    if (!mode) return GF4_EINVAL;
    i32 rc=fp_check(ctx,route);
    if (rc) return rc;
    struct gf4_head_image *image=fp_image(ctx,route->head);
    /* C2 rejects every external type before any HW change (B10 card). */
    if (route->location || route->ext_type) {
        if (!ctx->exec || !ctx->exec->external_mode) return GF4_EUNSUPPORTED;
        rc=ctx->exec->external_mode(ctx->exec->owner,route,&image->mode,dl);
        return rc ? rc : GF4_EUNSUPPORTED;
    }
    if (!ctx->exec || !ctx->exec->tmds_script) return GF4_ESTATE;
    const struct gf4_timing *t=&image->mode.t;
    if (!t->khz || !t->hd || t->hd>t->hs || t->hs>=t->he || t->he>t->ht ||
        !t->vd || t->vd>t->vs || t->vs>=t->ve || t->ve>t->vt || t->vt<6U)
        return GF4_ERANGE;
    struct gf_io *io=&ctx->ch->f->io;
    u32 owner,boot,sel,control,delta=route->head*FP_STRIDE;
    rc=fp_deadline(io,dl,route->head,"tmds_load_deadline");
    if (!rc) rc=fp_write(io,FP_CR,1,FP_OWNER);
    if (!rc) rc=fp_read(io,FP_CR+1U,1,&owner);
    if (rc) return rc;
    /* hw.c:85-106: save owner, select CRTC0 CR44, restore even on error. */
    rc=fp_index(io,FP_CR,FP_OWNER,route->head ? 3U : 0U);
    if (!rc) rc=fp_read(io,FP_BOOT,4,&boot);
    if (!rc) rc=fp_read(io,FP_SEL_CLK,4,&sel);
    struct gfinit_mode result=*mode;
    if (!rc) {
        fp_calc(image,route,boot,&result);
        result.head=route->head;
        u32 bit=route->or_mask&FP_OR_A ? FP_SEL_A : FP_SEL_C;
        result.sel_clk=(sel&~bit)|(route->head ? bit : 0);
        if (route->type==GF4_LVDS && (sel&FP_SPREAD_MASK)) {
            u32 spread=route->head ? FP_SPREAD_B : FP_SPREAD_A;
            if (!(sel&FP_SPREAD_FIRST)) spread<<=1;
            result.sel_clk=(result.sel_clk&~FP_SPREAD_MASK)|spread;
        }
        /* dfp.c:245-278,68-90; hw.h:109-116. OFF precedes binding. */
        rc=fp_read(io,FP_TG+delta,4,&control);
        if (!rc) rc=fp_write(io,FP_TG+delta,4,(control&~FP_ON_BITS)|FP_OFF_BITS);
        if (!rc) rc=fp_write(io,FP_SEL_CLK,4,result.sel_clk);
        if (!rc) rc=fp_index(io,FP_CR+delta,FP_PIXEL,result.cr[FP_PIXEL]);
        if (!rc) rc=fp_index(io,FP_CR+delta,FP_LCD,result.cr[FP_LCD]);
        u32 ramdac=(route->or_mask&FP_OR_C)>>2;
        u32 bind=FP_BIND_BASE|(route->head!=ramdac ? FP_BIND_OTHER : 0)|
            (route->type==GF4_LVDS ? FP_BIND_LVDS : 0);
        bool dual=route->type==GF4_LVDS ? route->dual_link : t->khz>FP_DUAL_KHZ;
        for (u32 bank=0;!rc && bank<(dual ? 2U : 1U);++bank) {
            u32 off=ramdac*FP_STRIDE+bank*FP_LINK_STRIDE;
            rc=fp_write(io,FP_TMDS_DATA+off,4,bind^(bank ? FP_BIND_OTHER : 0));
            if (!rc) rc=fp_write(io,FP_TMDS_CONTROL+off,4,FP_BIND_INDEX);
        }
        if (!rc) rc=fp_load(io,route->head,&result);
        /* dfp.c:446-480: C2 owns init-context head/output/deadline and ROM
         * acquire/script/release; do not retain its borrowed ROM here. */
        if (!rc) rc=fp_deadline(io,dl,route->head,"tmds_before_script");
        if (!rc) rc=ctx->exec->tmds_script(ctx->exec->owner,route,t->khz,dl);
        if (!rc) rc=fp_read(io,FP_TG+delta,4,&control);
        u32 test=FP_TEST;
        if (route->or_mask&(FP_OR_C|FP_OR_D)) test+=FP_TEST_C_OFFSET;
        if (route->or_mask&(FP_OR_B|FP_OR_D)) test+=FP_TEST_B_OFFSET;
        if (!rc) rc=fp_write(io,test,4,FP_TEST_VALUE);
        if (!rc) rc=fp_deadline(io,dl,route->head,"tmds_after_script");
        if (!rc) {
            /* Scripts may leave TG OFF: retain their other changes while
             * restoring this mode's sync polarity (dfp.c:113-128). */
            if ((control&(FP_ON_BITS|FP_OFF_BITS))==FP_OFF_BITS)
                control=(control&~(FP_ON_BITS|FP_OFF_BITS))|
                    (result.fp_control&(FP_ON_BITS|FP_OFF_BITS));
            result.fp_control=control;
            rc=fp_write(io,FP_TG+delta,4,control);
        }
    }
    i32 restore=fp_index(io,FP_CR,FP_OWNER,owner);
    if (!rc) rc=restore;
    if (!rc) {
        ctx->state->work[route->head]=result;
        image->fp_on=result.fp_control;
    }
    return rc;
}

/* B10-b: dfp.c:113-148,563-576. fp_on is never replaced by an OFF mask. */
i32 gf4_tmds_dpms(struct gf4_ctx *ctx,const struct gf4_route *route,
                   enum gf4_dpms dpms,u32 dl)
{
    if ((unsigned)dpms>GF4_OFF) return GF4_EINVAL;
    i32 rc=fp_check(ctx,route);
    if (rc) return rc;
    if (route->location || route->ext_type) {
        if (!ctx->exec || !ctx->exec->external_dpms) return GF4_EUNSUPPORTED;
        rc=ctx->exec->external_dpms(ctx->exec->owner,route,dpms,dl);
        return rc ? rc : GF4_EUNSUPPORTED;
    }
    struct gf_io *io=&ctx->ch->f->io;
    struct gf4_head_image *image=fp_image(ctx,route->head);
    u32 off=FP_TG+route->head*FP_STRIDE,value=image->fp_on;
    rc=fp_deadline(io,dl,route->head,"tmds_dpms_deadline");
    if (!rc && dpms!=GF4_ON) {
        rc=fp_read(io,off,4,&value);
        value=(value&~FP_ON_BITS)|FP_OFF_BITS;
    }
    if (!rc) rc=fp_write(io,off,4,value);
    if (!rc) ctx->state->work[route->head].fp_control=value;
    return rc;
}
