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



/* Independent oracle: nouveau c03c1a1 dispnv04/dfp.c:68-90,204-480,
 * 113-148,563-576; hw.c:503-527; hw.h:109-116; nvreg.h:368-428.
 * Golden FP words below are evaluated from the stated input modelines,
 * not from the product's work image. No reference tree is read at runtime. */
#define GF4_HOST_UNIT 10
#include "gf4_display_fake.h"
#define REQUIRE(x) do { __asm__ volatile ("" ::: "memory"); if (!(x)) return __LINE__; } while (0)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
/* nvreg.h:368-428, hw.h:70-96 and dfp.c:68-90. */
#define T_STRIDE 0x2000U
#define T_CR 0x6013d4U
#define T_OWNER 0x44U
#define T_PIXEL 0x28U
#define T_LCD 0x33U
#define T_SEL 0x680524U
#define T_V 0x680800U
#define T_H 0x680820U
#define T_DITHER 0x68083cU
#define T_TG 0x680848U
#define T_MARGIN 0x68084cU
#define T_PATTERN 0x680850U
#define T_DEBUG 0x680880U
#define T_DATA 0x6808b4U
#define T_CONTROL 0x6808b0U
#define T_BANK 8U
#define T_BIND_INDEX 4U
#define T_BIND_LOCAL 0x80U
#define T_BIND_CROSS 0x88U
#define T_BIND_XOR 8U
#define T_LVDS 1U
#define T_TEST 0x680608U
#define T_TEST_C 0x68U
#define T_TEST_B 0x2000U
#define T_TEST_VALUE 0x00100000U
#define T_BOOT 0x101000U /* dfp.c:340-341. */
#define T_BOOT_WIDE (8U<<12)
/* dfp.c:48-58,323-366 and nvreg.h:384-411. */
#define T_POS_H (1U<<4)
#define T_POS_V 1U
#define T_DISP (1U<<28)
#define T_ON (T_DISP|T_POS_H|T_POS_V)
#define T_OFF ((2U<<28)|(2U<<4)|2U)
#define T_NATIVE (2U<<8)
#define T_WIDE (1U<<24)
#define T_DUAL (8U<<28)
#define T_SAVED_BITS ((1U<<26)|(1U<<20))
#define T_DEBUG_VALUE ((1U<<24)|(1U<<20)|(1U<<12)|(1U<<8)|(8U<<4)|(1U<<4)|1U)
#define T_LIMIT_KHZ 165000U /* TMDS dual-link boundary. */
/* Distinct seed values reveal damage to unrelated registers and state. */
#define T_OLD_OWNER 4U
#define T_OWNER_HEAD_B 3U
#define T_PIXEL_32 3U
#define T_PIXEL_SLAVED 0x80U
#define T_LCD_SAVED 0xffU
#define T_LCD_KEEP 0xc4U /* nvreg.h:267: ff with route mask 3b removed. */
#define T_LCD_DIGITAL 3U
#define T_LCD_HEAD_A 8U
#define T_SPREAD_SECOND 0xa0U /* dfp.c:236-241, second spread pair. */
#define T_OWNER_SELECT_DATA_WRITE 3U /* read index, select index, select data. */
#define T_OLD_FP (T_ON|(1U<<17))
#define T_OTHER_FP 0x12345678U
#define T_OLD_ON (T_ON|T_SAVED_BITS|T_NATIVE|T_SCRIPT_BIT)
#define T_SEL_SEED 0xa5050055U
#define T_DITHER_SEED 0x10U
#define T_PATTERN_SEED 0x13579bdfU
#define T_SCRIPT_BIT (1U<<19)
#define T_DEADLINE 1000U
#define T_FAILURE (-777)
#define T_OR_A 1U
#define T_OR_B 2U
#define T_OR_C 4U
#define T_OR_D 8U
static const u8 output_masks[]={T_OR_A,T_OR_B,T_OR_C,T_OR_D};
/* dac.c:41-52: independent physical TEST offsets for the four DCB ORs. */
static const u32 test_offsets[]={0,T_TEST_B,T_TEST_C,T_TEST_B+T_TEST_C};
#define T_SEL_A (1U<<16)
#define T_SEL_C (1U<<18)
#define T_SPREAD_MASK 0xf0U
#define T_SPREAD_A 0x10U
#define T_SPREAD_B 0x40U
/* gfinit_mode palette/dither have six saved NV4x pattern words. */
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_host_event events[256];
static struct gf3_host_reg regs[128];
static struct gf4_route route;
static struct gfinit_mode input;
static bool script_off,script_expire;
static u32 script_control;
static const struct gf4_timing modelines[]={
    {40000U,800,840,968,1056,600,601,605,628,0},
    {108000U,1280,1328,1440,1688,1024,1025,1028,1066,0}
};
/* dfp.c:305-321, seven FP indices in disp.h:13-21. */
static const u32 golden_h[][7]={
    {799,1055,800,839,967,0,799},
    {1279,1687,1280,1327,1439,0,1279}
};
static const u32 golden_v[][7]={
    {599,627,622,600,604,0,599},
    {1023,1065,1060,1024,1027,0,1023}
};
static u32 get(u32 off)
{
    u32 value=0;
    for (u32 b=0;b<4U;++b) value|=gf4_host_reg_byte(&host,0,off+b)<<(b*8U);
    return value;
}
static void observe(struct gf4_host *h,const struct gf4_host_event *e)
{
    if (e->op==D_SCRIPT && !e->rc) {
        u32 off=T_TG+route.head*T_STRIDE;
        script_control=get(off)|T_SCRIPT_BIT;
        if (script_off) script_control=(script_control&~T_ON)|T_OFF;
        gf3_host_set(&h->base,0,off,script_control);
        if (script_expire) h->local_now=T_DEADLINE;
    }
}
static void setup(u8 head,u8 or_mask,u8 type,bool changing,u32 model,u32 flags,u32 khz,bool dual,u32 boot)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=COUNT(events);
    host.base.regs=regs; host.base.cregs=COUNT(regs); host.observe=observe;
    host.ctx.changing=changing;
    route=(struct gf4_route){.head=head,.head_mask=3U,.or_mask=or_mask,.type=type};
    struct gf4_head_image image={.enabled=true,.route=route,.fp_on=T_OLD_ON};
    image.mode.t=modelines[model]; image.mode.t.flags=flags; image.mode.t.khz=khz;
    state.head[head]=state.next[head]=image;
    input=(struct gfinit_mode){.fp_control=T_SAVED_BITS|T_OFF,.dither=T_DITHER_SEED};
    input.cr[T_PIXEL]=T_PIXEL_32; input.cr[T_LCD]=T_LCD_SAVED;
    for (u32 i=0;i<COUNT(input.dither_regs);++i) input.dither_regs[i]=T_PATTERN_SEED+i;
    route.dual_link=dual;
    gf3_host_set(&host.base,0,T_TG+head*T_STRIDE,T_OLD_FP);
    gf3_host_set(&host.base,0,T_TG+(head^1U)*T_STRIDE,T_OTHER_FP);
    gf3_host_set(&host.base,0,T_SEL,T_SEL_SEED);
    gf3_host_set(&host.base,0,T_BOOT,boot);
    host.cr[0][T_OWNER]=T_OLD_OWNER;
    script_off=script_expire=false; script_control=0;
}
struct expected { enum gf4_host_op op; u32 off,width,value; };
static struct expected expected[128];
static u32 nexpected;
static void write_expected(u32 off,u32 width,u32 value)
{
    assert(nexpected<COUNT(expected));
    expected[nexpected++]=(struct expected){D_WRITE,off,width,value};
}
static int golden(u32 model,u32 flags,bool dual,bool wide)
{
    u32 d=route.head*T_STRIDE;
    u32 select_bit=route.or_mask==T_OR_A ? T_SEL_A : T_SEL_C;
    u32 select=T_SEL_SEED;
    if (route.head) select|=select_bit; else select&=~select_bit;
    if (route.type==GF4_LVDS)
        select=(select&~T_SPREAD_MASK)|(route.head ? T_SPREAD_B : T_SPREAD_A);
    u32 on=T_SAVED_BITS|T_NATIVE|T_DISP;
    if (!(flags&GF4_NEG_H)) on|=T_POS_H;
    if (!(flags&GF4_NEG_V)) on|=T_POS_V;
    if (wide) on|=T_WIDE;
    if (dual) on|=T_DUAL;
    u32 final=on|T_SCRIPT_BIT;
    nexpected=0;
    write_expected(T_CR,1,T_OWNER); /* owner read index */
    write_expected(T_CR,1,T_OWNER);
    write_expected(T_CR+1U,1,route.head ? T_OWNER_HEAD_B : 0U);
    write_expected(T_TG+d,4,(T_OLD_FP&~T_ON)|T_OFF);
    write_expected(T_SEL,4,select);
    write_expected(T_CR+d,1,T_PIXEL); write_expected(T_CR+d+1U,1,T_PIXEL_32|T_PIXEL_SLAVED);
    write_expected(T_CR+d,1,T_LCD);
    write_expected(T_CR+d+1U,1,T_LCD_KEEP|T_LCD_DIGITAL|(route.head ? 0 : T_LCD_HEAD_A));
    for (u32 i=0;i<COUNT(golden_v[model]);++i) {
        write_expected(T_V+d+i*4U,4,golden_v[model][i]);
        write_expected(T_H+d+i*4U,4,golden_h[model][i]);
    }
    write_expected(T_DITHER+d,4,T_DITHER_SEED);
    for (u32 i=0;i<COUNT(input.dither_regs)/2U;++i) {
        write_expected(T_PATTERN+d+i*4U,4,T_PATTERN_SEED+i);
        write_expected(T_PATTERN+d+(i+3U)*4U,4,T_PATTERN_SEED+i+3U);
    }
    write_expected(T_TG+d,4,on);
    write_expected(T_DEBUG+d,4,T_DEBUG_VALUE);
    write_expected(T_DEBUG+d+4U,4,0); write_expected(T_DEBUG+d+8U,4,0);
    write_expected(T_MARGIN+d,4,0);
    expected[nexpected++]=(struct expected){D_SCRIPT,0,0,0};
    u32 test_offset=0;
    for (u32 i=0;i<COUNT(output_masks);++i)
        if (route.or_mask==output_masks[i]) test_offset=test_offsets[i];
    write_expected(T_TEST+test_offset,4,T_TEST_VALUE);
    write_expected(T_TG+d,4,final);
    write_expected(T_CR,1,T_OWNER); write_expected(T_CR+1U,1,T_OLD_OWNER);
    u32 n=0,script_at=0;
    for (u32 i=0;i<host.nevents;++i) {
        const struct gf4_host_event *e=&events[i];
        if (e->op!=D_WRITE && e->op!=D_SCRIPT) continue;
        REQUIRE(n<nexpected && e->op==expected[n].op);
        if (e->op==D_WRITE) {
            REQUIRE(e->a[0]==0 && e->a[1]==expected[n].off);
            REQUIRE(e->a[2]==expected[n].width && e->a[3]==expected[n].value);
        } else {
            REQUIRE(e->p[0]==&host && e->p[1]==&route);
            REQUIRE(e->a[0]==(host.ctx.changing ? state.next : state.head)[route.head].mode.t.khz);
            REQUIRE(e->a[1]==T_DEADLINE); script_at=i;
        }
        ++n;
    }
    REQUIRE(n==nexpected && host.calls[D_SCRIPT]==1);
    /* dfp.c:461-470: re-read script-modified TG before TEST/ON. */
    REQUIRE(events[script_at+1U].op==D_CLOCK && events[script_at+2U].op==D_READ && events[script_at+2U].a[1]==T_TG+d);
    REQUIRE(get(T_TG+d)==final && get(T_TG+(route.head^1U)*T_STRIDE)==T_OTHER_FP);
    REQUIRE(get(T_SEL)==select && host.cr[0][T_OWNER]==T_OLD_OWNER);
    const struct gf4_head_image *image=host.ctx.changing ? state.next : state.head;
    REQUIRE(image[route.head].fp_on==final && state.work[route.head].fp_control==final);
    REQUIRE(state.work[route.head].head==route.head);
    for (u32 i=0;i<COUNT(golden_v[model]);++i) {
        REQUIRE(state.work[route.head].fp_vert[i]==golden_v[model][i]);
        REQUIRE(state.work[route.head].fp_horiz[i]==golden_h[model][i]);
    }
    REQUIRE((host.ctx.changing ? state.head : state.next)[route.head].fp_on==T_OLD_ON);
    return 0;
}
static int loads(void)
{
    for (u8 head=0;head<GF4_HEADS;++head)
        for (u32 or=0;or<COUNT(output_masks);++or)
            for (u32 model=0;model<COUNT(modelines);++model)
                for (u32 flags=0;flags<= (GF4_NEG_H|GF4_NEG_V);++flags) {
                    setup(head,output_masks[or],GF4_TMDS,head!=0,model,flags,
                          modelines[model].khz,false,0);
                    REQUIRE(!gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE));
                    REQUIRE(!golden(model,flags,false,false));
                }
    for (u32 type=GF4_TMDS;type<=GF4_LVDS;++type)
        for (u32 dual=0;dual<3U;++dual) {
            setup(0,T_OR_C,(u8)type,true,0,0,T_LIMIT_KHZ-1U+dual,dual==2U,T_BOOT_WIDE);
            script_off=true;
            REQUIRE(!gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE));
            REQUIRE(!golden(0,0,dual==2U,true));
        }
    /* dfp.c:236-241: alternate saved spread pair is shifted once. */
    for (u8 head=0;head<GF4_HEADS;++head) {
        setup(head,T_OR_A,GF4_LVDS,false,0,0,modelines[0].khz,false,0);
        const u32 second_pair=(T_SEL_SEED&~T_SPREAD_MASK)|T_SPREAD_SECOND;
        gf3_host_set(&host.base,0,T_SEL,second_pair);
        REQUIRE(!gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE));
        u32 expect=(second_pair&~(T_SPREAD_MASK|T_SEL_A))|
            (head ? T_SEL_A|(T_SPREAD_B<<1) : T_SPREAD_A<<1);
        REQUIRE(get(T_SEL)==expect);
    }
    return 0;
}
/* Every fallible load event is injected once, including owner restoration.
 * Only the two owner-restoration writes may follow an earlier failure. */
static int load_failures(void)
{
    setup(1,T_OR_C,GF4_TMDS,true,0,0,modelines[0].khz,false,0);
    REQUIRE(!gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE));
    u32 count[D_OP_COUNT];
    gf2_host_memcpy(count,host.calls,sizeof(count));
    const enum gf4_host_op ops[]={D_ROUTE,D_CLOCK,D_READ,D_WRITE,D_SCRIPT};
    for (u32 op=0;op<COUNT(ops);++op)
        for (u32 nth=1;nth<=count[ops[op]];++nth) {
            setup(1,T_OR_C,GF4_TMDS,true,0,0,modelines[0].khz,false,0);
            struct gfinit_mode before=state.work[1];
            gf4_host_fail_at(&host,ops[op],nth,T_FAILURE);
            REQUIRE(gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE)==T_FAILURE);
            REQUIRE(state.next[1].fp_on==T_OLD_ON && state.head[1].fp_on==T_OLD_ON);
            REQUIRE(!gf2_host_memcmp(&state.work[1],&before,sizeof(before)));
            REQUIRE(get(T_TG)==T_OTHER_FP);
            bool failed=false;
            for (u32 i=0;i<host.nevents;++i) {
                const struct gf4_host_event *e=&events[i];
                if (failed) {
                    REQUIRE(e->op==D_WRITE);
                    REQUIRE((e->a[1]==T_CR && e->a[3]==T_OWNER) ||
                            (e->a[1]==T_CR+1U && e->a[3]==T_OLD_OWNER));
                }
                if (e->rc) failed=true;
            }
            REQUIRE(failed);
            /* Either cleanup write may itself fail; previous write failures
             * still restore the saved owner, including applied-error I/O. */
            if (ops[op]!=D_WRITE || nth<count[D_WRITE]-1U)
                REQUIRE(host.cr[0][T_OWNER]==T_OLD_OWNER);
        }
    setup(1,T_OR_C,GF4_TMDS,true,0,0,modelines[0].khz,false,0);
    /* First owner selection DATA write is applied but reports an error. */
    gf4_host_write_applied_error(&host,T_OWNER_SELECT_DATA_WRITE,T_FAILURE);
    REQUIRE(gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE)==T_FAILURE);
    REQUIRE(host.cr[0][T_OWNER]==T_OLD_OWNER && !host.calls[D_SCRIPT]);
    REQUIRE(state.next[1].fp_on==T_OLD_ON && !state.work[1].fp_control);
    setup(1,T_OR_C,GF4_TMDS,true,0,0,modelines[0].khz,false,0);
    script_expire=true;
    REQUIRE(!gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE));
    REQUIRE(host.cr[0][T_OWNER]==T_OLD_OWNER);
    REQUIRE(host.ctx.deadline==T_DEADLINE+GF2_TIMEOUT_US);
    REQUIRE(state.work[1].fp_control);
    return 0;
}
static int dpms(void)
{
    for (u8 head=0;head<GF4_HEADS;++head)
        for (u32 mode=GF4_ON;mode<=GF4_OFF;++mode) {
            setup(head,T_OR_A,GF4_TMDS,true,0,0,modelines[0].khz,false,0);
            REQUIRE(!gf4_tmds_dpms(&host.ctx,&route,(enum gf4_dpms)mode,T_DEADLINE));
            u32 expect=mode==GF4_ON ? T_OLD_ON : (T_OLD_FP&~T_ON)|T_OFF;
            REQUIRE(get(T_TG+head*T_STRIDE)==expect);
            REQUIRE(get(T_TG+(head^1U)*T_STRIDE)==T_OTHER_FP);
            REQUIRE(state.next[head].fp_on==T_OLD_ON && state.head[head].fp_on==T_OLD_ON);
            REQUIRE(state.work[head].fp_control==expect);
            REQUIRE(host.calls[D_WRITE]==1 && !host.calls[D_SCRIPT] && !host.calls[D_DAC]);
            gf4_host_trace_clear(&host);
            REQUIRE(!gf4_tmds_dpms(&host.ctx,&route,GF4_ON,T_DEADLINE));
            REQUIRE(get(T_TG+head*T_STRIDE)==T_OLD_ON);
        }
    const enum gf4_host_op ops[]={D_ROUTE,D_CLOCK,D_READ,D_WRITE};
    for (u32 i=0;i<COUNT(ops);++i) {
        setup(1,T_OR_A,GF4_LVDS,false,0,0,modelines[0].khz,false,0);
        gf4_host_fail_at(&host,ops[i],1,T_FAILURE);
        REQUIRE(gf4_tmds_dpms(&host.ctx,&route,GF4_OFF,T_DEADLINE)==T_FAILURE);
        REQUIRE(state.head[1].fp_on==T_OLD_ON && !state.work[1].fp_control);
        REQUIRE(get(T_TG)==T_OTHER_FP);
        REQUIRE(events[host.nevents-1U].rc==T_FAILURE);
    }
    return 0;
}
static int boundary(void)
{
    setup(0,T_OR_A,GF4_TMDS,false,0,0,modelines[0].khz,false,0);
    route.type=GF4_ANALOG;
    REQUIRE(gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_EINVAL);
    REQUIRE(!host.nevents);
    route.type=GF4_TMDS; route.head=GF4_HEADS;
    REQUIRE(gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_EINVAL);
    route.head=0; host.ctx.epoch++;
    REQUIRE(gf4_tmds_dpms(&host.ctx,&route,GF4_OFF,T_DEADLINE)==GF4_ESTATE);
    REQUIRE(!host.nevents);
    host.ctx.epoch=host.ctx.ch->epoch;
    REQUIRE(gf4_tmds_load(&host.ctx,&route,0,T_DEADLINE)==GF4_EINVAL);
    REQUIRE(gf4_tmds_dpms(&host.ctx,&route,(enum gf4_dpms)-1,T_DEADLINE)==GF4_EINVAL);
    for (u32 external=0;external<2U;++external) {
        setup(0,T_OR_A,GF4_TMDS,false,0,0,modelines[0].khz,false,0);
        route.location=external ? 0 : 1; route.ext_type=external ? 1 : 0;
        gf4_host_fail_at(&host,D_EXTERNAL,1,GF4_EUNSUPPORTED);
        REQUIRE(gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_EUNSUPPORTED);
        REQUIRE(!host.calls[D_WRITE] && !host.calls[D_SCRIPT]);
        REQUIRE(events[host.nevents-1U].p[2]==&state.head[0].mode);
        gf4_host_fail_at(&host,D_EXTERNAL_DPMS,1,T_FAILURE);
        REQUIRE(gf4_tmds_dpms(&host.ctx,&route,GF4_OFF,T_DEADLINE)==T_FAILURE);
        REQUIRE(!host.calls[D_WRITE] && !host.calls[D_SCRIPT]);
        REQUIRE(events[host.nevents-1U].a[1]==T_DEADLINE);
    }
    setup(0,T_OR_A,GF4_TMDS,false,0,0,modelines[0].khz,false,0);
    state.head[0].mode.t.vt=0;
    REQUIRE(gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_ERANGE);
    REQUIRE(!host.calls[D_WRITE] && !host.calls[D_SCRIPT]);
    setup(0,T_OR_A,GF4_TMDS,false,0,0,modelines[0].khz,false,0);
    gf4_host_fail_at(&host,D_ROUTE,1,GF4_ENODEV);
    REQUIRE(gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_ENODEV);
    REQUIRE(!host.calls[D_WRITE]);
    setup(0,T_OR_A,GF4_TMDS,false,0,0,modelines[0].khz,false,0);
    route.location=1;
    /* Even an adapter accidentally reporting success cannot admit a new
     * external encoder type in this wave. */
    REQUIRE(gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_EUNSUPPORTED);
    REQUIRE(gf4_tmds_dpms(&host.ctx,&route,GF4_ON,T_DEADLINE)==GF4_EUNSUPPORTED);
    REQUIRE(!host.calls[D_WRITE]);
    setup(0,T_OR_A,GF4_TMDS,false,0,0,modelines[0].khz,false,0);
    host.local_now=T_DEADLINE;
    REQUIRE(gf4_tmds_load(&host.ctx,&route,&input,T_DEADLINE)==GF4_ETIME);
    REQUIRE(!host.calls[D_WRITE]);
    REQUIRE(gf4_tmds_dpms(&host.ctx,&route,GF4_ON,T_DEADLINE)==GF4_ETIME);
    REQUIRE(!host.calls[D_WRITE]);
    return 0;
}
static int test(void)
{
    int rc=loads();
    if (!rc) rc=load_failures();
    if (!rc) rc=dpms();
    if (!rc) rc=boundary();
    return rc;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? 1 : 0));
    __builtin_unreachable();
}
