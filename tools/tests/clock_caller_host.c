/* Real caller + PC98 reader; protected user page detects premature writes. */
#include "types.h"
#include "pit.h"
#include "clock_source.h"
#include "platform_timer.h"
#include "os32_kapi_shared.h"
static struct pit_setup setup;
static unsigned int enabled, saves, restores, samples, retry_n;
static u32 raw_lo, raw_hi, sample_count, pending;
static int config_result;
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
int tick_clock_config(u32 num, u32 den)
{ check(num == setup.reload && den == setup.clk_hz); return config_result; }
void tick_clock_raw(u32 *lo, u32 *hi)
{ check(!enabled); *lo = raw_lo; *hi = raw_hi; }
void platform_timer_sample(struct platform_timer_sample *out)
{
    check(!enabled); samples++;
    out->p1 = pending; out->p2 = samples <= retry_n; out->count = sample_count;
}
static int tsc_ready, tsc_rc;
static unsigned long long fake_tsc_us = (0x10000000ULL << 32) | 123;
int pc98_tsc_init(void) { return -1; }
int pc98_tsc_ready(void) { return tsc_ready; }
int pc98_tsc_read_us(u32 *lo, u32 *hi)
{ if (tsc_rc) return tsc_rc; *lo = (u32)fake_tsc_us; *hi = (u32)(fake_tsc_us >> 32); return 0; }
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
        config_result = -1;
        check(platform_clock_init(100) == 0);
        platform_clock_info(&info);
        check(!info.ready);
        call(OS32_ERR_NOSYS, 0, 1);
        config_result = 0;
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
            retry_n = 1; raw_lo = tick_count = 500000; raw_hi = 0;
            sample_count = setup.reload;
            call(0, 2, old); check(user_page[0] == (u32)5000000000ULL && user_page[1] == 1);
            retry_n = 0; raw_lo = tick_count = 1;
            call(0, 1, old); check(user_page[0] == (u32)5000000000ULL && user_page[1] == 1);
            check(ktime_clamp_count == 1);
            check(sys_time_now(0, 0) == 0 && enabled == old);
        }
    }
    /* All four physical periods, including interpolation and boundary pending.
     * 100Hz must equal the previous clock; 1kHz must use raw, not jiffies. */
    for (clock = 0; clock < 4; clock++) {
        static const u32 reloads[] = {19968, 24576, 1997, 2458};
        static const u32 clocks[] = {1996800, 2457600, 1996800, 2457600};
        unsigned long long prev = 0;
        setup = (struct pit_setup){clocks[clock], clock < 2 ? 100 : 1000,
                                  reloads[clock], 17, PIT_MODE_TIMER0, 1};
        s_last_us = 0; ktime_clamp_count = 0; retry_n = 0;
        raw_hi = 0; pending = 0;
        for (raw_lo = 0; raw_lo <= 60000; raw_lo += 37) {
            unsigned int phase;
            tick_count = clock < 2 ? raw_lo : raw_lo / 10;
            for (phase = 0; phase < 3; phase++) {
                unsigned long long got, want, cycles;
                sample_count = phase == 0 ? setup.reload : phase == 1 ? setup.reload / 2 : 1;
                call(0, 1, raw_lo & 1);
                got = ((unsigned long long)user_page[1] << 32) | user_page[0];
                cycles = (unsigned long long)raw_lo * setup.reload + setup.reload - sample_count;
                /* Bounded 60s reference can multiply before dividing. */
                want = cycles * 1000000ULL / setup.clk_hz;
                check(got >= prev && got == want);
                if (clock < 2)
                    check(got == (unsigned long long)tick_count * 10000 +
                          (unsigned long long)(setup.reload - sample_count) * 10000 / setup.reload);
                prev = got;
            }
        }
        check(ktime_clamp_count == 0);
        /* pending at the u32 raw rollover must carry into the high word. */
        raw_lo = 0xffffffffU; raw_hi = 2; pending = 1; sample_count = setup.reload;
        call(0, 1, 1);
        {
            unsigned long long got = ((unsigned long long)user_page[1] << 32) | user_page[0];
            unsigned long long cycles = (3ULL << 32) * setup.reload;
            check(got == cycles / setup.clk_hz * 1000000ULL +
                         cycles % setup.clk_hz * 1000000ULL / setup.clk_hz);
        }
        raw_lo = 0; raw_hi = 3; pending = 0;
        call(0, 1, 0); check(ktime_clamp_count == 0);
    }
    /* Failure after slowing, stopping, or accelerating preserves the last
     * public value even when PIT is ahead, then advances by PIT elapsed time. */
    setup = (struct pit_setup){1996800, 100, 19968, 10000, PIT_MODE_TIMER0, 1};
    for (u32 scenario = 0; scenario < 3; ++scenario) {
        raw_hi = 4 + scenario; raw_lo = 10;
        pending = 0; sample_count = setup.reload; retry_n = 0;
        unsigned long long pit_base = (((unsigned long long)raw_hi << 32) + raw_lo) * 10000;
        fake_tsc_us = scenario == 2 ? pit_base + 1000000 : pit_base - 1000000;
        tsc_ready = 1; s_last_us = 0;
        call(0, 0, 1);
        raw_lo++;
        fake_tsc_us += scenario == 0 ? 5000 : scenario == 1 ? 0 : 20000;
        call(0, 0, 0);
        check((((unsigned long long)user_page[1] << 32) | user_page[0]) == fake_tsc_us);
        raw_lo++; tsc_ready = 0;
        call(0, 1, 1);
        check((((unsigned long long)user_page[1] << 32) | user_page[0]) == fake_tsc_us);
        raw_lo++; call(0, 1, 0);
        check((((unsigned long long)user_page[1] << 32) | user_page[0]) == fake_tsc_us + 10000);
    }
    fake_tsc_us = (0x10000000ULL << 32) | 123;
    setup = (struct pit_setup){2457600, 1000, 2458, 17, PIT_MODE_TIMER0, 1};
    /* READY bypasses PIT; failed read keeps outputs; failure switches with
     * a reference pair so the PIT fallback progresses from the last public value. */
    tsc_ready = 1; s_last_us = 0;
    call(0, 0, 1); check(user_page[0] == 123 && user_page[1] == 0x10000000);
    tsc_rc = OS32_ERR_AGAIN; call(OS32_ERR_AGAIN, 0, 0);
    tsc_rc = 0; tsc_ready = 0; raw_lo = 1; raw_hi = 0;
    pending = 0; sample_count = setup.reload;
    call(0, 1, 1); check(user_page[0] == 123 && user_page[1] == 0x10000000);
    raw_lo = 2; call(0, 1, 0);
    check(user_page[0] == 1123 && user_page[1] == 0x10000000);
    finish(0);
}

void tick_budget_config(void) { } /* Clock caller fixture. */
