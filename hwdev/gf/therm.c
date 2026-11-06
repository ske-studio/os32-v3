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
 *
 * Authors of the reference: Ben Skeggs, Martin Peres.
 */
/* NV44A only. References below are MIT nouveau c03c1a1, nvkm/subdev/.
 * Foreground alarms replace workers; no IRQ, locks or tachometer.
 * Shutdown is a pending request consumed by the resident foreground adapter.
 * sensor_setup=0 means no policy/setup yet; 0xff is the saved OLD_STYLE
 * register value; 0x100 marks unavailable setup, 0x1ff an initialized but stopped sensor.
 * The production gf5 owns the parsed trip allocation for
 * its lifetime; snapshots borrow/copy it under the feature5.h contract. */
#include "feature5.h"
#include "../../include/kprintf.h"
#define GF_ROM_BIT_ONLY
#include "rom.inc"
#undef GF_ROM_BIT_ONLY

/* therm/nv40.c:29-102,105-165; therm/fantog.c:111; base.c:151. */
#define TH_ADC 0x0015b0U
#define TH_RAW 0x0015b4U
#define TH_OLD_SETUP 0xffU
#define TH_OFF_SETUP 0x100U
#define TH_SENSOR_OFF_SETUP 0x1ffU
#define TH_SETTLE_US 20000U
#define TH_PWM2 0x0010f0U
#define TH_PWM9 0x0015f4U
#define TH_DIV9 0x0015f8U
#define TH_PWM_ENABLE 0x80000000U
#define TH_PWM2_FIELDS 0x7fff7fffU
#define TH_PWM9_DUTY 0x7fffffffU
#define TH_PWM2_DIV 0x7fffU
#define TH_FAN_TAG 0x09U /* bios/gpio.h DCB_GPIO_FAN. */
#define TH_POLL_US 1000000U
#define TH_TOGGLE_US 100000U
#define TH_PERCENT 100U
#define TH_STEP 3U /* therm/fan.c:61-70. */
#define TH_MILLI 1000U
#define TH_TEMP_BIAS 8 /* therm/nv40.c:97. */
#define TH_FALLBACK_MIN 30 /* therm/base.c:100-103. */
#define TH_BUMP_MS 500U /* therm/fan.c:189-194. */
#define TH_SLOW_MS 2000U
#define TH_LINEAR_MIN 40
#define TH_LINEAR_MAX 85
#define TH_FAN_ENTRY_BYTES 15U /* bios/fan.c:76-90, field +0xb LE32. */

enum { TH_BOOST, TH_DOWN, TH_CRITICAL, TH_SHUTDOWN };

/* GPIO public calls already classify rc and retain the raw source.
 * Other boundaries return raw rc; only this public exit maps them. */
static i32 th_exit(struct gf5 *g,i32 rc)
{
    if (g) {
        if (!rc || !g->source_rc) g->source_rc=rc;
        g->last_rc=gf5_rc_map(rc);
    }
    return g ? g->last_rc : GF5_EINVAL;
}
static i32 th_check(struct gf5 *g)
{
    if (!g || !g->ch || !g->ch->f) return GF5_EINVAL;
    return 0;
}
static bool th_span(const struct gf5 *g,u32 off,u32 bytes)
{
    return off<=g->rom.size && bytes<=g->rom.size-off;
}
static u32 th_le16(const u8 *p) { return (u32)p[0]|(u32)p[1]<<8; }
static u32 th_le32(const u8 *p) { return th_le16(p)|th_le16(p+2)<<16; }
static i32 th_read(struct gf5 *g,u32 reg,u32 *out)
{
    struct gf_io *io=&g->ch->f->io;
    return io->read(io->opaque,0,reg,4,out);
}
static i32 th_write(struct gf5 *g,u32 reg,u32 val)
{
    struct gf_io *io=&g->ch->f->io;
    return io->write(io->opaque,0,reg,4,val);
}
static i32 th_mask(struct gf5 *g,u32 reg,u32 mask,u32 val)
{
    u32 old;
    i32 rc=th_read(g,reg,&old);
    return rc ? rc : th_write(g,reg,(old&~mask)|val);
}
static i32 th_now(struct gf5 *g,u64 *now)
{
    return g->links.clock64 ? g->links.clock64(g->links.owner,now) : GF5_EINVAL;
}
/* No 64-bit division/libgcc. Bounded binary division for PWM arithmetic;
 * bios divisor can be a full word on line9 readback. */
static u32 th_div(u64 n,u32 d)
{
    u64 q=0,r=0;
    for (unsigned bit=64;bit--;) {
        r=(r<<1)|((n>>bit)&1U);
        if (r>=d) { r-=d; q|=1ULL<<bit; }
    }
    return (u32)q;
}

/* bios/therm.c:29-64. Empty table is a legitimate uncalibrated sensor.
 * Check the complete declared span before interpreting any entry. */
static i32 th_table(struct gf5 *g,u32 ptr,u32 *entries,u32 *len,u32 *count)
{
    *entries=*len=*count=0;
    if (!ptr) return 0;
    if (!th_span(g,ptr,4)) return GF5_EBIOS;
    const u8 *r=g->rom.bytes+ptr;
    u32 hdr=r[1]; *len=r[2]; *count=r[3];
    if (hdr<4 || *len<3 || !th_span(g,ptr,hdr+*len * *count)) return GF5_EBIOS;
    *entries=ptr+hdr;
    return 0;
}
/* bios/perf.c:30-78,201-215: BIT first, BMP >=0x25 fallback. */
static i32 th_perf_divisor(struct gf5 *g,u32 perf,u32 *divisor)
{
    if (perf) {
        if (!th_span(g,perf,2)) return GF5_EBIOS;
        const u8 *r=g->rom.bytes+perf;
        if (r[0]>=0x20U && r[0]<0x40U) {
            if (!th_span(g,perf,r[1])) return GF5_EBIOS;
            if (r[1]>6) {
                if (!th_span(g,perf,8)) return GF5_EBIOS;
                *divisor=th_le16(r+6);
            }
            return 0;
        }
        if (r[0]==0x40U) return 0;
    }
    static const u8 bmp[]={0xff,0x7f,'N','V',0};
    for (u32 at=0;th_span(g,at,sizeof(bmp));++at) {
        u32 i=0;
        while (i<sizeof(bmp) && g->rom.bytes[at+i]==bmp[i]) ++i;
        if (i!=sizeof(bmp)) continue;
        if (!th_span(g,at,7)) return GF5_EBIOS;
        if (g->rom.bytes[at+6]<0x25U) return 0;
        if (!th_span(g,at,0x96U)) return GF5_EBIOS;
        perf=th_le16(g->rom.bytes+at+0x94U);
        if (!perf) return 0;
        if (!th_span(g,perf,2)) return GF5_EBIOS;
        const u8 *r=g->rom.bytes+perf;
        if (r[1]>=0x20U && r[1]<0x40U && r[0]>6) {
            if (!th_span(g,perf,8) || !th_span(g,perf,r[0])) return GF5_EBIOS;
            *divisor=th_le16(r+6);
        }
        return 0;
    }
    return 0;
}
static i32 th_policy(struct gf5 *g,struct gf5_fan_saved *f)
{
    /* therm/temp.c:26-54; therm/fan.c:184-210. */
    *f=(struct gf5_fan_saved){.mode=GF5_FAN_NONE,.kind=GF5_FAN_OTHER,
        .max_duty=TH_PERCENT,.bump_ms=TH_BUMP_MS,.slow_ms=TH_SLOW_MS,
        .linear_min=TH_LINEAR_MIN,.linear_max=TH_LINEAR_MAX,.last_trip=-1,
        .gpio_line=U8_MAX,.sensor_poll=true,
        .threshold={90,95,105,135},.hyst={3,3,5,5}};
    if (!g->rom.bytes || !g->rom.size) return GF5_ENOTSUP;
    u32 p=0,plen=0; u8 version=0;
    i32 found=gf_rom_bit_entry(g->rom.bytes,g->rom.size,'P',&p,&plen,&version);
    if (found<0) return GF5_EBIOS;
    if (found>0) return th_perf_divisor(g,0,&f->divisor);
    if (version!=1 && version!=2) return GF5_ENOTSUP;
    u32 offset=version==1 ? 12U : 16U;
    if (plen<offset+4U) return GF5_EBIOS;
    u32 entries,len,count;
    i32 rc=th_table(g,th_le32(g->rom.bytes+p+offset),&entries,&len,&count);
    if (rc) return rc;
    /* bios/fan.c:29-93. A recognized table replaces therm fan entries. */
    u32 fan=version==2 && plen>=0x5cU ? th_le32(g->rom.bytes+p+0x58U) : 0;
    bool fan_table=false;
    if (fan) {
        if (!th_span(g,fan,4)) return GF5_EBIOS;
        const u8 *r=g->rom.bytes+fan;
        if (r[0]!=0x10U) return GF5_ENOTSUP;
        if (r[0]==0x10U) {
            u32 hdr=r[1],stride=r[2],n=r[3];
            if (hdr<4 || stride<TH_FAN_ENTRY_BYTES ||
                !th_span(g,fan,hdr+stride*n)) return GF5_EBIOS;
            if (n) {
                r+=hdr; fan_table=true; f->kind=GF5_FAN_LINEAR;
                /* Temporary method preference; GPIO availability is checked
                 * separately, before the first control operation. */
                if (!r[0]) f->method=GF5_FAN_TOGGLE;
                f->min_duty=r[2]; f->max_duty=r[3];
                f->freq=th_le32(r+0x0bU)&0xffffffU;
            }
        }
    }
    rc=th_perf_divisor(g,th_le32(g->rom.bytes+p),&f->divisor);
    if (rc) return rc;
    /* Count first, allocate precisely. No fixed trip ceiling. */
    for (u32 i=0;!fan_table && i<count;++i)
        if (g->rom.bytes[entries+i*len]==0x24U) ++f->ntrip;
    if (f->ntrip) {
        if (!g->links.alloc || !g->links.free) return GF5_EINVAL;
        void *trips=0;
        rc=g->links.alloc(g->links.owner,f->ntrip*sizeof(*f->trip),&trips);
        if (rc) return rc;
        f->trip=trips;
    }
    /* bios/therm.c:67-146,152-210: first core sensor only; fan entries
     * remain global even beyond the ambient section. */
    i32 sensor=-1; bool core=true; u32 trip=0;
    static const u8 duty_lut[]={0,0,25,0,40,0,50,0,75,0,85,0,100,0,100,0};
    for (u32 i=0;i<count;++i) {
        const u8 *r=g->rom.bytes+entries+i*len;
        u32 value=th_le16(r+1); i32 signed_value=(i16)value;
        switch (r[0]) {
        case 0x00: if (signed_value>0) core=false; break;
        case 0x01:
            if (core && ++sensor==0) f->offset_constant=(i8)r[2]/2;
            break;
        case 0x10: if (core && sensor==0) f->offset_num=signed_value; break;
        case 0x11: if (core && sensor==0) f->offset_den=signed_value; break;
        case 0x12: if (core && sensor==0) f->slope_mult=signed_value; break;
        case 0x13: if (core && sensor==0) f->slope_div=signed_value; break;
        case 0x04: case 0x07: case 0x08: case 0x32:
            if (core) {
                u32 t=r[0]==0x04 ? TH_CRITICAL : r[0]==0x07 ? TH_DOWN :
                      r[0]==0x08 ? TH_BOOST : TH_SHUTDOWN;
                f->threshold[t]=(value&0xff0U)>>4;
                f->hyst[t]=value&15U;
                if (f->hyst[t]<2) f->hyst[t]=2;
            }
            break;
        default: break;
        }
        if (fan_table) continue;
        switch (r[0]) {
        case 0x22: f->min_duty=value&U8_MAX; f->max_duty=value>>8; break;
        case 0x24:
            f->kind=GF5_FAN_TRIP;
            f->trip[trip++]=(struct gf5_fan_trip){(value&0xff0U)>>4,
                value&15U,duty_lut[value>>12]};
            break;
        case 0x25:
            if (!trip) { rc=GF5_EBIOS; goto fail; }
            f->trip[trip-1U].duty=value;
            break;
        case 0x26: if (!f->freq) f->freq=value; break;
        case 0x3b: f->bump_ms=value; break;
        case 0x3c: f->slow_ms=value; break;
        case 0x46:
            if (f->kind!=GF5_FAN_TRIP) f->kind=GF5_FAN_LINEAR;
            f->linear_min=r[1]; f->linear_max=r[2]; break;
        default: break;
        }
    }
    if (f->min_duty>TH_PERCENT) f->min_duty=TH_PERCENT;
    if (f->max_duty>TH_PERCENT) f->max_duty=TH_PERCENT;
    if (f->min_duty>f->max_duty) f->min_duty=f->max_duty;
    if (f->slope_mult && f->slope_div && f->offset_num && f->offset_den)
        f->mode=GF5_FAN_AUTO;
    return 0;
fail:
    if (f->trip) { g->links.free(g->links.owner,f->trip); f->trip=0; }
    return rc;
}
/* therm/nv40.c:52-69 OLD_STYLE; the timing adapter owns wait chunking. */
static i32 th_setup(struct gf5 *g)
{
    i32 rc=th_write(g,TH_ADC,TH_OLD_SETUP);
    struct gf_io *io=&g->ch->f->io;
    if (!rc) rc=io->timing.wait_us(io->opaque,TH_SETTLE_US);
    u32 raw;
    if (!rc) rc=th_read(g,TH_RAW,&raw);
    return rc;
}
static i32 th_init(struct gf5 *g)
{
    if (g->fan.sensor_setup && g->fan.sensor_setup!=TH_OFF_SETUP) return 0;
    struct gf5_fan_saved f;
    i32 rc=th_policy(g,&f);
    if (rc) return rc;
    rc=th_setup(g);
    if (rc) {
        if (f.trip) g->links.free(g->links.owner,f.trip);
        return rc;
    }
    f.sensor_setup=TH_OLD_SETUP;
    g->fan=f;
    return 0;
}
static i32 th_temperature(struct gf5 *g,struct gf5_thermal *out)
{
    g->poll_source="therm";
    i32 rc=th_init(g);
    if (rc) return rc;
    u32 raw;
    rc=th_write(g,TH_ADC,TH_OLD_SETUP);
    if (!rc) rc=th_read(g,TH_RAW,&raw);
    if (rc) return rc;
    struct gf5_fan_saved *f=&g->fan;
    *out=(struct gf5_thermal){.raw=raw&TH_OLD_SETUP};
    if (!f->slope_mult || !f->slope_div || !f->offset_num || !f->offset_den)
        return GF5_ENOTSUP;
    /* therm/nv40.c:89-102: signed division truncates separately. */
    i32 temp=(i32)out->raw*f->slope_mult/f->slope_div;
    temp+=f->offset_num/f->offset_den+f->offset_constant-TH_TEMP_BIAS;
    if (temp<0) temp=0;
    if ((u32)temp>S32_MAX/TH_MILLI) return GF5_ERANGE;
    out->milli_c=temp*TH_MILLI; out->calibrated=true;
    return 0;
}
i32 gf5_therm_read(struct gf5 *g,struct gf5_thermal *out)
{
    if (g) g->source_rc=0;
    i32 rc=th_check(g);
    if (!rc && !out) rc=GF5_EINVAL;
    if (!rc) rc=th_temperature(g,out);
    return th_exit(g,rc);
}

/* Log a fan failure once. The AUTO alarm and emergency requests remain
 * independent of a failed step (therm/base.c:153-159, fan.c:74-98). */
static void th_fan_off(struct gf5 *g,i32 rc)
{
    if (!g->fan_off)
        kprintf(KLOG_ATTR,"[gf5] fan off rc=%d reason=%s\n",
                g->source_rc ? g->source_rc : rc,
                g->poll_source ? g->poll_source : "fan");
    g->fan_off=true;
}
/* therm/fan.c:234-250, nv40.c:105-115, fantog.c:96-99:
 * only output GPIO2/9 can drive an NV40 fan. Keep classification free
 * of sensor setup and toggle PWM writes; parser errors remain errors. */
static i32 th_fan_route(struct gf5 *g,u8 *line,u8 log[2])
{
    g->poll_source="fan-gpio-find";
    i32 rc=gf5_gpio_find(g,TH_FAN_TAG,line,log);
    if (rc) return rc;
    if ((log[0]&2U) || (*line!=2 && *line!=9)) return GF5_ENOTSUP;
    return 0;
}
i32 gf5_fan_check_cstate(struct gf5 *g,u8 percent)
{
    if (g) g->source_rc=0;
    i32 rc=th_check(g);
    if (!rc && percent>TH_PERCENT) rc=GF5_EINVAL;
    /* Parse into a temporary policy, never publish state or initialize
     * the ADC. Release any temporary trip allocation on every exit. */
    if (!rc) {
        struct gf5_fan_saved policy;
        rc=th_policy(g,&policy);
        if (policy.trip) g->links.free(g->links.owner,policy.trip);
        if (!rc) {
            u8 line,log[2];
            rc=th_fan_route(g,&line,log);
        }
    }
    return th_exit(g,rc);
}
static i32 th_fan_prepare(struct gf5 *g)
{
    i32 rc=th_init(g);
    if (rc) return rc;
    struct gf5_fan_saved *f=&g->fan;
    if (f->gpio_line!=U8_MAX) return f->method ? 0 : GF5_ENOTSUP;
    u8 line,log[2];
    g->source_rc=0;
    rc=th_fan_route(g,&line,log);
    if (rc) {
        if (rc==GF5_ENODEV) g->poll_source="no-fan";
        if (rc==GF5_ENODEV || rc==GF5_ENOTSUP) th_fan_off(g,rc);
        return rc;
    }
    f->gpio_line=line; f->gpio_log[0]=log[0]; f->gpio_log[1]=log[1];
    if (f->method!=GF5_FAN_TOGGLE) f->method=GF5_FAN_PWM;
    else {
        g->poll_source="fan-pwm";
        rc=th_mask(g,line==2 ? TH_PWM2 : TH_PWM9,TH_PWM_ENABLE,0);
        if (rc) { f->gpio_line=U8_MAX; return rc; }
        f->duty=TH_PERCENT; /* therm/fantog.c:112-113. */
    }
    return 0;
}
static i32 th_get(struct gf5 *g,u32 *percent)
{
    struct gf5_fan_saved *f=&g->fan;
    if (f->method==GF5_FAN_TOGGLE) { *percent=f->duty; return 0; }
    u32 reg,divisor=0,duty=0;
    g->poll_source="fan-pwm-get";
    i32 rc=th_read(g,f->gpio_line==2 ? TH_PWM2 : TH_PWM9,&reg);
    if (rc) return rc;
    if (reg&TH_PWM_ENABLE) {
        if (f->gpio_line==2) { divisor=reg&TH_PWM2_DIV; duty=(reg>>16)&TH_PWM2_DIV; }
        else { duty=reg&TH_PWM9_DUTY; rc=th_read(g,TH_DIV9,&divisor); }
        if (rc) return rc;
    }
    if (divisor) {
        if (duty>divisor) divisor=duty;
        duty=divisor-duty; /* therm/fanpwm.c:47-51 NV40 inversion. */
        *percent=th_div((u64)duty*TH_PERCENT,divisor);
        return 0;
    }
    bool on;
    g->source_rc=0;
    g->poll_source="fan-gpio-get";
    rc=gf5_gpio_get(g,f->gpio_line,f->gpio_log,&on);
    if (!rc) *percent=on ? TH_PERCENT : 0;
    return rc;
}
i32 gf5_fan_get(struct gf5 *g,u32 *percent)
{
    if (g) g->source_rc=0;
    i32 rc=th_check(g);
    if (!rc && !percent) rc=GF5_EINVAL;
    if (!rc) rc=th_fan_prepare(g);
    if (!rc) rc=th_get(g,percent);
    return th_exit(g,rc);
}
static i32 th_toggle(struct gf5 *g,u32 percent,u64 now)
{
    struct gf5_fan_saved *f=&g->fan;
    bool on;
    i32 rc;
    g->source_rc=0;
    g->poll_source="fan-gpio-get";
    rc=gf5_gpio_get(g,f->gpio_line,f->gpio_log,&on);
    /* fantog.c:54-62: a failed get behaves as !negative == false;
     * still attempt the set and schedule the next transition. */
    i32 first=rc,source=g->source_rc ? g->source_rc : rc;
    const char *reason=g->poll_source;
    on=rc ? false : !on;
    g->source_rc=0;
    g->poll_source="fan-gpio-set";
    rc=gf5_gpio_set(g,f->gpio_line,f->gpio_log,on);
    if (!first && rc) { first=rc; source=g->source_rc ? g->source_rc : rc; reason=g->poll_source; }
    f->toggle_on=on; f->duty=percent;
    g->next_toggle=0;
    if (percent!=(on ? TH_PERCENT : 0)) {
        u32 delay=percent*TH_TOGGLE_US/TH_PERCENT;
        if (!on) delay=TH_TOGGLE_US-delay;
        g->next_toggle=now+delay;
    }
    g->source_rc=source;
    if (first) { g->poll_source=reason; th_fan_off(g,first); }
    g->source_rc=0;
    return 0;
}
static i32 th_set(struct gf5 *g,u32 percent,u64 now)
{
    struct gf5_fan_saved *f=&g->fan;
    if (f->active[TH_BOOST]) percent=f->max_duty;
    u32 reg=f->gpio_line==2 ? TH_PWM2 : TH_PWM9;
    i32 rc;
    g->poll_source="fan-pwm";
    if (f->method==GF5_FAN_TOGGLE) {
        rc=th_mask(g,reg,TH_PWM_ENABLE,0);
        /* fantog.c:85-89: pwm_ctrl and GPIO errors do not stop steps. */
        if (rc) th_fan_off(g,rc);
        return th_toggle(g,percent,now);
    }
    /* therm/fanpwm.c:64-81: nv40 has no pwm_clock, including freq>1
     * yielding divisor zero. Preserve that result, never invent a divisor. */
    u32 divisor=f->freq ? 1U/f->freq : f->divisor;
    u32 duty=th_div((u64)divisor*percent+TH_PERCENT-1U,TH_PERCENT);
    duty=divisor-duty;
    if (f->gpio_line==2)
        rc=th_mask(g,reg,TH_PWM2_FIELDS,(duty<<16)|divisor);
    else {
        rc=th_write(g,TH_DIV9,divisor);
        if (!rc) rc=th_mask(g,reg,TH_PWM9_DUTY,duty);
    }
    if (!rc) rc=th_mask(g,reg,TH_PWM_ENABLE,TH_PWM_ENABLE);
    if (!rc) f->duty=percent;
    return rc;
}
static u32 th_clamp(struct gf5_fan_saved *f,u32 target)
{
    if (target<f->min_duty) target=f->min_duty;
    if (target>f->max_duty) target=f->max_duty;
    return target;
}
static i32 th_update(struct gf5 *g,u32 target,bool immediate,u64 now)
{
    i32 rc=th_fan_prepare(g);
    if (rc) return rc;
    struct gf5_fan_saved *f=&g->fan;
    if (f->active[TH_BOOST]) { target=TH_PERCENT; immediate=true; }
    target=th_clamp(f,target); f->target=target;
    u32 duty;
    rc=th_get(g,&duty);
    if (rc) { /* therm/fan.c:59-74 readback unavailable. */
        g->source_rc=0; duty=target;
    }
    if (duty==target && !rc) {
        g->next_step=0;
        /* NONE cancels our foreground toggle alarm. Re-entering control
         * must resume a fractional saved duty even if get() matches it. */
        if (f->method==GF5_FAN_TOGGLE && !g->next_toggle && target && target<TH_PERCENT)
            return th_set(g,target,now);
        return 0;
    }
    if (immediate) duty=target;
    else if (duty<target) duty=target-duty>TH_STEP ? duty+TH_STEP : target;
    else if (duty>target) duty=duty-target>TH_STEP ? duty-TH_STEP : target;
    rc=th_set(g,duty,now);
    if (rc) return rc;
    g->next_step=duty==target ? 0 : now+(u64)(duty<target ? f->bump_ms : f->slow_ms)*TH_MILLI;
    return 0;
}
/* therm/base.c:37-103: TRIP hysteresis and integer linear interpolation. */
static u32 th_linear(struct gf5_fan_saved *f,i32 temp,i32 low,i32 high)
{
    if (temp<=low) return f->min_duty;
    if (temp>=high) return f->max_duty;
    return f->min_duty+(u32)(temp-low)*(f->max_duty-f->min_duty)/(u32)(high-low);
}
static void th_monitor_off(struct gf5 *,i32,const char *);
static i32 th_auto(struct gf5 *g,u64 now)
{
    struct gf5_fan_saved *f=&g->fan;
    u32 target;
    g->next_auto=0;
    if (f->kind==GF5_FAN_OTHER && f->cstate) target=f->cstate;
    else {
        /* base.c:153-154: AUTO keeps its own alarm after a sensor error. */
        g->next_auto=now+TH_POLL_US;
        struct gf5_thermal t;
        i32 rc=th_temperature(g,&t);
        if (rc) {
            if (rc==GF5_ENOTSUP) { f->mode=GF5_FAN_NONE; g->next_auto=0; }
            th_monitor_off(g,rc,"auto"); return 0;
        }
        i32 temp=t.milli_c/(i32)TH_MILLI;
        if (f->kind==GF5_FAN_TRIP) {
            i32 chosen=-1;
            for (u32 i=0;i<f->ntrip;++i)
                if (temp>=f->trip[i].temp) chosen=(i32)i;
            if (f->last_trip>=0 && (u32)f->last_trip<f->ntrip) {
                const struct gf5_fan_trip *last=&f->trip[f->last_trip];
                if (temp<=last->temp && temp>last->temp-last->hyst) chosen=f->last_trip;
            }
            f->last_trip=chosen; target=chosen<0 ? 0 : f->trip[chosen].duty;
        } else target=th_linear(f,temp,f->kind==GF5_FAN_LINEAR ? f->linear_min : TH_FALLBACK_MIN,
                               f->kind==GF5_FAN_LINEAR ? f->linear_max : f->threshold[TH_BOOST]);
    }
    return th_update(g,target,false,now);
}
static i32 th_mode(struct gf5 *g,enum gf5_fan_mode mode,u64 now)
{
    struct gf5_fan_saved *f=&g->fan;
    if (mode==GF5_FAN_NONE) {
        if (f->active[TH_BOOST]) return th_update(g,TH_PERCENT,true,now);
        f->mode=mode; g->next_auto=g->next_step=g->next_toggle=0; return 0;
    }
    i32 rc;
    if (mode==GF5_FAN_AUTO) {
        struct gf5_thermal t;
        rc=th_temperature(g,&t);
        if (rc) return rc==GF5_ENOTSUP ? GF5_EINVAL : rc;
        f->mode=mode;
        rc=th_auto(g,now);
    } else {
        f->mode=mode; g->next_auto=g->next_step=0;
        u32 duty;
        rc=th_fan_prepare(g);
        if (!rc) {
            if (th_get(g,&duty)) { g->source_rc=0; duty=TH_PERCENT; }
            rc=th_update(g,duty,true,now);
        }
    }
    /* base.c:181-208: only temperature gates mode selection. The fan
     * update is void there, even for fannil; retain its error in the log. */
    if (rc) th_fan_off(g,rc);
    return 0;
}
i32 gf5_fan_mode_set(struct gf5 *g,enum gf5_fan_mode mode)
{
    if (g) g->source_rc=0;
    i32 rc=th_check(g); u64 now;
    if (!rc && (unsigned)mode>GF5_FAN_AUTO) rc=GF5_EINVAL;
    if (!rc) rc=th_init(g);
    if (!rc) rc=th_now(g,&now);
    if (!rc) rc=th_mode(g,mode,now);
    return th_exit(g,rc);
}
i32 gf5_fan_cstate(struct gf5 *g,u8 percent,i32 dir)
{
    if (g) g->source_rc=0;
    i32 rc=th_check(g); u64 now;
    if (!rc && percent>TH_PERCENT) rc=GF5_EINVAL;
    if (!rc) rc=th_init(g);
    if (!rc && (!dir || (dir>0 && percent>g->fan.cstate) || (dir<0 && percent<g->fan.cstate))) {
        rc=th_now(g,&now);
        if (!rc) {
            g->fan.cstate=percent;
            if (g->fan.mode==GF5_FAN_AUTO) rc=th_auto(g,now);
            else if (g->fan.mode==GF5_FAN_MANUAL) rc=th_mode(g,GF5_FAN_MANUAL,now);
        }
    }
    return th_exit(g,rc);
}
/* temp.c:162-189: failed temperature acquisition cancels its alarm only.
 * Keep control-operation failures separate from this optional monitor. */
static void th_monitor_off(struct gf5 *g,i32 rc,const char *reason)
{
    bool first=g->fan.sensor_setup!=TH_OFF_SETUP &&
               g->fan.sensor_setup!=TH_SENSOR_OFF_SETUP;
    i32 source=g->source_rc ? g->source_rc : rc;
    (void)gf5_heat_step(g,0,0); /* Failed samples never count as cooling. */
    if (g->heat.down_active || g->heat.critical_paused) {
        g->heat.stop_pending=true;
        g->heat.last_rc=gf5_rc_map(source);
    }
    g->fan.sensor_poll=false;
    g->next_sensor=0;
    g->fan.sensor_setup=(!g->fan.sensor_setup || g->fan.sensor_setup==TH_OFF_SETUP) ?
                        TH_OFF_SETUP : TH_SENSOR_OFF_SETUP;
    if (first) {
        kprintf(KLOG_ATTR,"[gf5] therm off rc=%d reason=%s%s\n",
                source,reason,
                rc==GF5_ENOTSUP ? " auto=unavailable (uncalibrated)" : "");
    }
    g->source_rc=0;
}
/* MIT therm/temp.c:81-209 supplies hysteresis edges. The OS32 policy
 * additionally passes every sample to perf.c and gives shutdown priority. */
static i32 th_sensor(struct gf5 *g,u64 now)
{
    struct gf5_fan_saved *f=&g->fan;
    struct gf5_thermal t;
    i32 rc=th_temperature(g,&t);
    if (rc) {
        th_monitor_off(g,rc,"sensor");
        return 0;
    }
    g->next_sensor=now+GF5_SENSOR_INTERVAL_US;
    bool critical_changed=false,boost_rising=false;
    for (u32 i=0;i<GF5_THERM_THRESHOLDS;++i) {
        bool active=f->active[i];
        i64 limit=(i64)f->threshold[i]*TH_MILLI;
        i64 cool=((i64)f->threshold[i]-f->hyst[i])*TH_MILLI;
        if (!active && t.milli_c>=limit) active=true;
        else if (active && t.milli_c<=cool) active=false;
        if (i==TH_BOOST) boost_rising=active && !f->active[i];
        if (i==TH_CRITICAL) critical_changed=active!=f->active[i];
        f->active[i]=active;
    }
    /* Latch before pause/fan/PLL work, even when the first sample jumps
     * straight over all four thresholds. F0s consumes this at poll exit. */
    if (t.milli_c>=(i64)f->threshold[TH_SHUTDOWN]*TH_MILLI) {
        g->heat.stop_pending=true;
        g->heat.cool_samples=0;
    } else if (critical_changed && g->emergency.pause)
        g->emergency.pause(g->emergency.owner,f->active[TH_CRITICAL]);
    i32 heat_rc=gf5_heat_step(g,&t,now);
    i32 heat_source=g->source_rc ? g->source_rc : heat_rc;
    /* Fan failure never skips heat_step. Retry maximum every sample and
     * centralize the lower-target guard in th_update/th_set as well. */
    g->source_rc=0;
    i32 fan_rc=0,fan_source=0;
    if (f->active[TH_BOOST]) {
        fan_rc=th_update(g,TH_PERCENT,true,now);
        fan_source=g->source_rc ? g->source_rc : fan_rc;
        /* MIT temp.c:106-109: boost selects AUTO, including after a failed
         * maximum write. Further samples must not reselect the mode. */
        if (boost_rising && f->mode!=GF5_FAN_AUTO) {
            i32 mode_rc=th_mode(g,GF5_FAN_AUTO,now);
            if (!fan_rc && mode_rc) {
                fan_rc=mode_rc;
                fan_source=g->source_rc ? g->source_rc : mode_rc;
            }
        }
        if (fan_rc) {
            g->source_rc=fan_source;
            th_fan_off(g,fan_rc);
        }
    }
    g->source_rc=heat_rc ? heat_source : fan_source;
    return heat_rc ? heat_rc : fan_rc;
}

void gf5_fan_tick(struct gf5 *g,u64 now)
{
    if (g) g->source_rc=0;
    i32 rc=th_check(g);
    if (g) g->poll_source="other";
    if (!rc && g->fan.sensor_setup==TH_OFF_SETUP) return;
    if (!rc) {
        g->poll_source="therm-init";
        rc=th_init(g);
        if (rc) { th_monitor_off(g,rc,"init"); (void)th_exit(g,0); return; }
    }
    if (rc) { (void)th_exit(g,rc); return; }
    i32 first=0,source=0;
    if (g->fan.sensor_poll && (!g->next_sensor || now>=g->next_sensor))
        first=th_sensor(g,now);
    if (first) { source=g->source_rc ? g->source_rc : first; th_fan_off(g,first); }
    g->source_rc=0;
    if (g->fan.mode==GF5_FAN_AUTO && (!g->next_auto || now>=g->next_auto)) {
        /* OTHER+cstate has no periodic AUTO alarm. */
        if (g->fan.kind!=GF5_FAN_OTHER || !g->fan.cstate) {
            rc=th_auto(g,now);
            if (!first && rc) {
                first=rc; source=g->source_rc ? g->source_rc : rc; th_fan_off(g,rc);
            }
            g->source_rc=0;
        }
    }
    if (g->fan.mode!=GF5_FAN_NONE && g->next_step && now>=g->next_step) {
        g->next_step=0;
        rc=th_update(g,g->fan.target,false,now);
        if (!first && rc) {
            first=rc; source=g->source_rc ? g->source_rc : rc; th_fan_off(g,rc);
        }
        g->source_rc=0;
    }
    if (g->fan.mode!=GF5_FAN_NONE && g->next_toggle && now>=g->next_toggle) {
        g->next_toggle=0;
        rc=th_toggle(g,g->fan.duty,now);
        if (!first && rc) {
            first=rc; source=g->source_rc ? g->source_rc : rc; th_fan_off(g,rc);
        }
        g->source_rc=0;
    }
    g->source_rc=source;
    (void)th_exit(g,first);
}
/* PM18; therm/base.c:376-394: sensor setup, saved duty, mode, alarms.
 * The live trip allocation belongs to g; restoring never replaces it. */
i32 gf5_therm_resume(struct gf5 *g,const struct gf5_fan_saved *saved)
{
    if (g) g->source_rc=0;
    i32 rc=th_check(g);
    if (!rc && (!saved || (unsigned)saved->mode>GF5_FAN_AUTO)) rc=GF5_EINVAL;
    if (!rc && (saved->ntrip!=g->fan.ntrip ||
                (saved->ntrip && (!saved->trip || !g->fan.trip)))) rc=GF5_ESTATE;
    if (!rc) rc=th_setup(g);
    if (rc) return th_exit(g,rc);
    struct gf5_fan_trip *trip=g->fan.trip;
    enum gf5_fan_mode mode=saved->mode;
    bool active[GF5_THERM_THRESHOLDS];
    for (u32 i=0;i<GF5_THERM_THRESHOLDS;++i) active[i]=g->fan.active[i];
    g->fan=*saved;
    for (u32 i=0;i<GF5_THERM_THRESHOLDS;++i) g->fan.active[i]=active[i];
    g->fan.trip=trip;
    for (u32 i=0;i<g->fan.ntrip;++i) trip[i]=saved->trip[i];
    g->fan.sensor_setup=TH_OLD_SETUP;
    g->next_auto=g->next_step=g->next_sensor=g->next_toggle=0;
    u64 now;
    rc=th_now(g,&now);
    if (!rc && mode!=GF5_FAN_NONE) {
        /* Toggle preparation seeds duty; retain the saved restore value. */
        u32 duty=g->fan.duty;
        rc=th_fan_prepare(g);
        if (rc==GF5_ENODEV) { rc=0; }
        else if (rc==GF5_ENOTSUP) { rc=0; }
        else if (!rc) {
            rc=th_set(g,th_clamp(&g->fan,duty),now);
            /* base.c:383-386: restore mode even after a failed duty write. */
            if (rc) { th_fan_off(g,rc); rc=0; g->source_rc=0; }
        }
    }
    if (!rc) {
        /* base.c:355-356,388: fini left NONE; failed AUTO validation
         * leaves it there, and init still proceeds to the sensor alarm. */
        g->fan.mode=GF5_FAN_NONE;
        rc=th_mode(g,mode,now);
        if (rc) { th_fan_off(g,rc); rc=0; g->source_rc=0; }
    }
    if (!rc && g->fan.sensor_poll) g->next_sensor=now+TH_POLL_US;
    return th_exit(g,rc);
}
