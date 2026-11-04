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

/* NV44 internal TV: nouveau c03c1a1 dispnv04/tvnv17.c:144-218,
 * 368-606; tvmodesnv17.c:367-434,470-589; dac.c:235-330.
 * C0 owns rollback. Only complete images are adopted here. */
#include "api.h"
#include "../foundation.h"

/* dispnv04/nvreg.h:114-127,248,290,347-422; hw.h:70-96. */
#define TV_STRIDE 0x2000U
#define TV_CR 0x6013d4U
/* nvreg.h:82,129-133,248; crtc.c:334-337,579-582. */
#define TV_SR 0x0c03c4U
#define TV_PIXEL 0x28U
#define TV_SLAVED (1U<<7)
#define TV_CLOCK 1U
#define TV_CLKDIV2 (1U<<3)
#define TV_OWNER 0x44U
#define TV_RPC1 0x1aU
#define TV_POWER2 0x1588U
#define TV_POWER4 0x1590U
#define TV_DACCLK 0x68052cU
#define TV_TEST 0x680608U
#define TV_POINT 0x680610U
#define TV_TG 0x680848U
#define TV_CTV 0x680c00U
#define TV_INDEX 0xd220U
#define TV_DATA 0xd224U
#define TV_PTV 0xd000U
#define TV_ON_BITS ((1U<<28)|(1U<<4)|1U)
#define TV_OFF_BITS ((2U<<28)|(2U<<4)|2U)
#define TV_CTV_ON ((1U<<28)|(1U<<24)|(1U<<20)|(1U<<4)|1U)
#define TV_CTV_DEBUG ((1U<<24)|(1U<<20)|(1U<<12)|(1U<<8)|(8U<<4)|(1U<<4)|1U)
/* dac.c:252-254,304-312; tvnv17.c:598-602. */
#define TV_SAMPLE_RGB (0xa0U|(0xa0U<<10)|(0xa0U<<20))
#define TV_SAMPLE_NOTBLANK (8U<<28)
#define TV_SAMPLE_INSERT (1U<<12)
#define TV_DAC_POWER_OFF (1U<<16)
#define TV_TEST_RUN 0x00100000U
#define TV_CURIE (0x1aU<<16)
#define TV_GREEN 0x680630U
#define TV_SETUP 0x680700U
#define TV_8C0 0x6808c0U
#define TV_FP_VERT 0x680800U
#define TV_FP_HORIZ 0x680820U
#define TV_FP_DEBUG 0x680880U
#define TV_FP_MARGIN 0x68084cU
#define TV_TMDS_CONTROL 0x6808b0U
#define TV_TMDS_DATA 0x6808b4U
#define TV_GPIO_LOW 0x600818U
#define TV_GPIO_EXT 0x60081cU
#define TV_GPIO_HIGH 0x600850U
#define TV_HFILTER 0xd310U
#define TV_HFILTER2 0xd390U
#define TV_VFILTER 0xd510U
#define TV_SAMPLE_MASK 0xeU
#define TV_HEAD_BIT 0x10U
/* Local representation, owned solely by tv.c, api.h ctx.tv_subconnector. */
enum tv_connector { TV_UNKNOWN,TV_COMPOSITE,TV_SVIDEO,TV_COMPONENT,TV_SCART };
/* tvnv17.h:56-70: HD480i/HD576i use TV, not CTV (tvmodesnv17.c:123-146). */
enum { TV_HD480P=7,TV_HD576P=9,TV_HD720P=10,TV_HD1080I=11,TV_NORMS=12 };
static bool tv_ctv(u32 norm)
{
    return norm==TV_HD480P || norm==TV_HD576P || norm==TV_HD720P || norm==TV_HD1080I;
}
static const u32 tv_ptv_offsets[]={
    0x200,0x204,0x208,0x20c,0x304,0x500,0x504,0x508,
    0x600,0x604,0x608,0x60c,0x610,0x614
}; /* tvmodesnv17.c:417-430; data.h ptv[] follows this order. */
static i32 tv_read(struct gf_io *io,u32 off,u32 width,u32 *out)
{
    return io->read(io->opaque,0,off,width,out);
}
static i32 tv_write(struct gf_io *io,u32 off,u32 width,u32 value)
{
    return io->write(io->opaque,0,off,width,value);
}
static i32 tv_index(struct gf_io *io,u32 port,u32 index,u32 value)
{
    i32 rc=tv_write(io,port,1,index);
    return rc ? rc : tv_write(io,port+1U,1,value);
}
static i32 tv_deadline(struct gf_io *io,u32 dl)
{
    u32 now;
    i32 rc=io->timing.clock_us(io->opaque,&now);
    return rc ? rc : (i32)(now-dl)>=0 ? GF4_ETIME : GF4_OK;
}
static i32 tv_wait(struct gf_io *io,u32 us,u32 dl)
{
    u32 now;
    i32 rc=io->timing.clock_us(io->opaque,&now);
    if (rc) return rc;
    if ((i32)(now-dl)>=0 || us>dl-now) return GF4_ETIME;
    rc=io->timing.wait_us(io->opaque,us);
    return rc ? rc : tv_deadline(io,dl);
}
static u32 tv_output(const struct gf4_route *r)
{
    /* dac.c:41-52: physical OR offset, independent of head. */
    return ((r->or_mask&12U) ? 0x68U : 0)+((r->or_mask&10U) ? TV_STRIDE : 0);
}
static struct gf4_head_image *tv_image(struct gf4_ctx *ctx,u8 head)
{
    return &(ctx->changing ? ctx->state->next : ctx->state->head)[head];
}
static i32 tv_check(struct gf4_ctx *ctx,const struct gf4_route *r)
{
    if (!ctx || !ctx->state || !r || r->head>=GF4_HEADS || r->type!=GF4_TV ||
        r->tv_norm>=TV_NORMS) return GF4_EINVAL;
    if (!ctx->ready || !ctx->ch || !ctx->ch->f || ctx->epoch!=ctx->ch->epoch)
        return GF4_ESTATE;
    if (r->location || r->ext_type) return GF4_EUNSUPPORTED;
    struct gf_io *io=&ctx->ch->f->io;
    if (!io->read || !io->write || !io->timing.clock_us || !io->timing.wait_us ||
        !ctx->exec || !ctx->exec->tv_gpio) return GF4_ESTATE;
    return gf4_route_check(ctx,r);
}
/* nvkm/subdev/gpio/nv10.c:32-88. Save raw TVDAC drive fields so two
 * different previous pin levels can be restored without a new getter API.
 * exec.tv_gpio still owns logical GPIO writes. No absent line is invented. */
struct tv_gpio_saved { u32 off,mask,bits; };
static i32 tv_gpio_save(struct gf4_ctx *ctx,struct tv_gpio_saved saved[2],u32 *n)
{
    struct gf_io *io=&ctx->ch->f->io;
    *n=0;
    for (u32 i=0;i<2U;++i) {
        const struct gf4_gpio_value *g=&ctx->dcb.tvdac[i];
        if (!g->present) continue;
        u32 shift,mask,off,value;
        if (g->line<2U) { off=TV_GPIO_LOW; shift=g->line*16U; mask=0x11U; }
        else if (g->line<10U) { off=TV_GPIO_EXT; shift=(g->line-2U)*4U; mask=3U; }
        else if (g->line<14U) { off=TV_GPIO_HIGH; shift=(g->line-10U)*4U; mask=3U; }
        else return GF4_EINVAL;
        i32 rc=tv_read(io,off,4,&value);
        if (rc) return rc;
        mask<<=shift;
        saved[(*n)++]=(struct tv_gpio_saved){off,mask,value&mask};
    }
    return 0;
}
/* tvnv17.c:144-218 uses nv17_dac_sample_load on NV44 (NV42/43 are
 * deliberately not selected). dac.c:235-330: two samples are ANDed.
 * Cleanup attempts every saved field even if an earlier restore fails;
 * the original negative rc wins. Valid is published only after cleanup. */
i32 gf4_tv_sense(struct gf4_ctx *ctx,const struct gf4_route *r,u32 dl)
{
    i32 rc=tv_check(ctx,r);
    if (rc) return rc;
    struct gf_io *io=&ctx->ch->f->io;
    u32 o=tv_output(r),dac,test,power2,power4=0,owner,rpc,head;
    struct tv_gpio_saved gpio[2]; u32 ngpio;
    rc=tv_deadline(io,dl);
    if (!rc) rc=tv_read(io,TV_DACCLK+o,4,&dac);
    if (!rc) rc=tv_read(io,TV_TEST+o,4,&test);
    if (!rc) rc=tv_read(io,TV_POWER2,4,&power2);
    if (!rc && o==0x68U) rc=tv_read(io,TV_POWER4,4,&power4);
    if (!rc) rc=tv_gpio_save(ctx,gpio,&ngpio);
    if (!rc) rc=tv_write(io,TV_CR,1,TV_OWNER);
    if (!rc) rc=tv_read(io,TV_CR+1U,1,&owner);
    if (rc) return rc;
    head=(dac>>8)&1U;
    rc=tv_index(io,TV_CR,TV_OWNER,head ? 3U : 0);
    if (!rc) rc=tv_write(io,TV_CR+head*TV_STRIDE,1,TV_RPC1);
    if (!rc) rc=tv_read(io,TV_CR+head*TV_STRIDE+1U,1,&rpc);
    if (!rc && !(rpc&0xc0U)) head^=1U;
    u32 head_test=0,a=0,b=0;
    bool touched=false,inserted=false;
    if (!rc) rc=tv_read(io,TV_TEST+head*TV_STRIDE,4,&head_test);
    if (!rc) {
        touched=true;
        rc=tv_write(io,TV_TEST+o,4,test&~TV_DAC_POWER_OFF);
    }
    if (!rc) rc=tv_write(io,TV_POWER2,4,power2&0xd7ffffffU);
    if (!rc && o==0x68U) rc=tv_write(io,TV_POWER4,4,power4&0xffffffcfU);
    if (!rc) rc=ctx->exec->tv_gpio(ctx->exec->owner,true,dl);
    if (!rc) rc=tv_wait(io,4000U,dl); /* dac.c:284. */
    if (!rc) rc=tv_write(io,TV_DACCLK+o,4,(dac&0xfffffeceU)|(head<<8)|TV_CURIE);
    if (!rc) rc=tv_wait(io,1000U,dl); /* dac.c:303. */
    u32 temp;
    if (!rc) rc=tv_read(io,TV_DACCLK+o,4,&temp);
    if (!rc) rc=tv_write(io,TV_DACCLK+o,4,temp|1U);
    if (!rc) {
        inserted=true;
        rc=tv_write(io,TV_POINT+head*TV_STRIDE,4,TV_SAMPLE_NOTBLANK|TV_SAMPLE_RGB);
    }
    if (!rc) rc=tv_read(io,TV_TEST+head*TV_STRIDE,4,&temp);
    if (!rc) rc=tv_write(io,TV_TEST+head*TV_STRIDE,4,temp|TV_SAMPLE_INSERT);
    if (!rc) rc=tv_wait(io,5000U,dl); /* dac.c:312. */
    if (!rc) rc=tv_read(io,TV_TEST+o,4,&a);
    if (!rc) rc=tv_read(io,TV_TEST+o,4,&b);
    /* dac.c:318-330, always run restoration after possibly applied writes. */
    i32 cleanup=0,step;
#define TV_RESTORE(off,value) do { step=tv_write(io,(off),4,(value)); if (!cleanup) cleanup=step; } while (0)
    if (inserted) {
        TV_RESTORE(TV_TEST+head*TV_STRIDE,head_test&~TV_SAMPLE_INSERT);
        TV_RESTORE(TV_POINT+head*TV_STRIDE,0);
    }
    if (touched) {
        TV_RESTORE(TV_DACCLK+o,dac);
        TV_RESTORE(TV_TEST+o,test);
        if (o==0x68U) TV_RESTORE(TV_POWER4,power4);
        TV_RESTORE(TV_POWER2,power2);
        for (u32 i=0;i<ngpio;++i) {
            step=tv_read(io,gpio[i].off,4,&temp);
            if (!step) step=tv_write(io,gpio[i].off,4,(temp&~gpio[i].mask)|gpio[i].bits);
            if (!cleanup) cleanup=step;
        }
    }
#undef TV_RESTORE
    step=tv_index(io,TV_CR,TV_OWNER,owner);
    if (!cleanup) cleanup=step;
    if (!rc) rc=cleanup;
    if (!rc) {
        u8 pins=(a&b)>>28&TV_SAMPLE_MASK;
        u8 connector=TV_UNKNOWN;
        if (pins==2U || pins==4U) connector=TV_COMPOSITE;
        else if (pins==12U) connector=TV_SVIDEO;
        /* nvkm/subdev/bios/dcb.c:125 zeroes tvconf, never sets component.
         * D0 exposes that same parser: use its default false branch, not
         * a norm-based guess at has_component_output (tvnv17.c:175-179). */
        else if (pins==14U) connector=TV_SCART;
        ctx->tv_pin_mask=pins;
        ctx->tv_subconnector=connector;
        ctx->tv_sense_valid=true;
    }
    return rc;
}

/* tvnv17.c:368-400. Caller keeps DPMS/ownership in head/next until C0
 * commits. PTV200 is global; FP is per head; DACCLK sharing belongs to B9. */
static i32 tv_power(struct gf4_ctx *ctx,const struct gf4_route *r,
                    struct gf4_tv_state *tv,u32 *fp,enum gf4_dpms dpms,u32 dl)
{
    struct gf_io *io=&ctx->ch->f->io;
    bool on=dpms==GF4_ON;
    u32 delta=r->head*TV_STRIDE,value;
    i32 rc=tv_deadline(io,dl);
    if (rc) return rc;
    bool ctv=tv_ctv(r->tv_norm);
    if (ctv && on) value=*fp;
    else {
        rc=tv_read(io,TV_TG+delta,4,&value);
        if (rc) return rc;
        value=(value&~TV_ON_BITS)|TV_OFF_BITS;
    }
    rc=tv_write(io,TV_TG+delta,4,value);
    u32 ptv=(tv->ptv[0]&~1U)|(!ctv && on ? 1U : 0);
    if (!rc) rc=tv_write(io,TV_PTV+tv_ptv_offsets[0],4,ptv);
    if (!rc) rc=ctx->exec->tv_gpio(ctx->exec->owner,on,dl);
    if (!rc) rc=tv_deadline(io,dl);
    if (!rc) rc=gf4_dac_set(ctx,r,on);
    if (!rc) { tv->ptv[0]=ptv; *fp=value; }
    return rc;
}
i32 gf4_tv_dpms(struct gf4_ctx *ctx,const struct gf4_route *r,enum gf4_dpms dpms,u32 dl)
{
    if ((unsigned)dpms>GF4_OFF) return GF4_EINVAL;
    i32 rc=tv_check(ctx,r);
    if (rc) return rc;
    struct gf4_tv_state next=ctx->state->tv[r->head];
    u32 fp=tv_image(ctx,r->head)->fp_on;
    rc=tv_power(ctx,r,&next,&fp,dpms,dl);
    if (!rc) { ctx->state->tv[r->head]=next; ctx->state->work[r->head].fp_control=fp; }
    return rc;
}
/* tvmodesnv17.c:148-213, tvnv17.c:540-574. Raw encoder output timings
 * stay full-frame, including 1080i; they are not CRTC-normalized timings. */
static const struct gf4_timing tv_ctv_output[]={
    {27000,720,735,743,858,480,490,494,525,0},
    {27000,720,730,738,864,576,581,585,625,0},
    {74250,1280,1349,1357,1650,720,725,730,750,0},
    {74250,1920,1961,2049,2200,1080,1084,1088,1125,GF4_INTERLACE}
};
/* tvmodesnv17.c:439-451, vertical sizes after generator DBLSCAN. */
static const struct { u16 hd,vd; } tv_clkdiv_modes[]={
    {320,2U*200U},{320,2U*240U},{400,2U*300U}
};
static i32 tv_fp_calc(u32 norm,const struct gf4_timing *in,struct gfinit_mode *m)
{
    u32 n=norm==TV_HD480P ? 0 : norm==TV_HD576P ? 1 : norm==TV_HD720P ? 2 : 3;
    const struct gf4_timing *out=&tv_ctv_output[n];
    /* B4 normalizes CRTC fields once. Restore full-frame display height
     * for tvmodesnv17.c:549-569, which uses mode, not crtc_vdisplay. */
    u32 vd=in->vd;
    if (in->flags&GF4_INTERLACE) vd*=2U;
    if (in->flags&GF4_DBLSCAN) vd/=2U;
    if (!in->hd || !vd || in->hd>out->hd || vd>out->vd) return GF4_ERANGE;
    /* tvmodesnv17.c:553-566: overscan=50 => interpolation midpoint;
     * interlace forces 100 => full margins. */
    u32 hm=(out->hd-in->hd)/2U,vm=(out->vd-vd)/2U;
    if (!(out->flags&GF4_INTERLACE)) {
        if (hm>out->hd/20U) hm=out->hd/20U;
        if (vm>out->vd/20U) vm=out->vd/20U;
    }
    u32 hr=in->hd*0x800U/(out->hd-2U*hm);
    u32 vr=(vd*0x800U/(out->vd-2U*vm))&~3U;
    i32 hcrtc=((i32)out->hd-600)/40-1;
    if (hcrtc<1) hcrtc=1;
    const u32 h[]={out->hd-1U,out->ht-1U,out->hd+(u32)hcrtc,
                   out->hs-1U,out->he-1U,hm,out->hd-hm-1U};
    const u32 v[]={out->vd-1U,out->vt-1U,out->vd-1U,
                   out->vs-1U,out->ve-1U,vm,out->vd-vm-1U};
    for (u32 i=0;i<7U;++i) { m->fp_horiz[i]=h[i]; m->fp_vert[i]=v[i]; }
    m->fp_control=TV_CTV_ON;
    m->fp_debug[0]=TV_CTV_DEBUG;
    m->fp_debug[1]=(1U<<28)|(vr<<16)|(1U<<12)|hr;
    m->fp_debug[2]=0;
    m->fp_margin=0x801080U;
    return 0;
}
/* tvmodesnv17.c:470-523, properties fixed at saturation=50, hue=0.
 * interpolate(...,50) preserves the helper's norm saturation bytes. */
static void tv_properties(struct gf4_ctx *ctx,struct gf4_tv_state *tv,u8 head)
{
    tv->ptv[0]=(tv->ptv[0]&~TV_HEAD_BIT)|(head ? TV_HEAD_BIT : 0);
    switch (ctx->tv_subconnector) {
    case TV_COMPOSITE:
        tv->ptv[1]=2U|((ctx->tv_pin_mask&4U) ? 0x010000U :
                       (ctx->tv_pin_mask&2U) ? 0x100000U : 0x110000U);
        tv->enc[7]=0x10; break;
    case TV_SVIDEO: tv->ptv[1]=0x11012U; tv->enc[7]=0x18; break;
    case TV_COMPONENT: tv->ptv[1]=0x111333U; tv->enc[7]=0x14; break;
    case TV_SCART: tv->ptv[1]=0x111012U; tv->enc[7]=0x18; break;
    default: break;
    }
    tv->enc[0x25]=0;
}
static i32 tv_state_load(struct gf_io *io,const struct gf4_tv_state *tv,u32 dl)
{
    i32 rc=0;
    for (u32 i=0;!rc && i<sizeof(tv->enc);++i) {
        rc=tv_deadline(io,dl);
        if (!rc) rc=tv_write(io,TV_INDEX,4,i);
        if (!rc) rc=tv_write(io,TV_DATA,4,tv->enc[i]);
    }
    const u32 bases[]={TV_HFILTER,TV_HFILTER2,TV_VFILTER};
    const u32 offsets[]={0,0x1cU,0x40U,0x5cU}; /* tvmodesnv17.c:367-378. */
    const u32 (*filters[])[7]={tv->hfilter,tv->hfilter2,tv->vfilter};
    for (u32 f=0;!rc && f<3U;++f)
        for (u32 i=0;!rc && i<4U;++i)
            for (u32 j=0;!rc && j<7U;++j) {
                rc=tv_deadline(io,dl);
                if (!rc) rc=tv_write(io,bases[f]+offsets[i]+4U*j,4,filters[f][i][j]);
            }
    for (u32 i=0;!rc && i<sizeof(tv_ptv_offsets)/sizeof(tv_ptv_offsets[0]);++i)
        rc=tv_write(io,TV_PTV+tv_ptv_offsets[i],4,tv->ptv[i]);
    /* tvmodesnv17.c:432-434: latch is encoder register 3e, not PTV3e. */
    if (!rc) rc=tv_write(io,TV_INDEX,4,0x3eU);
    if (!rc) rc=tv_write(io,TV_DATA,4,1);
    if (!rc) rc=tv_write(io,TV_INDEX,4,0x3eU);
    if (!rc) rc=tv_write(io,TV_DATA,4,0);
    return rc;
}

/* tvnv17.c:423-440 and dfp.c:55-90: move an unused internal FP
 * encoder off the CTV head before enabling its timing generator. */
static i32 tv_unbind_fp(struct gf4_ctx *ctx,u8 head)
{
    struct gf_io *io=&ctx->ch->f->io;
    for (u32 i=0;i<ctx->nroutes;++i) {
        const struct gf4_route *r=&ctx->routes[i];
        if ((r->type!=GF4_TMDS && r->type!=GF4_LVDS) || r->location || r->ext_type) continue;
        bool active=false;
        for (u32 h=0;h<GF4_HEADS;++h) {
            const struct gf4_head_image *image=tv_image(ctx,(u8)h);
            if (image->enabled && image->route.type==r->type &&
                image->route.or_mask==r->or_mask) active=true;
        }
        if (active) continue;
        u32 ramdac=(r->or_mask&4U)>>2,delta=ramdac*TV_STRIDE,value;
        i32 rc=tv_write(io,TV_TMDS_CONTROL+delta,4,(1U<<16)|4U);
        if (!rc) rc=tv_read(io,TV_TMDS_DATA+delta,4,&value);
        if (rc) return rc;
        if ((((value>>3)&1U)^ramdac)!=head) continue;
        u32 bind=0x80U|((head^1U)!=ramdac ? 8U : 0)|(r->type==GF4_LVDS ? 1U : 0);
        for (u32 bank=0;bank<(r->dual_link ? 2U : 1U);++bank) {
            rc=tv_write(io,TV_TMDS_DATA+delta+bank*8U,4,bind^(bank ? 8U : 0));
            if (!rc) rc=tv_write(io,TV_TMDS_CONTROL+delta+bank*8U,4,4U);
            if (rc) return rc;
        }
    }
    return 0;
}

i32 gf4_tv_load(struct gf4_ctx *ctx,const struct gf4_route *r,
                 const struct gfinit_mode *mode,u32 dl)
{
    if (!mode) return GF4_EINVAL;
    i32 rc=tv_check(ctx,r);
    if (rc) return rc;
    if (!ctx->data || ctx->data->version!=GF4_DATA_VERSION ||
        ctx->data->size!=sizeof(*ctx->data) || !ctx->data->tv_state) return GF4_ESTATE;
    struct gf4_head_image *image=tv_image(ctx,r->head);
    struct gf4_tv_state tv;
    rc=ctx->data->tv_state(r->tv_norm,&image->mode.t,&tv);
    if (rc) return rc;
    if (tv.version!=GF4_TV_VERSION || tv.size!=sizeof(tv) || tv.norm!=r->tv_norm)
        return GF4_ESTATE;
    struct gfinit_mode work=*mode;
    bool ctv=tv_ctv(r->tv_norm);
    work.head=r->head;
    /* crtc.c:579-582: preserve pixel depth and enable TV slaving. */
    work.cr[TV_PIXEL]|=TV_SLAVED;
    /* tvmodesnv17.c:439-451: only the three LD doublescan modelines
     * request CLKDIV2. Their vertical sizes here are CRTC-normalized. */
    const struct gf4_timing *t=&image->mode.t;
    bool clkdiv2=false;
    if (!ctv && (t->flags&GF4_DBLSCAN))
        for (u32 i=0;i<sizeof(tv_clkdiv_modes)/sizeof(tv_clkdiv_modes[0]);++i)
            if (t->hd==tv_clkdiv_modes[i].hd && t->vd==tv_clkdiv_modes[i].vd)
                clkdiv2=true;
    work.sr[TV_CLOCK]=(work.sr[TV_CLOCK]&~TV_CLKDIV2)|(clkdiv2 ? TV_CLKDIV2 : 0);
    work.cr[0x33]&=~0x3bU; /* dfp.c:110-111: remove old FP route. */
    work.cr[0x53]=0x40; work.cr[0x54]=0; /* tvnv17.c:476-480. */
    work.ramdac630=2; work.tv_setup=1; work.ramdac8c0=0;
    if (ctv) { rc=tv_fp_calc(r->tv_norm,&image->mode.t,&work); if (rc) return rc; }
    if (!ctx->tv_sense_valid) { rc=gf4_tv_sense(ctx,r,dl); if (rc) return rc; }
    if (!ctv) tv_properties(ctx,&tv,r->head);
    struct gf_io *io=&ctx->ch->f->io;
    u32 d=r->head*TV_STRIDE,o=tv_output(r),owner,dac,control;
    rc=tv_deadline(io,dl);
    if (!rc) rc=tv_write(io,TV_CR,1,TV_OWNER);
    if (!rc) rc=tv_read(io,TV_CR+1U,1,&owner);
    if (rc) return rc;
    rc=tv_index(io,TV_CR,TV_OWNER,r->head ? 3U : 0);
    /* tvnv17.c:403-455: encoder OFF, FP disable, route DACCLK. */
    struct gf4_tv_state off=ctx->state->tv[r->head];
    u32 off_fp=image->fp_on;
    if (!rc) rc=tv_power(ctx,r,&off,&off_fp,GF4_OFF,dl);
    if (!rc) rc=tv_read(io,TV_TG+d,4,&control);
    if (!rc) rc=tv_write(io,TV_TG+d,4,(control&~TV_ON_BITS)|TV_OFF_BITS);
    if (!rc && ctv) rc=tv_unbind_fp(ctx,r->head);
    if (!rc) rc=tv_read(io,TV_DACCLK+o,4,&dac);
    if (!rc) {
        work.dacclk=(dac&~0x30U)|1U|TV_CURIE|(ctv ? 0x20U : 0x10U);
        if (ctv) {
            work.dacclk=(work.dacclk&~0x100U)|(r->head ? 0x100U : 0);
            work.cr[0x33]|=1U|(r->head ? 0 : 8U);
        }
        rc=tv_write(io,TV_DACCLK+o,4,work.dacclk);
    }
    if (!rc) rc=tv_index(io,TV_CR+d,TV_PIXEL,work.cr[TV_PIXEL]);
    if (!rc) rc=tv_index(io,TV_SR+d,TV_CLOCK,work.sr[TV_CLOCK]);
    if (!rc) rc=tv_index(io,TV_CR+d,0x33U,work.cr[0x33]);
    if (!rc) rc=tv_index(io,TV_CR+d,0x53U,work.cr[0x53]);
    if (!rc) rc=tv_index(io,TV_CR+d,0x54U,work.cr[0x54]);
    if (!rc) rc=tv_write(io,TV_GREEN+d,4,work.ramdac630);
    if (!rc) rc=tv_write(io,TV_SETUP+d,4,work.tv_setup);
    if (!rc) rc=tv_write(io,TV_8C0+d,4,work.ramdac8c0);
    /* hw.c:503-535 loads the CTV table and FP per-head state. */
    if (ctv) {
        for (u32 i=0;!rc && i<7U;++i) {
            rc=tv_write(io,TV_FP_VERT+d+i*4U,4,work.fp_vert[i]);
            if (!rc) rc=tv_write(io,TV_FP_HORIZ+d+i*4U,4,work.fp_horiz[i]);
        }
        if (!rc) rc=tv_write(io,TV_TG+d,4,work.fp_control);
        for (u32 i=0;!rc && i<3U;++i) rc=tv_write(io,TV_FP_DEBUG+d+i*4U,4,work.fp_debug[i]);
        if (!rc) rc=tv_write(io,TV_FP_MARGIN+d,4,work.fp_margin);
        for (u32 i=0;!rc && i<sizeof(tv.ctv)/sizeof(tv.ctv[0]);++i)
            rc=tv_write(io,TV_CTV+d+4U*i,4,tv.ctv[i]);
    }
    if (!rc) rc=tv_state_load(io,&tv,dl);
    if (!rc) rc=tv_write(io,TV_TEST+o,4,TV_TEST_RUN);
    if (!rc) rc=tv_power(ctx,r,&tv,&work.fp_control,GF4_ON,dl);
    i32 restore=tv_index(io,TV_CR,TV_OWNER,owner);
    if (!rc) rc=restore;
    if (!rc) {
        ctx->state->tv[r->head]=tv;
        ctx->state->work[r->head]=work;
        if (ctv) image->fp_on=work.fp_control;
    }
    return rc;
}
