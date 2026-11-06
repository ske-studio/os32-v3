/* T1 policy acceptance: GF_GUARD_THERM_DESIGN section 6 (OS32 decisions).
 * Sensor/PWM fixture: MIT nouveau c03c1a1 therm/{nv40,temp,fanpwm}.c,
 * inherited below. External heat_step is observed, never reimplemented. */
#define _start legacy_therm_start
#include "gf5_therm_host.c"
#undef _start
static u32 samples,failed_samples;
static i32 sampled_temp;
static u64 sampled_time;
static bool shutdown_seen;
static void t1_observe(struct gf5_host *host,const struct gf5_host_event *e)
{
    observe(host,e);
    if (e->op==F5_HEAT) {
        const struct gf5_thermal *t=e->p[1];
        if (t) { ++samples; sampled_temp=t->milli_c; }
        else ++failed_samples;
        sampled_time=((u64)e->a[1]<<32)|e->a[0];
        shutdown_seen=host->ctx.stop->state;
    }
}
static void begin(void)
{
    reset(); ready(); h.observe=t1_observe;
    samples=failed_samples=0; shutdown_seen=false;
}
static void sample(u32 temp)
{
    gf3_host_set(&h.display.base,0,T_RAW,temp);
    h.now+=T_POLL;
    gf5_fan_tick(&h.ctx,h.now);
}
static void test_samples(void)
{
    begin();
    sample(96); sample(96);
    assert(samples==2 && sampled_temp==96000 && sampled_time==h.now);
    assert(!down_count); /* perf.c owns continuous downclock, no edge duplicate. */
    u32 n=samples;
    gf5_fan_tick(&h.ctx,h.now); assert(samples==n);
    h.now+=9*T_POLL; gf5_fan_tick(&h.ctx,h.now);
    assert(samples==n+1 && h.ctx.next_sensor==h.now+T_POLL);
    sample(106); assert(pause_count==1 && pause_active);
    sample(106); assert(pause_count==1);
    sample(100); assert(pause_count==2 && !pause_active); /* equality releases: temp <= thr - hyst (nouveau therm/temp.c:150-153) */
    sample(99); assert(pause_count==2 && !pause_active);
    begin(); sample(135);
    assert(shutdown_seen && h.ctx.stop->state && !pause_count);
    assert(samples==1 && !down_count);
    assert(h.ctx.stop->reason==GF5_ESTATE && h.ctx.stop->milli_c==135000 &&
           !h.ctx.stop->source_rc);
}
static void test_maximum(void)
{
    for (u32 mode=GF5_FAN_NONE;mode<=GF5_FAN_AUTO;++mode) {
        begin(); assert(!gf5_fan_mode_set(&h.ctx,mode));
        sample(90);
        assert(h.ctx.fan.active[0] && h.ctx.fan.duty==T_FULL);
        assert(h.ctx.fan.mode==GF5_FAN_AUTO);
        assert(!gf5_fan_cstate(&h.ctx,10,0));
        assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_NONE));
        assert(h.ctx.fan.duty==T_FULL);
        assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_AUTO));
        assert(h.ctx.fan.target==T_FULL);
        sample(89); assert(h.ctx.fan.duty==T_FULL && h.ctx.fan.target==T_FULL);
        sample(88); assert(h.ctx.fan.active[0]);
        sample(87); assert(!h.ctx.fan.active[0]); /* equality releases (temp <= thr - hyst) */
        sample(86); assert(!h.ctx.fan.active[0]);
    }
    begin(); h.ctx.fan.max_duty=80; sample(90);
    assert(h.ctx.fan.duty==80 && h.ctx.fan.target==80);
    struct gf5_fan_saved saved=h.ctx.fan;
    saved.active[0]=false; saved.mode=GF5_FAN_MANUAL; saved.duty=20;
    gf5_host_trace_clear(&h);
    assert(!gf5_therm_resume(&h.ctx,&saved));
    for (u32 i=0;i<h.display.nevents;++i)
        if (devents[i].op==D_WRITE && devents[i].a[1]==T_PWM2)
            assert(((devents[i].a[3]>>16)&0x7fffU)==20);
    assert(h.ctx.fan.active[0] && h.ctx.fan.duty==80);
}
static void test_failures_t1(void)
{
    begin(); gf5_host_fail_at(&h,F5_GPIO_FIND,1,GF5_ENODEV);
    sample(96); sample(96);
    assert(samples==2 && h.ctx.fan.sensor_poll);
    sample(135); assert(shutdown_seen && h.ctx.stop->state);
    begin(); gf4_host_fail_at(&h.display,D_WRITE,2,-81);
    sample(106); assert(samples==1 && pause_count==1);
    begin(); sample(96);
    h.ctx.heat.down_active=true;
    gf4_host_fail_at(&h.display,D_READ,h.display.calls[D_READ]+1,-82);
    sample(96); assert(failed_samples==1 && !h.ctx.fan.sensor_poll);
    begin(); assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_AUTO));
    h.ctx.heat.down_active=true; h.ctx.heat.cool_samples=2;
    h.ctx.next_sensor=h.now+T_POLL; h.ctx.next_auto=0;
    gf4_host_fail_at(&h.display,D_READ,h.display.calls[D_READ]+1,-82);
    gf5_fan_tick(&h.ctx,h.now);
    assert(failed_samples==1 && h.ctx.stop->state);
    begin(); h.ctx.heat.critical_paused=true;
    gf4_host_fail_at(&h.display,D_READ,1,-82);
    sample(106);
    assert(failed_samples==1 && h.ctx.stop->state && h.ctx.stop->reason==GF5_EIO);
}
void _start(void)
{
    test_samples(); test_maximum(); test_failures_t1(); gf2_host_exit(0);
}
