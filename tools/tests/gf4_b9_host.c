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
/* MIT nouveau dispnv04/dac.c:41-51,364-444,460-473; nvreg.h:329,348. */
#define GF4_HOST_UNIT 9
#include "gf4_display_fake.h"
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
/* nouveau dispnv04/nvreg.h:329-330,348; dac.c:41-51,364-444. */
#define T_DAC_B 0x68252cU
#define T_TEST_B 0x682608U
#define T_TEST_VALUE 0x00100000U
#define T_ENABLE 1U
#define T_HEAD_B 0x100U
#define T_INITIAL 0xa5000040U /* arbitrary unrelated bits for RMW */
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_host_event events[64];
static struct gf3_host_reg regs[GF4_HEADS];
static int test(void)
{
    struct gf4_route wired={.connector=2,.head=0,.type=GF4_ANALOG,.or_mask=2,
        .i2c_index=1,.head_mask=3,.max_khz=200000};
    struct gf4_route route=wired;
    struct gfinit_mode mode={0};
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=sizeof(events)/sizeof(events[0]);
    host.base.regs=regs; host.base.cregs=sizeof(regs)/sizeof(regs[0]);
    host.ctx.routes=&wired; host.ctx.nroutes=1;
    route.head=1;
    REQUIRE(!gf4_route_check(&host.ctx,&route));
    route.head=GF4_HEADS;
    REQUIRE(gf4_route_check(&host.ctx,&route)==GF4_EINVAL);
    route.head=1;
    route.connector++;
    REQUIRE(gf4_route_check(&host.ctx,&route)==GF4_ENODEV);
    route=wired; route.head=1; route.max_khz++;
    REQUIRE(gf4_route_check(&host.ctx,&route)==GF4_ENODEV);
    route=wired; route.head=1;
    state.head[0].enabled=true; state.head[0].route=wired;
    REQUIRE(gf4_route_check(&host.ctx,&route)==GF4_EBUSY);
    state.head[0].enabled=false;
    wired.location=1; route.location=1;
    REQUIRE(gf4_route_check(&host.ctx,&route)==GF4_EUNSUPPORTED);
    wired.location=route.location=0;
    struct gf4_host_read reads[]={{0,T_DAC_B,4,T_INITIAL|T_ENABLE}};
    gf4_host_reads(&host,reads,1);
    REQUIRE(!gf4_dac_set(&host.ctx,&route,false));
    REQUIRE(host.nevents==2 && host.events[1].a[1]==T_DAC_B && host.events[1].a[3]==T_INITIAL);
    gf4_host_trace_clear(&host);
    state.next[0].enabled=true; state.next[0].dpms=GF4_ON; state.next[0].route=wired;
    host.ctx.changing=true;
    gf4_host_reads(&host,reads,1);
    REQUIRE(!gf4_dac_set(&host.ctx,&route,false));
    REQUIRE(host.events[1].a[3]==(T_INITIAL|T_ENABLE));
    state.next[0].enabled=false; host.ctx.changing=false;
    gf4_host_trace_clear(&host);
    struct gf4_host_read load_reads[]={
        {0,T_DAC_B,4,T_INITIAL|T_ENABLE}, {0,T_DAC_B,4,T_INITIAL},
        {0,T_DAC_B,4,T_INITIAL|T_HEAD_B|T_ENABLE}};
    gf4_host_reads(&host,load_reads,3);
    REQUIRE(!gf4_analog_load(&host.ctx,&route,&mode,0));
    REQUIRE(host.nevents==7);
    REQUIRE(host.events[1].a[3]==T_INITIAL);
    REQUIRE(host.events[3].a[3]==(T_INITIAL|T_HEAD_B|T_ENABLE));
    REQUIRE(host.events[4].a[1]==T_TEST_B && host.events[4].a[3]==T_TEST_VALUE);
    REQUIRE(host.events[6].a[3]==(T_INITIAL|T_HEAD_B|T_ENABLE));
    /* The load sequence is OFF R/W, select R/W, TEST W, ON R/W. */
    const enum gf4_host_op sequence[]={D_READ,D_WRITE,D_READ,D_WRITE,
                                       D_WRITE,D_READ,D_WRITE};
    struct gf4_state before;
    gf2_host_memcpy(&before,&state,sizeof(before));
    for (u32 failure=0;failure<sizeof(sequence)/sizeof(sequence[0]);failure++) {
        u32 nth=0;
        for (u32 i=0;i<=failure;i++) if (sequence[i]==sequence[failure]) nth++;
        gf4_host_trace_clear(&host);
        gf4_host_reads(&host,load_reads,sizeof(load_reads)/sizeof(load_reads[0]));
        gf4_host_fail_at(&host,D_READ,0,0);
        gf4_host_fail_at(&host,D_WRITE,0,0);
        gf4_host_fail_at(&host,sequence[failure],nth,GF2_EIO);
        REQUIRE(gf4_analog_load(&host.ctx,&route,&mode,0)==GF2_EIO);
        REQUIRE(host.nevents==failure+1U);
        for (u32 i=0;i<host.nevents;i++) REQUIRE(host.events[i].op==sequence[i]);
        REQUIRE(host.events[failure].rc==GF2_EIO);
        const u8 *actual=(const u8 *)&state,*expected=(const u8 *)&before;
        for (u32 i=0;i<sizeof(state);i++) REQUIRE(actual[i]==expected[i]);
    }
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
