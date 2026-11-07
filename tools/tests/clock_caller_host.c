/* Real caller + PC98 reader; protected user page detects premature writes. */
#include "types.h"
#include "pit.h"
#include "clock_source.h"
#include "platform_timer.h"
#include "os32_kapi_shared.h"
static struct pit_setup setup;
static unsigned int enabled, saves, restores, samples, retry_n;
static u32 user_page[1024] __attribute__((aligned(4096)));
volatile u32 tick_count;
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
static void check(int ok) { if (!ok) finish(11); }
static void protect(unsigned int mode)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "a"(125), "b"(user_page),
                     "c"(sizeof(user_page)), "d"(mode) : "memory");
    check(rc == 0);
}
unsigned int irq_save(void)
{
    unsigned int old = enabled;
    saves++; enabled = 0; protect(1); return old;
}
void irq_restore(unsigned int old)
{
    check(!enabled); restores++; enabled = old; protect(3);
}
const struct pit_setup *pit_get_setup(void) { return &setup; }
int pit_init(unsigned int hz) { check(hz == 100); return 0; }
void platform_timer_sample(struct platform_timer_sample *out)
{
    check(!enabled); samples++;
    out->p1 = 0; out->p2 = samples <= retry_n; out->count = setup.reload;
}
#include "../../platform/pc98/clock_source.c"
#include "../../kernel/time/ktime.c"
static void call(int rc, unsigned int n, unsigned int old)
{
    unsigned int before = saves;
    enabled = old; samples = 0;
    user_page[0] = 0x1234; user_page[1] = 0x5678;
    check(sys_time_now(user_page, user_page + 1) == rc);
    check(enabled == old && saves == before + 1 && restores == saves);
    check(samples == n);
    if (rc) check(user_page[0] == 0x1234 && user_page[1] == 0x5678);
}
void _start(void)
{
    unsigned int old, clock;
    for (clock = 0; clock < 2; clock++) {
        setup = (struct pit_setup){clock ? 2457600 : 1996800, 100,
                                  clock ? 24576 : 19968, 10000, PIT_MODE_TIMER0, 1};
        struct clock_info info;
        check(platform_clock_init(100) == 0);
        platform_clock_info(&info);
        check(info.ready && info.tick_hz == 100 && info.counter_hz == setup.clk_hz);
        for (old = 0; old < 2; old++) {
            s_last_us = 0; ktime_clamp_count = 0;
            setup.valid = 0; call(OS32_ERR_NOSYS, 0, old); setup.valid = 1;
            setup.mode = 0; call(OS32_ERR_NOSYS, 0, old); setup.mode = PIT_MODE_TIMER0;
            setup.reload = 0; call(OS32_ERR_NOSYS, 0, old);
            setup.reload = clock ? 24576 : 19968;
            retry_n = 3; call(OS32_ERR_AGAIN, 3, old);
            retry_n = 1; tick_count = 500000;
            call(0, 2, old); check(user_page[0] == (u32)5000000000ULL && user_page[1] == 1);
            retry_n = 0; tick_count = 1;
            call(0, 1, old); check(user_page[0] == (u32)5000000000ULL && user_page[1] == 1);
            check(ktime_clamp_count == 1);
            check(sys_time_now(0, 0) == 0 && enabled == old);
        }
    }
    finish(0);
}
