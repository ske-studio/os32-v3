/* V1: independent expectations from MIT nouveau c03c1a1
 * therm/fan.c:234-250, nv40.c:105-165, fantog.c:96-113 and
 * bios/fan.c:29-93. ROM offsets are synthetic fixture positions.
 * Existing GPIO parser classification is exercised at its UNIT3 boundary. */
#include "gf_startlog_host.h"
#define GF5_HOST_UNIT 3
#include "gf5_fake.h"
enum { BIT=0x20, POWER=0x80, THERM=0x100, FAN=0x180, SIZE=0x200 };
static struct gf5_host h;
static struct gf4_state display;
static struct gf5_host_event events[64];
static struct gf4_host_event devents[128];
static struct gf3_host_reg regs[16];
static u8 rom[SIZE],arena[1024] __attribute__((aligned(GF5_HOST_ALLOC_ALIGN)));
static u8 line,direction;
static void put32(u32 at,u32 v)
{ for (u32 i=0;i<4;++i) rom[at+i]=(u8)(v>>(8*i)); }
static void observe(struct gf5_host *host,const struct gf5_host_event *e)
{
    if (e->op!=F5_GPIO_FIND) return;
    assert(e->a[0]==0x09U); /* DCB_GPIO_FAN, bios/gpio.h. */
    if (e->rc) { host->ctx.source_rc=e->rc; return; }
    *(u8 *)e->p[1]=line;
    u8 *log=(void *)e->p[2]; log[0]=direction; log[1]=1;
}
static void reset(void)
{
    startlog_clear();
    gf5_host_init(&h,&display);
    h.display.base.regs=regs; h.display.base.cregs=sizeof(regs)/sizeof(regs[0]);
    h.display.events=devents; h.display.cevents=sizeof(devents)/sizeof(devents[0]);
    h.events=events; h.cevents=sizeof(events)/sizeof(events[0]); h.observe=observe;
    gf5_host_arena(&h,arena,sizeof(arena));
    gf2_host_memset(rom,0,sizeof(rom));
    const u8 bit[]={0xff,0xb8,'B','I','T',0,0,0,12,6,1,0,'P',2,0x5c,0,POWER,0};
    gf2_host_memcpy(rom+BIT,bit,sizeof(bit));
    put32(POWER+16,THERM);
    rom[THERM]=0x10; rom[THERM+1]=4; rom[THERM+2]=3;
    h.ctx.rom=(struct gf5_rom_view){.bytes=rom,.size=sizeof(rom)};
    line=2; direction=0;
}
static void fan_table(u8 method)
{
    put32(POWER+0x58,FAN);
    rom[FAN]=0x10; rom[FAN+1]=4; rom[FAN+2]=15; rom[FAN+3]=1;
    rom[FAN+4]=method; rom[FAN+7]=100;
}
static void check(i32 expected,u8 percent)
{
    struct gf5_fan_saved before=h.ctx.fan;
    u64 auto_at=h.ctx.next_auto,step_at=h.ctx.next_step;
    u64 sensor_at=h.ctx.next_sensor,toggle_at=h.ctx.next_toggle;
    i32 rc=gf5_fan_check_cstate(&h.ctx,percent);
    assert(startlog_equal(""));
    assert(rc==expected && h.ctx.last_rc==expected && h.ctx.source_rc==expected);
    struct gf5_host_activity a=gf5_host_activity(&h);
    assert(!a.reads && !a.writes && !a.pauses && !a.resumes);
    assert(!h.calls[F5_GPIO_GET] && !h.calls[F5_GPIO_SET] && !h.calls[F5_CLOCK64]);
    assert(!h.live_allocs && h.ctx.phase==GF5_RUN && !h.ctx.fan_off);
    assert(h.ctx.next_auto==auto_at && h.ctx.next_step==step_at);
    assert(h.ctx.next_sensor==sensor_at && h.ctx.next_toggle==toggle_at);
    const u8 *old=(const u8 *)&before,*now=(const u8 *)&h.ctx.fan;
    for (u32 i=0;i<sizeof(before);++i) assert(old[i]==now[i]);
}
void _start(void)
{
    for (u32 method=0;method<2;++method) {
        for (u32 pin=0;pin<2;++pin) {
            reset(); fan_table(method); line=pin ? 9 : 2;
            check(0,0); check(0,100); /* no calibration required. */
        }
    }
    reset(); check(GF5_EINVAL,101); assert(!h.calls[F5_GPIO_FIND]);
    assert(gf5_fan_check_cstate(0,50)==GF5_EINVAL);
    reset(); h.ctx.ch=0; check(GF5_EINVAL,50);
    reset(); gf5_host_fail_at(&h,F5_GPIO_FIND,0,GF5_ENODEV); check(0,50);
    reset(); gf5_host_fail_at(&h,F5_GPIO_FIND,0,GF5_ENOTSUP); check(GF5_ENOTSUP,50);
    reset(); gf5_host_fail_at(&h,F5_GPIO_FIND,0,GF5_EBIOS); check(GF5_EBIOS,50);
    reset(); line=8; check(0,50); /* present, unsupported NV40 pin */
    reset(); direction=2; check(0,50); /* present input pin */
    reset(); fan_table(1); rom[FAN]=0x11; check(0,50);
    /* Unknown versions need only their version byte; recognized tables
     * must still validate the header and complete declared range. */
    reset(); put32(POWER+0x58,SIZE-1); rom[SIZE-1]=0x11; check(0,50);
    reset(); put32(POWER+0x58,SIZE-1); rom[SIZE-1]=0x10; check(GF5_EBIOS,50);
    reset(); fan_table(1); rom[FAN+3]=255; check(GF5_EBIOS,50);
    reset(); put32(POWER+0x58,SIZE); check(GF5_EBIOS,50);
    reset(); rom[BIT+13]=3; check(GF5_ENOTSUP,50);
    reset(); fan_table(1); rom[FAN+2]=1; check(GF5_EBIOS,50);
    reset(); rom[THERM+3]=255; check(GF5_EBIOS,50);
    reset(); h.ctx.rom.bytes=0; check(GF5_ENOTSUP,50);
    /* Temporary trip policy is released even if GPIO lookup fails. */
    for (u32 fail=0;fail<2;++fail) {
        reset(); rom[THERM+3]=1; rom[THERM+4]=0x24;
        if (fail) gf5_host_fail_at(&h,F5_GPIO_FIND,0,GF5_ENODEV);
        check(0,50);
        assert(h.calls[F5_ALLOC]==1 && h.calls[F5_FREE]==1);
    }
    /* Cached live state must not bypass fresh ROM/DCB validation. */
    reset(); h.ctx.fan.sensor_setup=0xff; h.ctx.fan.gpio_line=2;
    h.ctx.fan.method=GF5_FAN_PWM; h.ctx.fan.cstate=75;
    h.ctx.next_sensor=17; h.ctx.next_toggle=19;
    gf5_host_fail_at(&h,F5_GPIO_FIND,0,GF5_ENODEV); check(0,50);
    /* The normal path and resume retain ENODEV without programming an
     * invented PWM pin. Temperature setup remains independent. */
    reset(); gf5_host_fail_at(&h,F5_GPIO_FIND,0,GF5_ENODEV);
    u32 duty=123;
    assert(gf5_fan_get(&h.ctx,&duty)==GF5_ENODEV && duty==123);
    struct gf5_fan_saved saved=h.ctx.fan;
    saved.mode=GF5_FAN_MANUAL;
    gf5_host_trace_clear(&h);
    assert(!gf5_therm_resume(&h.ctx,&saved));
    assert(h.display.calls[D_WRITE]==1 && h.display.calls[D_READ]==1);
    assert(!h.calls[F5_GPIO_SET]);
    /* fannil: valid cstate changes and mode selection succeed, direct
     * get and the internal set retain ENODEV without touching PWM/GPIO. */
    for (u32 pin=0;pin<2;++pin) {
        reset(); line=pin ? 8 : 2; direction=pin ? 0 : 2;
        check(0,50);
        assert(!gf5_fan_cstate(&h.ctx,50,0) && h.ctx.fan.cstate==50);
        assert(!gf5_fan_mode_set(&h.ctx,GF5_FAN_MANUAL));
        assert(gf5_fan_get(&h.ctx,&duty)==GF5_ENODEV);
        gf5_host_trace_clear(&h);
        assert(!gf5_fan_cstate(&h.ctx,75,0) && h.ctx.fan.cstate==75);
        assert(!h.display.nevents && !h.calls[F5_GPIO_GET] && !h.calls[F5_GPIO_SET]);
        assert(!h.ctx.next_step && !h.ctx.next_toggle);
    }
    gf2_host_exit(0);
}
