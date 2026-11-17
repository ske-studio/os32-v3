#include "kernel/time/tick.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static int run(void)
{
    static const u32 nums[] = {19968, 24576, 1997, 2458, 1};
    static const u32 dens[] = {1996800, 2457600, 1996800, 2457600, 1000};
    static const u32 counts[] = {6000, 6000, 60000, 60000, 60000};
    static const u32 jifs[] = {6000, 6000, 6000, 6000, 6000};
    static const u32 millis[] = {60000, 60000, 60006, 60009, 60000};
    u32 lo, hi, i, n, start, old_raw, old_hi, old_jif, old_ms, old_jp, old_mp;
    CHECK(tick_clock_step() == TICK_STEP_JIFFY && tick_count == 1);
    tick_count = 0xffffffffu;
    CHECK(tick_clock_step() == TICK_STEP_JIFFY);
    tick_clock_jiffy64(&lo, &hi); CHECK(lo == 0 && hi == 1);
    for (i = 0; i < 5; ++i) {
        start = tick_count;
        CHECK(tick_clock_config(nums[i], dens[i]) == 0);
        for (n = 0; n < counts[i]; ++n) tick_clock_step();
        CHECK(tick_count - start == jifs[i]);
        CHECK(tick_clock_ms() == millis[i]);
    }
    raw_lo = 0xffffffffu; raw_hi = 7;
    tick_count = 0xffffffffu; tick_hi = 4;
    CHECK(tick_clock_config(1, 100) == 0);
    CHECK(tick_clock_step() == TICK_STEP_JIFFY);
    tick_clock_raw(&lo, &hi); CHECK(lo == 0 && hi == 8);
    tick_clock_jiffy64(&lo, &hi); CHECK(lo == 0 && hi == 5);
    old_raw = raw_lo; old_hi = raw_hi; old_jif = tick_count;
    old_ms = ms_now; old_jp = jif_phase; old_mp = ms_phase;
    CHECK(tick_clock_config(0, 100) == -1);
    CHECK(tick_clock_config(1, 0) == -1);
    CHECK(tick_clock_config(2, 100) == -1);
    CHECK(tick_clock_config(4294968u, 0xffffffffu) == -1);
    CHECK(tick_clock_config(1, 0xffffffffu) == -1);
    CHECK(raw_lo == old_raw && raw_hi == old_hi && tick_count == old_jif &&
          tick_hi == 5 && ms_now == old_ms && jif_phase == old_jp &&
          ms_phase == old_mp && clk == 100 && step_jif == 100 && step_ms == 1000);
    return 0;
}
void _start(void)
{
    int rc = run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
