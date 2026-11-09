/* ILP32 libc-free test: replace only privileged TB register reads. */
#include "types.h"
static unsigned long long now;
static unsigned upper_calls, lower_calls, rollover;
static u32 tb_upper(void)
{
    upper_calls++;
    if (rollover && upper_calls == 1) return 7;
    return (u32)(now >> 32);
}
static u32 tb_lower(void)
{
    lower_calls++;
    return (u32)now;
}
#define PM_CLOCK_FAKE
#include "clock.c"
static void quit(unsigned code) __attribute__((noreturn));
static void quit(unsigned code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(x) do { if (!(x)) quit(__LINE__); } while (0)
static void expect(unsigned long long expected)
{
    u32 lo=0, hi=0;
    REQUIRE(platform_clock_read_us(&lo,&hi)==0);
    REQUIRE((((unsigned long long)hi<<32)|lo)==expected);
}
void _start(void)
{
    struct pm_kboot_info info={0};
    struct clock_info state;
    struct platform_clock_value value;
    u32 lo=123, hi=456;
    REQUIRE(platform_clock_read_us(&lo,&hi)<0);
    REQUIRE(lo==123 && hi==456);
    platform_clock_info(&state);
    REQUIRE(!state.ready);
    REQUIRE(platform_clock_init(1000)<0);
    u32 invalid[]={0,999999,1000000001,0xffffffffUL};
    for (unsigned i=0;i<4;i++) {
        info.tb_hz=invalid[i]; pm_clock_bind(&info);
        REQUIRE(platform_clock_init(1000)<0);
        REQUIRE(platform_clock_read_us(&lo,&hi)<0);
        REQUIRE(lo==123 && hi==456);
    }
    u32 rates[]={1000000,25000000,33333333,1000000000};
    for (unsigned i=0;i<4;i++) {
        info.tb_hz=rates[i]; pm_clock_bind(&info);
        REQUIRE(platform_clock_init(0)<0);
        REQUIRE(platform_clock_init(100)<0);
        now=0x7fffffff0ULL;
        REQUIRE(platform_clock_init(pm_tick_hz())==0);
        platform_clock_info(&state);
        REQUIRE(state.ready && state.tick_hz==1000 && state.counter_hz==rates[i]);
        platform_clock_read(&value);
        REQUIRE(value.hz==rates[i] && value.is_8mhz==0);
        now+=17; expect(17000000ULL/rates[i]);
        now=origin+(unsigned long long)rates[i]*60*86400+rates[i]-1;
        expect(60ULL*86400*1000000+999999);
        now=~0ULL;
        unsigned long long d=now-origin;
        expect((d/rates[i])*1000000+(d%rates[i])*1000000/rates[i]);
    }
    info.tb_hz=1000000; pm_clock_bind(&info);
    now=0; REQUIRE(platform_clock_init(1000)==0);
    now=0x800000003ULL; rollover=1; upper_calls=lower_calls=0;
    expect(now);
    REQUIRE(upper_calls==4 && lower_calls==2);
    rollover=0;
    /* Unsigned full TB wrap relative to a nonzero origin. */
    now=~0ULL-9; REQUIRE(platform_clock_init(1000)==0);
    now=10; expect(20);
    pm_clock_bind(0);
    REQUIRE(platform_clock_read_us(&lo,&hi)<0);
    REQUIRE(lo==123 && hi==456);
    quit(0);
}
