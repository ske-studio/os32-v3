/*
 * Copyright 2012 Red Hat Inc.
 * Copyright 2012 Nouveau Community
 * Copyright 2012 The Nouveau community
 * Copyright 2014 Martin Peres
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
/* Independent golden values from MIT nouveau c03c1a1:
 * nvkm/subdev/therm/nv40.c:52-165, fanpwm.c:38-81, fantog.c:39-62,
 * base.c:37-173 and temp.c:26-209; bios/{therm.c:67-210,fan.c:29-93}.
 * The synthetic ROM and expected traces below are kept in-tree, with no
 * dependency on the reference checkout. No product formulas are imported.
 * UNIT 3 supplies every external boundary through the shared fixture. */
#define GF5_HOST_UNIT 3
#include "gf5_fake.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
/* Addresses/fields: reference nv40.c:52-165. */
#define T_ADC 0x15b0U
#define T_RAW 0x15b4U
#define T_FORBIDDEN 0x15b8U
#define T_PWM2 0x10f0U
#define T_PWM9 0x15f4U
#define T_DIV9 0x15f8U
#define T_ENABLE 0x80000000U
#define T_SETTLE 20000U /* nv40.c:52-69 OLD_STYLE sensor setup. */
#define T_NATIVE_WAIT_MAX 1000U /* native.c:68, GF_INIT_WAIT_CHUNK_US (PM22). */
#define T_POINTS_PER_STEP 3U /* therm/fan.c:61-70. */
#define T_US_PER_MS 1000U /* therm/fan.c:90, ms -> microseconds. */
#define T_PWM_FIELDS 0x7fff7fffU /* therm/nv40.c:110-117. */
#define T_FULL 100U /* therm/fan.c:37, percentage scale. */
#define T_POLL 1000000ULL /* base.c:151; temp.c:197. */
#define T_BUMP 500000ULL /* fan.c:189-190, milliseconds -> us. */
#define T_SLOW 2000000ULL /* therm/fan.c:193-194, ms -> us. */
#define T_PERIOD 100000ULL /* fantog.c:111. */
/* Arbitrary non-overlapping ROM layout, not hardware addresses. */
enum { R_BIT=0x20,R_P=0x80,R_THERM=0x100,R_PERF=0x300,R_FAN=0x340,R_SIZE=0x400 };
static struct gf5_host h;
static struct gf4_state display_state;
/* 20ms setup is split into <=50us waits, plus all control observations. */
static struct gf4_host_event devents[1024];
static struct gf5_host_event events[128];
static struct gf3_host_reg regs[16];
static u8 rom[R_SIZE],arena[4096] __attribute__((aligned(GF5_HOST_ALLOC_ALIGN)));
static u8 gpio_line;
static bool gpio_on;
static i32 gpio_source; /* Emulate B4 classified public GPIO failures. */
static u32 down_count,pause_count;
static bool down_active,pause_active;

static void put16(u32 at,u32 value) { rom[at]=(u8)value; rom[at+1]=(u8)(value>>8); }
static void put32(u32 at,u32 value) { put16(at,value); put16(at+2,value>>16); }
static void entry(u8 type,u32 value)
{
    u32 at=R_THERM+4U+rom[R_THERM+3U]*3U;
    assert(at+3U<=R_PERF);
    rom[at]=type; put16(at+1U,value); ++rom[R_THERM+3U];
}
/* Allowed fixture observer: successful GPIO stub output only. GPIO SET
 * changes the test's physical line, independently from fan_saved fields. */
static void observe(struct gf5_host *host,const struct gf5_host_event *e)
{
    if (e->rc) {
        if (gpio_source && (e->op==F5_GPIO_FIND || e->op==F5_GPIO_GET || e->op==F5_GPIO_SET))
            host->ctx.source_rc=gpio_source;
        return;
    }
    if (e->op==F5_GPIO_FIND) {
        assert(e->a[0]==0x09U);
        *(u8 *)e->p[1]=gpio_line;
        u8 *log=(void *)e->p[2]; log[0]=0; log[1]=1;
    } else if (e->op==F5_GPIO_GET) *(bool *)e->p[2]=gpio_on;
    else if (e->op==F5_GPIO_SET) gpio_on=e->a[1];
}
/* Actual registered emergency consumers, not replacement product APIs. */
static void down(void *owner,bool active)
{
    assert(owner==&h); ++down_count; down_active=active;
}
static void pause_gpu(void *owner,bool active)
{
    assert(owner==&h); ++pause_count; pause_active=active;
}
static void reset(void)
{
    gf5_host_init(&h,&display_state);
    h.events=events; h.cevents=COUNT(events);
    h.display.events=devents; h.display.cevents=COUNT(devents);
    h.display.base.regs=regs; h.display.base.cregs=COUNT(regs);
    h.observe=observe; gpio_line=2; gpio_on=false; gpio_source=0;
    h.ctx.deadline=0x12345678U; /* Fan operations must leave this untouched. */
    gf5_host_arena(&h,arena,sizeof(arena));
    gf2_host_memset(rom,0,sizeof(rom));
    const u8 bit[]={0xff,0xb8,'B','I','T',0,0,0,12,6,1,0,'P',2,0x5c,0,R_P,0};
    gf2_host_memcpy(rom+R_BIT,bit,sizeof(bit));
    put32(R_P,R_PERF); put32(R_P+16,R_THERM);
    rom[R_THERM]=0x10; rom[R_THERM+1]=4; rom[R_THERM+2]=3;
    rom[R_PERF]=0x21; rom[R_PERF+1]=8; put16(R_PERF+6,100);
    /* raw temperature in Celsius: slope 1, offset 8 cancels NV40 bias. */
    entry(0x00,0); entry(0x01,0); entry(0x10,8); entry(0x11,1);
    entry(0x12,1); entry(0x13,1);
    h.ctx.rom=(struct gf5_rom_view){.bytes=rom,.size=sizeof(rom)};
    gf3_host_set(&h.display.base,0,T_RAW,60);
    /* enabled, divisor100, inverted duty80 => physical duty20%. */
    gf3_host_set(&h.display.base,0,T_PWM2,T_ENABLE|0x00500064U);
    down_count=pause_count=0; down_active=pause_active=false;
    h.ctx.emergency=(struct gf5_emergency_ops){down,pause_gpu,&h};
}
static void ready(void)
{
    struct gf5_thermal t;
    assert(!gf5_therm_read(&h.ctx,&t)); assert(t.calibrated && t.milli_c==60000);
    gf5_host_trace_clear(&h);
}
static u32 reg_value(u32 off)
{
    for (u32 i=0;i<h.display.base.nregs;++i)
        if (regs[i].off==off) return regs[i].value;
    return 0;
}
static void no_forbidden(void)
{
    for (u32 i=0;i<h.display.nevents;++i) {
        struct gf4_host_event *e=&devents[i];
        if (e->op==D_READ || e->op==D_WRITE) {
            assert(e->a[0]==0 && e->a[2]==4 && e->a[1]!=T_FORBIDDEN);
        }
    }
    assert(h.ctx.deadline==0x12345678U && !h.calls[F5_PAUSE] && !h.calls[F5_RESUME]);
}
static void test_temperature(void)
{
    reset();
    /* Independent worked example, nv40.c:95-100:
     * 59*3/2=88, 5/2=2, constant=3, correction=-8 => 85 C. */
    put16(R_THERM+4+2*3+1,5); put16(R_THERM+4+3*3+1,2);
    put16(R_THERM+4+4*3+1,3); put16(R_THERM+4+5*3+1,2);
    rom[R_THERM+4+1*3+2]=6;
    gf3_host_set(&h.display.base,0,T_RAW,0xab00U|59U);
    struct gf5_thermal t={0};
    assert(!gf5_therm_read(&h.ctx,&t));
    assert(t.raw==59 && t.calibrated && t.milli_c==85000);
    assert(h.now==T_SETTLE && devents[0].op==D_WRITE && devents[0].a[1]==T_ADC && devents[0].a[3]==0xff);
    u32 settle=0;
    for (u32 i=0;i<h.display.nevents;++i) if (devents[i].op==D_SLEEP) {
        assert(devents[i].a[0] && devents[i].a[0]<=T_NATIVE_WAIT_MAX);
        settle+=devents[i].a[0];
    }
    assert(settle==T_SETTLE && h.ctx.fan.sensor_setup==0xff);
    gf5_host_trace_clear(&h);
    assert(!gf5_therm_read(&h.ctx,&t));
    assert(h.display.nevents==2 && devents[0].op==D_WRITE && devents[1].op==D_READ);
    no_forbidden();
    h.ctx.fan.offset_num=-100;
    assert(!gf5_therm_read(&h.ctx,&t)); assert(t.milli_c==33000);
    h.ctx.fan.offset_constant=-100;
    assert(!gf5_therm_read(&h.ctx,&t)); assert(t.milli_c==0);
    i32 *coeff[]={&h.ctx.fan.slope_mult,&h.ctx.fan.slope_div,&h.ctx.fan.offset_num,&h.ctx.fan.offset_den};
    for (u32 i=0;i<COUNT(coeff);++i) {
        i32 saved=*coeff[i]; *coeff[i]=0; t.calibrated=true;
        assert(gf5_therm_read(&h.ctx,&t)==GF5_ENOTSUP);
        assert(t.raw==59 && !t.calibrated && !t.milli_c); *coeff[i]=saved;
    }
    gf5_host_trace_clear(&h);
    gf4_host_fail_at(&h.display,D_WRITE,1,-17);
    t=(struct gf5_thermal){123,456,true};
    assert(gf5_therm_read(&h.ctx,&t)==GF5_EIO && h.ctx.source_rc==-17);
    assert(h.display.nevents==1 && t.raw==123 && t.milli_c==456 && t.calibrated);
    gf4_host_fail_at(&h.display,D_WRITE,0,0); gf5_host_trace_clear(&h);
    gf4_host_fail_at(&h.display,D_READ,1,-18);
    assert(gf5_therm_read(&h.ctx,&t)==GF5_EIO && h.ctx.source_rc==-18);
    assert(h.display.nevents==2 && t.raw==123);
    assert(gf5_therm_read(0,&t)==GF5_EINVAL); /* no I/O. */
    reset(); ready(); h.ctx.fan.slope_mult=32767; h.ctx.fan.slope_div=1;
    gf3_host_set(&h.display.base,0,T_RAW,255);
    assert(gf5_therm_read(&h.ctx,&t)==GF5_ERANGE && !t.calibrated);
}
static void test_policy(void)
{
    reset();
    entry(0x08,(80U<<4)|1U); /* minimum threshold hysteresis becomes2. */
    entry(0x22,(90U<<8)|10U); entry(0x3b,123); entry(0x3c,456);
    entry(0x24,(4U<<12)|(50U<<4)|3U); /* LUT[4]=40 */
    entry(0x24,(8U<<12)|(70U<<4)|5U); entry(0x25,77);
    entry(0x01,0); entry(0x12,99); /* Second sensor cannot replace slope1. */
    ready();
    assert(h.ctx.fan.min_duty==10 && h.ctx.fan.max_duty==90 && h.ctx.fan.hyst[0]==2);
    assert(h.ctx.fan.threshold[0]==80 && h.ctx.fan.bump_ms==123 && h.ctx.fan.slow_ms==456);
    assert(h.ctx.fan.slope_mult==1 && h.ctx.fan.ntrip==2 && h.live_allocs==1);
    assert(h.ctx.fan.trip[0].temp==50 && h.ctx.fan.trip[0].hyst==3 && h.ctx.fan.trip[0].duty==40);
    assert(h.ctx.fan.trip[1].temp==70 && h.ctx.fan.trip[1].duty==77);
    reset(); put16(R_BIT+14,19); /* P v2 needs20 bytes for the thermal pointer. */
    struct gf5_thermal t;
    assert(gf5_therm_read(&h.ctx,&t)==GF5_EBIOS && !h.display.nevents && !h.live_allocs);
    reset(); rom[R_THERM+2]=2;
    assert(gf5_therm_read(&h.ctx,&t)==GF5_EBIOS && !h.display.nevents);
    reset(); put32(R_P+16,R_SIZE-2U);
    assert(gf5_therm_read(&h.ctx,&t)==GF5_EBIOS && !h.display.nevents);
    reset(); h.ctx.rom.size=R_THERM+4+6*3-1;
    assert(gf5_therm_read(&h.ctx,&t)==GF5_EBIOS && !h.display.nevents);
    reset(); entry(0x25,77);
    assert(gf5_therm_read(&h.ctx,&t)==GF5_EBIOS && !h.display.nevents);
    reset(); entry(0x24,(50U<<4)|3U);
    gf5_host_fail_at(&h,F5_ALLOC,1,GF5_ENOMEM);
    assert(gf5_therm_read(&h.ctx,&t)==GF5_ENOMEM && !h.display.nevents && !h.live_allocs);
    assert(!h.ctx.fan.trip && !h.ctx.fan.sensor_setup);
    reset(); entry(0x24,(50U<<4)|3U);
    gf4_host_fail_at(&h.display,D_SLEEP,2,-19);
    assert(gf5_therm_read(&h.ctx,&t)==GF5_EIO && h.ctx.source_rc==-19);
    assert(h.display.nevents==3 && !h.live_allocs && h.calls[F5_FREE]==1);
    assert(!h.ctx.fan.trip && !h.ctx.fan.sensor_setup);
    /* New fan table entry0 wins over fallback thermal fan values. */
    reset(); put32(R_P+0x58,R_FAN);
    rom[R_FAN]=0x10; rom[R_FAN+1]=4; rom[R_FAN+2]=15; rom[R_FAN+3]=1;
    rom[R_FAN+4]=1; rom[R_FAN+6]=15; rom[R_FAN+7]=85; put32(R_FAN+4+11,0xab000002U);
    entry(0x22,(40U<<8)|30U); entry(0x24,0);
    ready();
    assert(h.ctx.fan.kind==GF5_FAN_LINEAR && h.ctx.fan.min_duty==15 && h.ctx.fan.max_duty==85);
    assert(h.ctx.fan.freq==2 && !h.ctx.fan.ntrip && !h.live_allocs);
    u32 duty;
    assert(!gf5_fan_get(&h.ctx,&duty) && duty==20);
    /* freq2 -> divisor0, readback falls back to GPIO, never a made-up1. */
    h.ctx.fan.kind=GF5_FAN_OTHER; h.ctx.fan.mode=GF5_FAN_AUTO;
    assert(!gf5_fan_cstate(&h.ctx,60,0));
    assert(!(reg_value(T_PWM2)&0x7fff7fffU));
    gpio_on=true; assert(!gf5_fan_get(&h.ctx,&duty) && duty==100);
    no_forbidden();
    /* BIT P v1 thermal offset+12, as distinct from v2+16. */
    reset(); rom[R_BIT+13]=1; put32(R_P+12,R_THERM); put32(R_P+16,R_SIZE-1);
    ready(); assert(h.ctx.fan.divisor==100);
    /* BMP performance headers reverse ver/hdr, perf.c:65-78. */
    reset(); rom[R_BIT]=0;
    enum { BMP=0x180 };
    const u8 bmp[]={0xff,0x7f,'N','V',0,0,0x25};
    gf2_host_memcpy(rom+BMP,bmp,sizeof(bmp)); put16(BMP+0x94,R_PERF);
    rom[R_PERF]=8; rom[R_PERF+1]=0x21;
    assert(gf5_therm_read(&h.ctx,&t)==GF5_ENOTSUP && t.raw==60 && !t.calibrated);
    assert(h.ctx.fan.divisor==100 && h.ctx.fan.sensor_setup==0xff);
    /* Fan entry type0 chooses toggle even on a PWM-capable GPIO. */
    reset(); put32(R_P+0x58,R_FAN);
    rom[R_FAN]=0x10; rom[R_FAN+1]=4; rom[R_FAN+2]=15; rom[R_FAN+3]=1;
    rom[R_FAN+4]=0; rom[R_FAN+7]=100;
    ready(); assert(!gf5_fan_get(&h.ctx,&duty));
    assert(h.ctx.fan.method==GF5_FAN_TOGGLE && !(reg_value(T_PWM2)&T_ENABLE));
}
static void test_pwm(void)
{
    reset(); ready(); u32 duty;
    assert(!gf5_fan_get(&h.ctx,&duty) && duty==20);
    h.ctx.fan.kind=GF5_FAN_OTHER;
    assert(!gf5_fan_cstate(&h.ctx,80,0));
    assert(h.ctx.fan.cstate==80 && h.ctx.fan.target==80);
    /* max3 points:20->23%, inverted duty77, divisor100. */
    assert(reg_value(T_PWM2)==(T_ENABLE|0x004d0064U));
    assert(h.ctx.next_step==h.now+T_BUMP && !h.ctx.next_auto);
    u32 writes=h.display.calls[D_WRITE];
    assert(!gf5_fan_cstate(&h.ctx,70,1));
    assert(h.ctx.fan.cstate==80 && h.display.calls[D_WRITE]==writes);
    assert(!gf5_fan_cstate(&h.ctx,90,-1)); assert(h.ctx.fan.cstate==80);
    h.ctx.fan.sensor_poll=false;
    h.now=h.ctx.next_step-1; gf5_fan_tick(&h.ctx,h.now);
    assert(h.display.calls[D_WRITE]==writes);
    ++h.now; gf5_fan_tick(&h.ctx,h.now);
    assert(reg_value(T_PWM2)==(T_ENABLE|0x004a0064U)); /*23->26*/
    assert(!gf5_fan_cstate(&h.ctx,10,-1));
    assert(reg_value(T_PWM2)==(T_ENABLE|0x004d0064U)); /*26->23*/
    assert(h.ctx.next_step==h.now+T_SLOW);
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_NONE));
    assert(!h.ctx.next_step && !h.ctx.next_toggle && !h.ctx.next_auto);
    writes=h.display.calls[D_WRITE]; h.now+=T_SLOW; gf5_fan_tick(&h.ctx,h.now);
    assert(h.display.calls[D_WRITE]==writes);
    /* MANUAL clamps current duty to ROM range and stops AUTO. */
    h.ctx.fan.min_duty=30; h.ctx.fan.max_duty=90;
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_MANUAL));
    assert(reg_value(T_PWM2)==(T_ENABLE|0x00460064U)); /*30%*/
    assert(h.ctx.fan.mode==GF5_FAN_MANUAL && !h.ctx.next_auto);
    assert(gf5_fan_cstate(&h.ctx,101,0)==GF5_EINVAL);
    assert(gf5_fan_mode_set(&h.ctx,(enum gf5_fan_mode)3)==GF5_EINVAL);
    no_forbidden();
    /* line9 full-word divisor/readback and ordered data then enable. */
    reset(); ready(); gpio_line=9;
    gf3_host_set(&h.display.base,0,T_PWM9,T_ENABLE|80U);
    gf3_host_set(&h.display.base,0,T_DIV9,100);
    assert(!gf5_fan_get(&h.ctx,&duty) && duty==20);
    h.ctx.fan.kind=GF5_FAN_OTHER;
    gf5_host_trace_clear(&h);
    assert(!gf5_fan_cstate(&h.ctx,80,0));
    const u32 off[]={T_DIV9,T_PWM9,T_PWM9};
    const u32 value[]={100,T_ENABLE|77U,T_ENABLE|77U};
    u32 n=0;
    for (u32 i=0;i<h.display.nevents;++i) if (devents[i].op==D_WRITE) {
        assert(n<COUNT(off) && devents[i].a[1]==off[n] && devents[i].a[3]==value[n]); ++n;
    }
    assert(n==COUNT(off));
    gf3_host_set(&h.display.base,0,T_DIV9,10);
    gf3_host_set(&h.display.base,0,T_PWM9,T_ENABLE|20U);
    assert(!gf5_fan_get(&h.ctx,&duty) && duty==0); /* max(divisor,duty). */
    gf3_host_set(&h.display.base,0,T_PWM9,0); gpio_on=true;
    assert(!gf5_fan_get(&h.ctx,&duty) && duty==100);
}
static void test_failures(void)
{
    /* Every line9 programming boundary fails without later writes. A
     * failed readback is explicitly allowed to fall back to target. */
    for (u32 fail=1;fail<=3;++fail) {
        reset(); ready(); gpio_line=9; u32 duty;
        assert(!gf5_fan_get(&h.ctx,&duty));
        h.ctx.fan.kind=GF5_FAN_OTHER;
        gf5_host_trace_clear(&h);
        gf4_host_fail_at(&h.display,D_WRITE,fail,-31);
        assert(gf5_fan_cstate(&h.ctx,80,0)==GF5_EIO);
        assert(h.ctx.source_rc==-31 && h.display.calls[D_WRITE]==fail);
        assert(devents[h.display.nevents-1].rc==-31 && !h.ctx.next_step);
        assert(h.ctx.rom.bytes==rom && !h.ctx.image && h.ctx.phase==GF5_RUN);
        no_forbidden();
    }
    reset(); ready(); u32 duty=123;
    gf5_host_fail_at(&h,F5_GPIO_FIND,1,GF5_ENOTSUP);
    assert(gf5_fan_get(&h.ctx,&duty)==GF5_ENOTSUP && duty==123);
    assert(!h.display.nevents && !h.calls[F5_GPIO_GET] && !h.calls[F5_GPIO_SET]);
    reset(); ready();
    gf5_host_fail_at(&h,F5_CLOCK64,1,-32);
    assert(gf5_fan_mode_set(&h.ctx,GF5_FAN_MANUAL)==GF5_EIO && h.ctx.source_rc==-32);
    assert(!h.calls[F5_GPIO_FIND] && !h.display.nevents && h.ctx.fan.mode==GF5_FAN_AUTO);
    reset(); ready(); gpio_line=8;
    assert(gf5_fan_get(&h.ctx,&duty)==GF5_ENOTSUP && !h.display.nevents);
    reset(); ready(); h.ctx.fan.slope_mult=0;
    assert(gf5_fan_mode_set(&h.ctx,GF5_FAN_AUTO)==GF5_EINVAL);
    reset(); ready();
    gf3_host_set(&h.display.base,0,T_PWM2,0);
    gf5_host_fail_at(&h,F5_GPIO_GET,1,-33);
    assert(gf5_fan_get(&h.ctx,&duty)==GF5_EIO && h.ctx.source_rc==-33);
    assert(duty==123 && !h.calls[F5_GPIO_SET]);
    /* MANUAL read failure uses100% immediately, with the ROM clamp. */
    reset(); ready(); assert(!gf5_fan_get(&h.ctx,&duty));
    h.ctx.fan.max_duty=90;
    gf4_host_fail_at(&h.display,D_READ,0,-34);
    /* RMW also needs reads, so fail only the first (readback) boundary. */
    gf5_host_trace_clear(&h); gf4_host_fail_at(&h.display,D_READ,1,-34);
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_MANUAL));
    assert(h.ctx.fan.duty==90 && reg_value(T_PWM2)==(T_ENABLE|0x000a0064U));
}
static void test_auto(void)
{
    reset(); entry(0x22,(90U<<8)|10U); entry(0x46,(80U<<8)|40U); ready();
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_AUTO));
    assert(h.ctx.fan.kind==GF5_FAN_LINEAR && h.ctx.fan.target==50);
    assert(h.ctx.next_auto==h.now+T_POLL);
    h.ctx.fan.sensor_poll=false; h.ctx.next_step=0;
    gf3_host_set(&h.display.base,0,T_RAW,70);
    u32 reads=h.display.calls[D_READ];
    h.now=h.ctx.next_auto-1; gf5_fan_tick(&h.ctx,h.now);
    assert(h.display.calls[D_READ]==reads && h.ctx.fan.target==50);
    ++h.now; gf5_fan_tick(&h.ctx,h.now); assert(h.ctx.fan.target==70);
    /* perf cstate must not overwrite temperature-derived LINEAR target. */
    assert(!gf5_fan_cstate(&h.ctx,99,0)); assert(h.ctx.fan.target==70);
    gf3_host_set(&h.display.base,0,T_RAW,10);
    assert(!gf5_fan_cstate(&h.ctx,99,0)); assert(h.ctx.fan.target==10);
    gf3_host_set(&h.display.base,0,T_RAW,100);
    assert(!gf5_fan_cstate(&h.ctx,99,0)); assert(h.ctx.fan.target==90);
    /* OTHER uses cstate only if nonzero; zero falls back30..fanboost90. */
    h.ctx.fan.kind=GF5_FAN_OTHER;
    assert(!gf5_fan_cstate(&h.ctx,60,0)); assert(h.ctx.fan.target==60 && !h.ctx.next_auto);
    gf3_host_set(&h.display.base,0,T_RAW,60);
    assert(!gf5_fan_cstate(&h.ctx,0,0)); assert(h.ctx.fan.target==50 && h.ctx.next_auto);
    /* TRIP selection and the strictly-open lower hysteresis edge. */
    reset(); entry(0x24,(4U<<12)|(50U<<4)|3U);
    entry(0x24,(8U<<12)|(70U<<4)|5U); ready();
    gf3_host_set(&h.display.base,0,T_RAW,70);
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_AUTO));
    assert(h.ctx.fan.target==75 && h.ctx.fan.last_trip==1);
    gf3_host_set(&h.display.base,0,T_RAW,66);
    assert(!gf5_fan_cstate(&h.ctx,99,0)); assert(h.ctx.fan.target==75 && h.ctx.fan.last_trip==1);
    gf3_host_set(&h.display.base,0,T_RAW,65);
    assert(!gf5_fan_cstate(&h.ctx,99,0)); assert(h.ctx.fan.target==40 && h.ctx.fan.last_trip==0);
    gf3_host_set(&h.display.base,0,T_RAW,46);
    assert(!gf5_fan_cstate(&h.ctx,99,0)); assert(!h.ctx.fan.target && h.ctx.fan.last_trip==-1);
}
static void test_sensor(void)
{
    reset(); ready();
    /* Avoid unrelated AUTO/step I/O; thresholds still poll in NONE. */
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_NONE));
    gf3_host_set(&h.display.base,0,T_RAW,106);
    gf5_fan_tick(&h.ctx,h.now);
    assert(h.ctx.fan.active[0] && h.ctx.fan.active[1] && h.ctx.fan.active[2] && !h.ctx.fan.active[3]);
    assert(down_count==1 && down_active && pause_count==1 && pause_active);
    assert(h.ctx.next_sensor==h.now+T_POLL && h.ctx.fan.mode==GF5_FAN_AUTO);
    /* fanboost issued immediate100% (inverted duty0) before AUTO. */
    bool boosted=false;
    for (u32 i=0;i<h.display.nevents;++i)
        if (devents[i].op==D_WRITE && devents[i].a[1]==T_PWM2 && devents[i].a[3]==(T_ENABLE|100U)) boosted=true;
    assert(boosted);
    h.now=h.ctx.next_sensor; gf5_fan_tick(&h.ctx,h.now);
    assert(down_count==1 && pause_count==1);
    gf3_host_set(&h.display.base,0,T_RAW,101); h.now=h.ctx.next_sensor; gf5_fan_tick(&h.ctx,h.now);
    assert(pause_count==1); /* critical105,hyst5:101 stays active. */
    gf3_host_set(&h.display.base,0,T_RAW,100); h.now=h.ctx.next_sensor; gf5_fan_tick(&h.ctx,h.now);
    assert(pause_count==2 && !pause_active);
    gf3_host_set(&h.display.base,0,T_RAW,92); h.now=h.ctx.next_sensor; gf5_fan_tick(&h.ctx,h.now);
    assert(down_count==2 && !down_active);
    gf3_host_set(&h.display.base,0,T_RAW,87); h.now=h.ctx.next_sensor; gf5_fan_tick(&h.ctx,h.now);
    assert(!h.ctx.fan.active[0]);
    gf3_host_set(&h.display.base,0,T_RAW,135); h.now=h.ctx.next_sensor; gf5_fan_tick(&h.ctx,h.now);
    assert(h.ctx.fan.active[3] && h.ctx.fan.target==100);
    assert(reg_value(T_PWM2)==(T_ENABLE|100U));
    no_forbidden();
    /* Failed sensor read stops polling, preserving raw source and state. */
    gf5_host_trace_clear(&h); gf4_host_fail_at(&h.display,D_READ,1,-41);
    bool was=h.ctx.fan.active[3]; h.now=h.ctx.next_sensor;
    gf5_fan_tick(&h.ctx,h.now);
    assert(h.ctx.last_rc==GF5_EIO && h.ctx.source_rc==-41 && !h.ctx.fan.sensor_poll && !h.ctx.next_sensor);
    assert(h.ctx.fan.active[3]==was && devents[1].rc==-41);
    /* A fanless board must still notify registered emergency consumers. */
    reset(); ready(); gf5_host_fail_at(&h,F5_GPIO_FIND,0,GF5_ENOTSUP);
    gf3_host_set(&h.display.base,0,T_RAW,106); gf5_fan_tick(&h.ctx,h.now);
    assert(down_count==1 && pause_count==1 && h.ctx.last_rc==GF5_ENOTSUP);
}
static void test_toggle_and_wrap(void)
{
    reset(); ready();
    /* Seed a saved toggle policy: public ABI restoration uses these same
     * fields. Its method/table decoding is covered by the fan table case. */
    h.ctx.fan.method=GF5_FAN_TOGGLE; h.ctx.fan.kind=GF5_FAN_OTHER;
    /* Real full-width clock crossing the low-word wrap. */
    h.now=(1ULL<<32)-20000ULL;
    h.ctx.fan.min_duty=h.ctx.fan.max_duty=50;
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_MANUAL));
    assert(h.ctx.fan.duty==50 && h.ctx.fan.toggle_on && gpio_on);
    assert(!(reg_value(T_PWM2)&T_ENABLE));
    assert(h.ctx.next_toggle==h.now+T_PERIOD/2 && h.ctx.next_toggle>(1ULL<<32));
    u64 until=h.ctx.next_toggle;
    h.ctx.fan.sensor_poll=false; h.ctx.next_step=0;
    h.now=until-1; u32 sets=h.calls[F5_GPIO_SET]; gf5_fan_tick(&h.ctx,h.now);
    assert(h.calls[F5_GPIO_SET]==sets);
    ++h.now; gf5_fan_tick(&h.ctx,h.now);
    assert(!gpio_on && !h.ctx.fan.toggle_on && h.ctx.next_toggle==h.now+T_PERIOD/2);
    gf5_host_fail_at(&h,F5_GPIO_GET,0,-51); gf5_host_trace_clear(&h);
    h.now=h.ctx.next_toggle;
    gf5_fan_tick(&h.ctx,h.now);
    assert(h.ctx.last_rc==GF5_EIO && h.ctx.source_rc==-51 && !h.calls[F5_GPIO_SET]);
    assert(!h.ctx.next_toggle);
    gf5_host_fail_at(&h,F5_GPIO_GET,0,0);
    gf5_host_fail_at(&h,F5_GPIO_SET,0,-52); gf5_host_trace_clear(&h);
    h.ctx.next_toggle=h.now; /* A separate pending alarm for SET injection. */
    gf5_fan_tick(&h.ctx,h.now);
    assert(h.ctx.last_rc==GF5_EIO && h.ctx.source_rc==-52 && !gpio_on && !h.ctx.next_toggle);
    gf5_host_fail_at(&h,F5_GPIO_SET,0,0);
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_NONE));
    assert(!h.ctx.next_toggle);
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_MANUAL));
    assert(h.ctx.next_toggle && gpio_on);
    /* Bring the line to off before checking the zero endpoint. */
    h.now=h.ctx.next_toggle; gf5_fan_tick(&h.ctx,h.now); assert(!gpio_on);
    /* Endpoint0 toggles once to on, then to off at zero delay and stops. */
    h.ctx.fan.mode=GF5_FAN_MANUAL; h.ctx.fan.min_duty=0; h.ctx.fan.max_duty=0;
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_MANUAL));
    gf5_fan_tick(&h.ctx,h.now);
    assert(!gpio_on && !h.ctx.next_toggle && !h.ctx.fan.duty);
    no_forbidden();
}
/* A bad temperature read must not gate independent step/toggle alarms.
 * base.c:128-151, fan.c:61-93, fantog.c:39-62 have separate workers. */
static void test_independent_alarms(void)
{
    reset(); ready();
    h.ctx.fan.kind=GF5_FAN_OTHER;
    assert(!gf5_fan_cstate(&h.ctx,80,0));
    h.ctx.fan.kind=GF5_FAN_LINEAR; h.ctx.fan.sensor_poll=false;
    for (u32 i=0;i<2;++i) {
        h.now=h.ctx.next_step; h.ctx.next_auto=h.now;
        u32 before=h.ctx.fan.duty;
        gf5_host_trace_clear(&h);
        gf4_host_fail_at(&h.display,D_READ,1,-61);
        gf5_fan_tick(&h.ctx,h.now);
        /* fan.c:61-70: each due step increases by three points. */
        assert(h.ctx.fan.duty==before+T_POINTS_PER_STEP && h.ctx.next_step>h.now);
        assert(h.ctx.last_rc==GF5_EIO && h.ctx.source_rc==-61);
    }
    reset(); ready(); h.ctx.fan.method=GF5_FAN_TOGGLE;
    h.ctx.fan.min_duty=h.ctx.fan.max_duty=T_FULL/2;
    assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_MANUAL));
    h.ctx.fan.sensor_poll=false;
    h.now=h.ctx.next_toggle; gf5_fan_tick(&h.ctx,h.now); assert(!gpio_on);
    h.ctx.fan.mode=GF5_FAN_AUTO; h.ctx.fan.kind=GF5_FAN_LINEAR;
    for (u32 i=0;i<2;++i) {
        h.now=h.ctx.next_toggle; h.ctx.next_auto=h.now;
        bool was=gpio_on;
        gf5_host_trace_clear(&h);
        gf4_host_fail_at(&h.display,D_READ,1,-62);
        gf5_fan_tick(&h.ctx,h.now);
        assert(gpio_on!=was && h.calls[F5_GPIO_SET]==1);
        assert(h.ctx.next_toggle==h.now+T_PERIOD/2);
        assert(h.ctx.last_rc==GF5_EIO && h.ctx.source_rc==-62);
    }
    /* Preserve the first error even when a later GPIO boundary also fails. */
    h.now=h.ctx.next_toggle; h.ctx.next_auto=h.now;
    gf5_host_trace_clear(&h); gf4_host_fail_at(&h.display,D_READ,1,-62);
    gpio_source=-63; gf5_host_fail_at(&h,F5_GPIO_GET,1,GF5_EIO);
    gf5_fan_tick(&h.ctx,h.now);
    assert(h.ctx.source_rc==-62 && h.ctx.last_rc==GF5_EIO && !h.calls[F5_GPIO_SET]);
}
static void test_boost_auto(void)
{
    reset(); ready(); h.ctx.fan.kind=GF5_FAN_OTHER;
    assert(!gf5_fan_cstate(&h.ctx,60,0));
    assert(h.ctx.fan.mode==GF5_FAN_AUTO && h.ctx.fan.cstate);
    gf3_host_set(&h.display.base,0,T_RAW,h.ctx.fan.threshold[0]);
    gf5_host_trace_clear(&h); gf5_fan_tick(&h.ctx,h.now);
    /* temp.c:95-100 + base.c:208: immediate full duty survives same AUTO. */
    assert(h.ctx.fan.target==T_FULL && h.ctx.fan.duty==T_FULL);
    assert(!h.ctx.next_auto && !h.ctx.next_step && h.ctx.fan.mode==GF5_FAN_AUTO);
    assert(h.ctx.fan.cstate==60 && h.ctx.fan.active[0]);
    no_forbidden();
}
static void test_gpio_source(void)
{
    const enum gf5_host_op op[]={F5_GPIO_FIND,F5_GPIO_GET,F5_GPIO_SET};
    for (u32 i=0;i<COUNT(op);++i) {
        reset(); ready(); u32 duty=123;
        if (op[i]==F5_GPIO_GET) gf3_host_set(&h.display.base,0,T_PWM2,0);
        if (op[i]==F5_GPIO_SET) {
            h.ctx.fan.method=GF5_FAN_TOGGLE;
            h.ctx.fan.min_duty=h.ctx.fan.max_duty=T_FULL/2;
        }
        /* Raw -5 overlaps a lower-layer timeout enum. B4's published
         * EIO must survive, independently of that original source value. */
        gpio_source=-5;
        gf5_host_fail_at(&h,op[i],1,GF5_EIO);
        i32 rc=op[i]==F5_GPIO_SET ? gf5_fan_mode_set(&h.ctx,GF5_FAN_MANUAL) :
                                  gf5_fan_get(&h.ctx,&duty);
        assert(rc==GF5_EIO && h.ctx.source_rc==gpio_source);
        assert(h.calls[op[i]]==1 && events[h.nevents-1].op==op[i]);
        if (op[i]!=F5_GPIO_SET) assert(duty==123 && !h.calls[F5_GPIO_SET]);
        assert(h.ctx.rom.bytes==rom && !h.ctx.image && !h.live_allocs);
    }
}
static void test_resume(void)
{
    for (u32 mode=GF5_FAN_NONE;mode<=GF5_FAN_AUTO;++mode) {
        reset(); entry(0x24,(4U<<12)|(50U<<4)|3U); ready();
        struct gf5_fan_trip *owned=h.ctx.fan.trip;
        struct gf5_fan_trip copy=*owned;
        struct gf5_fan_saved saved=h.ctx.fan;
        saved.trip=&copy; saved.mode=mode; saved.duty=T_FULL/2;
        saved.kind=GF5_FAN_OTHER; saved.cstate=80; saved.active[0]=true;
        owned->temp=0; h.ctx.fan.offset_constant=99;
        h.ctx.next_sensor=h.ctx.next_auto=h.ctx.next_step=h.ctx.next_toggle=1;
        h.now=1ULL<<32;
        gf5_host_trace_clear(&h);
        assert(!gf5_therm_resume(&h.ctx,&saved));
        u64 now=h.now;
        assert(now==(1ULL<<32)+T_SETTLE && h.ctx.next_sensor==now+T_POLL);
        assert(devents[0].op==D_WRITE && devents[0].a[1]==T_ADC);
        u32 settle=0,first_fan=h.display.nevents;
        for (u32 i=0;i<h.display.nevents;++i) {
            if (devents[i].op==D_SLEEP) {
                assert(devents[i].a[0] && devents[i].a[0]<=T_NATIVE_WAIT_MAX);
                settle+=devents[i].a[0];
            }
            if (first_fan==h.display.nevents && devents[i].op==D_WRITE &&
                devents[i].a[1]==T_PWM2) first_fan=i;
        }
        assert(settle==T_SETTLE && h.ctx.fan.mode==saved.mode);
        assert(h.ctx.fan.trip==owned && owned->temp==copy.temp && owned->hyst==copy.hyst && owned->duty==copy.duty);
        assert(h.ctx.fan.offset_constant==saved.offset_constant && h.ctx.fan.active[0]);
        assert(!h.calls[F5_ALLOC] && !h.calls[F5_FREE] && h.live_allocs==1);
        assert(saved.trip==&copy && saved.duty==T_FULL/2 && saved.mode==mode);
        if (mode==GF5_FAN_NONE) {
            assert(first_fan==h.display.nevents && !h.ctx.next_step && !h.ctx.next_toggle && !h.ctx.next_auto);
        } else {
            /* divisor100, 50% inverted => high word50, low word100.
             * nv40.c:110-117; restore duty precedes AUTO's 3 point step. */
            assert(first_fan<h.display.nevents);
            assert((devents[first_fan].a[3]&T_PWM_FIELDS)==((T_FULL/2)<<16|T_FULL));
            if (mode==GF5_FAN_AUTO) {
                assert(h.ctx.fan.duty==saved.duty+T_POINTS_PER_STEP && h.ctx.fan.target==saved.cstate);
                assert(h.ctx.next_step==now+(u64)saved.bump_ms*T_US_PER_MS);
            } else assert(h.ctx.fan.duty==saved.duty && !h.ctx.next_step);
        }
        no_forbidden();
    }
    /* Toggle restoration re-arms a fractional duty against the new clock. */
    reset(); ready();
    struct gf5_fan_saved saved=h.ctx.fan;
    saved.mode=GF5_FAN_MANUAL; saved.method=GF5_FAN_TOGGLE; saved.duty=T_FULL/2;
    assert(saved.gpio_line==U8_MAX); /* Exercise GPIO resolution in prepare. */
    assert(!gf5_therm_resume(&h.ctx,&saved));
    assert(h.ctx.fan.duty==saved.duty && h.ctx.fan.mode==saved.mode);
    assert(gpio_on && h.ctx.next_toggle==h.now+T_PERIOD/2);
    /* Reject mismatched trip counts before setup, retaining ownership. */
    reset(); entry(0x24,(50U<<4)|3U); ready(); saved=h.ctx.fan;
    struct gf5_fan_trip *owned=h.ctx.fan.trip;
    ++saved.ntrip;
    assert(gf5_therm_resume(&h.ctx,&saved)==GF5_ESTATE);
    assert(!h.display.nevents && h.ctx.fan.trip==owned && h.live_allocs==1 && !h.calls[F5_FREE]);
    assert(gf5_therm_resume(&h.ctx,0)==GF5_EINVAL && !h.display.nevents);
    assert(gf5_therm_resume(0,&saved)==GF5_EINVAL);
    /* Fail each setup boundary, clock, and first fan write. No later
     * boundary may execute; allocations and borrowed ROM stay resident. */
    const struct { u32 op,nth; bool clock; } failures[]={
        {D_WRITE,1,false},{D_SLEEP,1,false},{D_READ,1,false},
        {F5_CLOCK64,1,true},{D_WRITE,2,false} /* ADC write then fan write. */
    };
    for (u32 i=0;i<COUNT(failures);++i) {
        reset(); entry(0x24,(50U<<4)|3U); ready(); saved=h.ctx.fan;
        saved.mode=GF5_FAN_MANUAL; saved.duty=T_FULL/2;
        owned=h.ctx.fan.trip;
        if (failures[i].clock) gf5_host_fail_at(&h,F5_CLOCK64,failures[i].nth,-81);
        else gf4_host_fail_at(&h.display,failures[i].op,failures[i].nth,-81);
        assert(gf5_therm_resume(&h.ctx,&saved)==GF5_EIO && h.ctx.source_rc==-81);
        if (failures[i].clock) assert(events[h.nevents-1].op==F5_CLOCK64 && !h.calls[F5_GPIO_FIND]);
        else assert(devents[h.display.nevents-1].rc==-81 && !h.calls[F5_GPIO_SET]);
        assert(h.ctx.fan.trip==owned && h.live_allocs==1 && !h.calls[F5_ALLOC] && !h.calls[F5_FREE]);
        assert(h.ctx.rom.bytes==rom && !h.ctx.image && h.ctx.phase==GF5_RUN);
        no_forbidden();
    }
}
void _start(void)
{
    test_temperature(); test_policy(); test_pwm(); test_failures();
    test_auto(); test_sensor(); test_toggle_and_wrap();
    test_independent_alarms(); test_boost_auto(); test_gpio_source(); test_resume();
    gf2_host_exit(0);
}
