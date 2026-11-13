/* Real ILP32 library, with only its three KAPI inputs replaced. */
#include "userland/lib/mmtime/mmtime_clock.c"
#include "userland/lib/mmtime/mmtime_event.c"
#include "userland/lib/mmtime/mmtime_sleep.c"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static unsigned long long clock_us;
static u32 tick, yields, hits, other_hits, replacement, victim;
static int clock_rc, callback_error, action;
static int fake_time(u32 *lo, u32 *hi)
{
    *lo = (u32)clock_us;
    *hi = (u32)(clock_us >> 32);
    return clock_rc;
}
static u32 fake_tick(void) { return tick; }
static i32 fake_yield(void) { ++yields; clock_us += 1000; return 0; }
static KernelAPI api;
KernelAPI *kapi = &api;

static void reset(void)
{
    for (u32 i = 0; i < MMT_EVENT_MAX; ++i) events[i] = (MmtEvent){0};
    base_us = last_out = clock_us = 0;
    base_tick = tick = yields = pass = hold = hits = other_hits = 0;
    delivering = clock_rc = callback_error = action = 0;
    replacement = victim = 0;
    api.sys_time_now = fake_time;
    api.get_tick = fake_tick;
    api.sys_yield = fake_yield;
}
static unsigned long long read_us(void)
{
    u32 lo, hi;
    if (mmt_now_us(&lo, &hi)) callback_error = 1;
    return ((unsigned long long)hi << 32) | lo;
}
static void other_cb(u32 id, u32 msg, u32 user, u32 dw1, u32 dw2)
{
    (void)id; (void)msg; (void)user; (void)dw1; (void)dw2;
    ++other_hits;
}
static void callback(u32 id, u32 msg, u32 user, u32 dw1, u32 dw2)
{
    ++hits;
    if (msg || user != 42 || dw1 || dw2) callback_error = 1;
    switch (action) {
    case 1:
        if (mmt_kill_event(id)) callback_error = 1;
        break;
    case 2:
        if (mmt_kill_event(victim)) callback_error = 1;
        break;
    case 3: /* ONESHOT must already be free, including its generation. */
        if (mmt_kill_event(id) != OS32_ERR_INVAL) callback_error = 1;
        replacement = mmt_set_event(1, 0, other_cb, 0, MMT_ONESHOT);
        if (!replacement || replacement == id ||
            (replacement - 1) % MMT_EVENT_MAX != (id - 1) % MMT_EVENT_MAX)
            callback_error = 1;
        clock_us += 1000;
        break;
    case 4:
        action = 0;
        mmt_deliver();
        break;
    case 5: /* Allocate a later slot, already due before this pass ends. */
        replacement = mmt_set_event(1, 0, other_cb, 0, MMT_ONESHOT);
        clock_us += 1000;
        break;
    case 6:
        mmt_hold(1);
        break;
    case 7: /* Kill/set from a periodic callback; dying slot stays reserved. */
        if (mmt_kill_event(id)) callback_error = 1;
        replacement = mmt_set_event(1, 0, other_cb, 0, MMT_ONESHOT);
        if (!replacement || replacement == id) callback_error = 1;
        clock_us += 1000;
        break;
    }
}
static u32 periodic(u32 delay)
{
    return mmt_set_event(delay, 0, callback, 42, MMT_PERIODIC);
}
static int test_clock(void)
{
    reset();
    clock_rc = OS32_ERR_NOSYS; tick = 5;
    CHECK(read_us() == 50000);
    clock_us = 20000; clock_rc = 0;
    CHECK(read_us() == 50000);
    clock_rc = OS32_ERR_AGAIN; tick = 6;
    CHECK(read_us() == 50000);
    tick = 9; CHECK(read_us() == 60000);
    clock_us = 30000; clock_rc = 0; CHECK(read_us() == 60000);
    clock_us = 61000; CHECK(read_us() == 61000);
    /* Success -> failure three times -> recovery below fallback -> catchup. */
    clock_us = 100000; tick = 10; CHECK(read_us() == 100000);
    clock_rc = OS32_ERR_AGAIN;
    for (tick = 11; tick <= 13; ++tick)
        CHECK(read_us() == 100000 + (tick - 10) * 10000ULL);
    clock_rc = 0; clock_us = 110000; CHECK(read_us() == 130000);
    clock_us = 130001; CHECK(read_us() == 130001);
    tick = 0xfffffffeUL; clock_us = 0x1fffffff0ULL;
    CHECK(read_us() == clock_us);
    clock_rc = OS32_ERR_AGAIN; tick = 1;
    CHECK(read_us() == 0x1fffffff0ULL + 30000);
    clock_rc = 0; clock_us = 0x100000001ULL * 1000;
    CHECK(mmt_now_ms() == 1);
    CHECK(!callback_error);
    return 0;
}
static int test_events(void)
{
    u32 id, ms;
    reset();
    CHECK(!mmt_set_event(0, 0, callback, 42, 0));
    CHECK(!mmt_set_event(1000001, 0, callback, 42, 0));
    CHECK(!mmt_set_event(1, 1000001, callback, 42, 0));
    CHECK(!mmt_set_event(1, 0, 0, 42, 0));
    CHECK(!mmt_set_event(1, 0, callback, 42, 2));
    CHECK(mmt_begin_period(0) == OS32_ERR_INVAL);
    CHECK(mmt_end_period(1000001) == OS32_ERR_INVAL);
    CHECK(mmt_begin_period(1) == 0 && mmt_end_period(1000000) == 0);
    CHECK(mmt_kill_event(0) == OS32_ERR_INVAL);
    CHECK(mmt_kill_event(16) == OS32_ERR_INVAL);
    CHECK(mmt_next_due_ms(&ms) == 0);
    CHECK(mmt_next_due_ms(0) == OS32_ERR_INVAL);
    id = periodic(10); CHECK(id);
    for (clock_us = 1000; clock_us <= 1000000; clock_us += 1000) mmt_deliver();
    CHECK(hits == 100 && events[0].missed == 0);
    CHECK(mmt_kill_event(id) == 0);
    CHECK(mmt_kill_event(id) == OS32_ERR_INVAL);
    CHECK(mmt_next_due_ms(&ms) == 0); /* dying is excluded */
    mmt_deliver(); CHECK(mmt_kill_event(id) == OS32_ERR_INVAL);
    reset(); id = mmt_set_event(1, 0, callback, 42, MMT_ONESHOT);
    clock_us = 999; mmt_deliver(); CHECK(hits == 0);
    clock_us = 1000; mmt_deliver(); CHECK(hits == 1);
    CHECK(mmt_kill_event(id) == OS32_ERR_INVAL);
    reset(); CHECK(periodic(1000000));
    clock_us = 1; CHECK(mmt_next_due_ms(&ms) == 1 && ms == 1000000);
    clock_us = 999999999; CHECK(mmt_next_due_ms(&ms) == 1 && ms == 1);
    clock_us = 1000000000; CHECK(mmt_next_due_ms(&ms) == 1 && ms == 0);
    clock_us = 1000000001; CHECK(mmt_next_due_ms(&ms) == 1 && ms == 0);
    mmt_deliver(); CHECK(hits == 1 && deadline(&events[0]) == 2000000000ULL);
    CHECK(!callback_error);
    reset();
    id = periodic(1); CHECK(id && periodic(1000));
    CHECK(mmt_next_due_ms(&ms) == 1 && ms == 1);
    CHECK(mmt_kill_event(id) == 0);
    CHECK(mmt_next_due_ms(&ms) == 1 && ms == 1000);
    return 0;
}
static int test_catchup(void)
{
    static const u32 times[] = {79999, 80000, 90000, 95000};
    static const u32 calls[] = {7, 8, 8, 8};
    static const u32 misses[] = {0, 0, 1, 1};
    static const u32 next[] = {80000, 90000, 100000, 100000};
    for (u32 i = 0; i < 4; ++i) {
        reset(); CHECK(periodic(10)); clock_us = times[i]; mmt_deliver();
        CHECK(hits == calls[i] && events[0].missed == misses[i]);
        CHECK(deadline(&events[0]) == next[i]);
        CHECK((u32)(deadline(&events[0]) - 10000) % 10000 == 0);
    }
    /* Exercise high-word division and the documented modulo-u32 counter. */
    reset(); CHECK(periodic(1));
    clock_us = 4294967306ULL * 1000; mmt_deliver();
    CHECK(hits == 8 && events[0].missed == 2);
    CHECK(deadline(&events[0]) == 4294967307ULL * 1000);
    reset(); CHECK(periodic(10)); events[0].missed = 0xffffffffUL;
    clock_us = 90000; mmt_deliver(); CHECK(events[0].missed == 0);
    return 0;
}
static int test_callbacks(void)
{
    for (int mode = 1; mode <= 7; ++mode) {
        reset(); action = mode;
        u32 id = mode == 3 ? mmt_set_event(1, 0, callback, 42, MMT_ONESHOT) : periodic(mode == 5 ? 10 : 1);
        CHECK(id);
        if (mode == 2) victim = mmt_set_event(1, 0, other_cb, 0, MMT_PERIODIC);
        clock_us = (mode == 1 || mode == 7) ? 20000 : (mode == 5 ? 10000 : 1000);
        mmt_deliver();
        CHECK(!callback_error && hits == 1 && other_hits == 0);
        if (mode == 3 || mode == 5 || mode == 7) {
            CHECK(replacement);
            action = 0;
            mmt_deliver(); CHECK(other_hits == 1);
        }
        if (mode == 6) {
            clock_us = 2000; mmt_deliver(); CHECK(hits == 1);
            action = 0; mmt_hold(0); mmt_deliver(); CHECK(hits == 2);
        }
    }
    return 0;
}
static int test_hold_sleep(void)
{
    u32 id, ms;
    reset(); CHECK(periodic(1));
    mmt_hold(1); mmt_hold(1); clock_us = 1000;
    mmt_deliver(); CHECK(hits == 0);
    mmt_hold(0); mmt_deliver(); CHECK(hits == 0);
    mmt_hold(0); mmt_deliver(); CHECK(hits == 1);
    mmt_hold(0); clock_us = 2000; mmt_deliver(); CHECK(hits == 2);
    reset(); mmt_hold(1);
    for (u32 i = 0; i < 16; ++i) {
        id = periodic(1); CHECK(id && mmt_kill_event(id) == 0);
    }
    CHECK(!periodic(1) && mmt_next_due_ms(&ms) == 0);
    mmt_deliver(); CHECK(!periodic(1));
    mmt_hold(0); mmt_deliver(); CHECK(periodic(1));
    reset(); CHECK(periodic(1));
    mmt_sleep_ms(0); CHECK(yields == 1 && hits == 0);
    mmt_sleep_ms(3); CHECK(yields == 4 && hits == 4 && clock_us == 4000);
    CHECK(!callback_error);
    return 0;
}
void _start(void)
{
    int rc = test_clock();
    if (!rc) rc = test_events();
    if (!rc) rc = test_catchup();
    if (!rc) rc = test_callbacks();
    if (!rc) rc = test_hold_sleep();
    if (rc) {
        char line[] = "FAIL line 000\n";
        line[10] = '0' + rc / 100; line[11] = '0' + rc / 10 % 10;
        line[12] = '0' + rc % 10;
        __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(line), "d"(14) : "memory");
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? 1 : 0));
    __builtin_unreachable();
}
