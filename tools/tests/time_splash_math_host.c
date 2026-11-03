/* Independent PIT/exit accounting model, with the actual pure math and P2 API. */
#include "types.h"
#include "kernel/time/time_splash_math.h"
#include "kernel/time/ktime.h"
#include "arch/x86/kernel/idt.h"
#include "platform_timer.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
#define REQUIRE(x) do { if (!(x)) finish(__LINE__ % 250 + 1); } while (0)
volatile u32 tick_count;
static struct pit_setup pit;
static struct platform_timer_sample feed[16];
static unsigned int feed_len, feed_pos;
const struct pit_setup *pit_get_setup(void) { return &pit; }
void platform_timer_sample(struct platform_timer_sample *s)
{ REQUIRE(feed_pos < feed_len); *s = feed[feed_pos++]; }

struct range { u32 lo, hi; };
struct costs { u32 budget, loop_min; struct range tail, enter, loop, retry, head; };
struct model { u32 tick; int used; unsigned int reads; struct TimeSplashMathResult result; unsigned long long a, b, lo, hi; };

/* Samples are supplied as raw IRR/latch/IRR triples, including rejected tries. */
static int model_sample(u32 reload, struct TimeSplashSample *s, unsigned int *tries)
{
    for (*tries = 0; *tries < TIME_RETRY_MAX; ++*tries) {
        struct platform_timer_sample x;
        if (feed_pos == feed_len) return -1;
        x = feed[feed_pos++];
        if (!x.count || x.count > reload) return -1;
        if (x.p1 == x.p2) { s->count = x.count; s->pending = x.p2; return 0; }
    }
    return -1;
}
static int model_settle(struct model *m, struct TimeSplashMathInput in, struct costs c)
{
    struct TimeSplashSample e = in.end, next;
    unsigned int n, tries, k;
    unsigned long long step, wait, lag, a, b;
    if (m->used) return -1;
    m->used = 1;
    if (c.budget == 0) return -1;
    if (!c.loop_min || c.budget >= in.reload) return -1;
    if (e.count <= c.budget) {
        k = c.budget / c.loop_min + 1;
        step = c.loop.hi + (TIME_RETRY_MAX - 1) * (unsigned long long)c.retry.hi + c.head.hi + c.tail.hi;
        wait = c.tail.hi + c.enter.hi + k * step;
        lag = c.tail.hi + c.enter.hi + step;
        if (wait >= in.reload || lag + c.budget >= in.reload) return -1;
        a = c.tail.lo + c.enter.lo; b = c.tail.hi + c.enter.hi;
        for (n = 0; n < k; ++n) {
            if (model_sample(in.reload, &next, &tries)) return -1;
            m->reads++;
            a += c.loop.lo + tries * c.retry.lo + c.head.lo;
            b += c.loop.hi + tries * c.retry.hi + c.head.hi;
            if (next.count > e.count) break;
            a += c.tail.lo; b += c.tail.hi;
        }
        if (n == k || next.count <= c.budget || in.elapsed_min > ~0ULL - a || in.elapsed_max > ~0ULL - b) return -1;
        in.end = next;
        in.elapsed_min += a;
        in.elapsed_max += b;
        m->a = a; m->b = b; m->lo = in.elapsed_min; m->hi = in.elapsed_max;
    }
    if (time_splash_math(&in, &m->result) ||
        in.end.pending != (u32)(in.start.pending || m->result.wraps) || m->result.tick_adjust < 0) return -1;
    m->tick += (u32)m->result.tick_adjust;
    return 0;
}
static void put(unsigned int i, u32 count, int p1, int p2)
{ feed[i] = (struct platform_timer_sample){ count, p1, p2 }; }
static void tests(void)
{
    struct TimeSplashMathInput in;
    struct TimeSplashMathResult out;
    struct costs c = { 4, 1, {1,1}, {1,1}, {1,1}, {1,1}, {1,1} };
    struct model m;
    /* Sweep every entry phase, both clocks, all four pending pairs and
       zero/one/two wraps, using elapsed time as the independent oracle. */
    for (unsigned int j = 0; j < 2; j++) {
        u32 r = j ? 24576 : 19968;
        for (u32 start = 1; start <= r; start++) for (u32 p = 0; p < 4; p++) for (u32 w = 0; w < 3; w++) {
            u32 end = start == r ? 1 : start + 1;
            unsigned long long d = (unsigned long long)(w + (end > start)) * r + start - end;
            in = (struct TimeSplashMathInput){{start,p / 2},{end,p % 2},r,d,d};
            REQUIRE(time_splash_math(&in, &out) == 0);
            REQUIRE(out.wraps == w + (end > start));
            REQUIRE(out.tick_adjust == (long long)out.wraps + (long long)(p / 2) - (long long)(p % 2));
            in.elapsed_max += r;
            REQUIRE(time_splash_math(&in, &out) == -1);
        }
    }
    /* Counterexample: 34575 cycles, then four cycles to the next latch. */
    in = (struct TimeSplashMathInput){{10000,0},{1,1},24576,34575,34575};
    feed_pos = 0; feed_len = 1; put(0,24573,1,1);
    m = (struct model){.tick=7};
    REQUIRE(model_settle(&m,in,c) == 0 && m.result.wraps == 2 && m.result.tick_adjust == 1 && m.tick == 8);
    REQUIRE(m.a == 4 && m.b == 4 && m.lo == 34579 && m.hi == 34579); /* fixture below uses four cycles */
    REQUIRE(model_settle(&m,in,c) == -1 && m.tick == 8);
    /* Equality needs a wait; a stable sample after an IRR race. */
    in = (struct TimeSplashMathInput){{10,0},{4,0},24576,6,6};
    feed_pos=0; feed_len=2; put(0,3,0,1); put(1,24575,1,1);
    m = (struct model){.tick=7};
    REQUIRE(model_settle(&m,in,c) == 0 && m.reads == 1 && m.a == 5 && m.b == 5 && m.tick == 7);
    /* Hand calculation: 2+3+4+2+2+4+5+2=24, and
       3+5+6+4+3+6+7+4=38; 96+24..96+38 = 120..134.
       The final latch implies 100+24576-24550 = 126 cycles. */
    c = (struct costs){4,1,{2,3},{3,5},{4,6},{5,7},{2,4}};
    in = (struct TimeSplashMathInput){{100,0},{4,0},24576,96,96};
    feed_pos=0; feed_len=3; put(0,3,0,0); put(1,2,0,1); put(2,24550,1,1);
    m = (struct model){.tick=7};
    REQUIRE(model_settle(&m,in,c) == 0 && m.reads == 2 && feed_pos == 3);
    REQUIRE(m.a == 24 && m.b == 38 && m.lo == 120 && m.hi == 134);
    REQUIRE(m.result.wraps == 1 && m.result.tick_adjust == 0 && m.tick == 7);
    c = (struct costs){4,1,{1,1},{1,1},{1,1},{1,1},{1,1}};
    /* Early drop still settles the elapsed interval. */
    in = (struct TimeSplashMathInput){{100,0},{90,0},19968,10,10};
    m = (struct model){.tick=9}; feed_pos=feed_len=0;
    REQUIRE(model_settle(&m,in,c) == 0 && m.tick == 9 && feed_pos == 0);
    c.budget=0; m = (struct model){.tick=9};
    REQUIRE(model_settle(&m,in,c) == -1 && m.tick == 9);
    c.budget=4; in.end.count=4; in.elapsed_min=in.elapsed_max=96;
    feed_pos=0; feed_len=5; for (unsigned int i=0;i<5;i++) put(i,4,0,0);
    m = (struct model){.tick=9};
    REQUIRE(model_settle(&m,in,c) == -1 && m.reads == 5 && feed_pos == 5 && m.tick == 9);
    feed_pos=0; feed_len=3; for (unsigned int i=0;i<3;i++) put(i,3,0,1);
    m = (struct model){.tick=9};
    REQUIRE(model_settle(&m,in,c) == -1 && feed_pos == 3 && m.tick == 9);
    c.loop.hi=19968; m = (struct model){.tick=9}; feed_pos=0;
    REQUIRE(model_settle(&m,in,c) == -1 && feed_pos == 0 && m.tick == 9);
    /* Real P2 sampling and unconfigured budget, including retry and consume. */
    pit = (struct pit_setup){ .reload=19968, .mode=PIT_MODE_TIMER0, .valid=1 };
    tick_count=23; feed_pos=0; feed_len=3;
    put(0,100,0,1); put(1,99,1,1); put(2,90,1,1);
    REQUIRE(ktime_splash_sample(&in.start) == 0 && in.start.count == 99 && feed_pos == 2);
    REQUIRE(ktime_splash_sample(&in.end) == 0 && in.end.count == 90);
    in.reload=19968; in.elapsed_min=in.elapsed_max=9;
    REQUIRE(ktime_splash_settle(&in) == -1 && tick_count == 23);
    REQUIRE(ktime_splash_settle(&in) == -1 && tick_count == 23);
    feed_pos=0; feed_len=1; put(0,0,0,0);
    REQUIRE(ktime_splash_sample(&in.start) == -1 && tick_count == 23);
}
void _start(void) { tests(); finish(0); }
