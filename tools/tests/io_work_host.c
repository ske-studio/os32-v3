#include "types.h"
static unsigned int irq_depth, irq_calls, compat_hits, compat_count;
static unsigned int irq_save(void) { unsigned int old = irq_depth; ++irq_depth; ++irq_calls; return old; }
static void irq_restore(unsigned int old) { irq_depth = old; }
#include "io_work.c"
unsigned int idle_work_compat_count(void) { return compat_count; }
void idle_work_compat_run(void) { ++compat_hits; }

static u32 ticks, read_cost, source, generation, factor, reads, conversions, fail_read, fail_convert;
static u32 pg, lookups, trace[128], traced, cost[18], units[18], mode[18];
static u8 tiers[18];
static u32 budgets[18], observed[18];
static int results[18];
enum { REQUEST = 1, NEST = 2, CHANGE = 4, FAIL_TAKE = 8, FAIL_POST = 16, POLICY = 32 };
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); __builtin_unreachable(); }
static void fail(unsigned int line)
{
    char buf[16]; unsigned int n = 0;
    buf[n++] = '\n';
    do { buf[n++] = '0' + line % 10; line /= 10; } while (line);
    for (unsigned int i = 0; i < n / 2; ++i) { char c = buf[i]; buf[i] = buf[n - i - 1]; buf[n - i - 1] = c; }
    __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(buf), "d"(n) : "memory");
    finish(1);
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static int read_clock(u32 *c, u32 *s, u32 *g)
{
    CHECK(!irq_depth); ++reads;
    if (reads == fail_read) return OS32_ERR_AGAIN;
    *c = ticks; ticks += read_cost; *s = source; *g = generation; return 0;
}
static int to_count(u32 us, u32 s, u32 g, u32 *out)
{
    CHECK(!irq_depth); ++conversions;
    if (fail_convert == 2) *out = 1;
    if (fail_convert || s != source || g != generation) return OS32_ERR_AGAIN;
    *out = us * factor; return 0;
}
static int read_us(u32 *lo, u32 *hi) { *lo = 123; *hi = 456; return 0; }
static const struct io_clock_ops ops = { read_clock, to_count, read_us };
int io_policy_limits(u32 id, u8 *t, u32 *b, u32 *low, u32 *high)
{
    if (!id || id > 17) return OS32_ERR_NOTFOUND;
    *t = id == 1 ? 1 : id == 3 ? 3 : 2;
    *b = id == IO_SVC_LAN_BULK ? 440 : 120; *low = 10; *high = 500; return 0;
}
int io_policy_lookup(u32 id, u8 *t, u32 *b) { ++lookups; *t = tiers[id]; *b = budgets[id]; return 0; }
u32 io_policy_generation(void) { return pg; }
static int step(void *state, struct io_slice *s)
{
    u32 id = (u32)state, flags = mode[id];
    CHECK(!irq_depth && traced < 128); trace[traced++] = id; observed[id] = s->limit_count;
    CHECK(io_unregister(id) == OS32_ERR_BUSY);
    if (flags & REQUEST) io_request(id);
    if (flags & NEST) {
        mode[id] &= ~NEST;
        u32 before = reads; CHECK(io_dispatch(500) == 0); CHECK(reads == before);
    }
    if (flags & CHANGE) ++generation;
    if (flags & FAIL_TAKE) fail_read = reads + 1;
    for (u32 n = 0; n < units[id] && io_slice_take(s); ++n) ticks += cost[id];
    if (flags & FAIL_POST) fail_read = reads + 1;
    if (flags & POLICY) { tiers[3] = 1; budgets[3] = 10; ++pg; }
    return results[id];
}
static struct io_desc desc(u32 id)
{
    struct io_desc d = { .id = id, .max_unit_us = id == 4 ? 110 : 10, .step = step, .state = (void *)id };
    io_policy_limits(id, &d.default_tier, &d.default_budget_us, &d.budget_min_us, &d.budget_max_us);
    return d;
}
static void add(u32 id) { struct io_desc d = desc(id); CHECK(io_register(&d) == 0); io_request(id); }
static void reset(void)
{
    count = running = compat_count = cache_valid = policy_valid = clock_failed = 0;
    pending[0] = pending[1] = pending[2] = 0;
    rounds[0] = rounds[1] = cursor[0] = cursor[1] = fair_next = 0;
    stats = (__typeof__(stats)){0};
    ticks = read_cost = reads = conversions = fail_read = fail_convert = traced = lookups = compat_hits = irq_calls = 0;
    source = generation = factor = pg = 1;
    for (u32 id = 1; id <= 17; ++id) {
        u32 low, high; io_policy_limits(id, &tiers[id], &budgets[id], &low, &high);
        cost[id] = 10; units[id] = 1; mode[id] = 0; results[id] = IO_MORE;
    }
    io_clock_set_ops(&ops);
}
static void registration(void)
{
    reset(); struct io_desc d = desc(1);
    CHECK(io_register(0) == OS32_ERR_INVAL);
    d.id = 99; CHECK(io_register(&d) == OS32_ERR_INVAL); d = desc(1);
    d.default_tier = 4; CHECK(io_register(&d) == OS32_ERR_INVAL); d = desc(1);
    d.default_budget_us = 501; CHECK(io_register(&d) == OS32_ERR_INVAL); d = desc(1);
    d.budget_min_us = 9; CHECK(io_register(&d) == OS32_ERR_INVAL); d = desc(1);
    CHECK(io_register(&d) == 0 && io_register(&d) == OS32_ERR_EXIST);
    for (u32 id = 2; id <= 16; ++id) add(id);
    d = desc(17); CHECK(io_register(&d) == OS32_ERR_FULL);
    io_request(1); CHECK(reads == 0 && conversions == 0 && traced == 0);
    CHECK(io_dispatch(500) == 16 && reads <= 1 + 2 * 16 + 16);
    CHECK(io_unregister(8) == 0 && io_unregister(8) == OS32_ERR_NOTFOUND);
    add(17); traced = 0; CHECK(io_dispatch(500) == 16);
    CHECK(trace[14] == 17 && trace[15] == 3); /* Tier 2 registration order survives holes. */
}
static void slices(void)
{
    reset(); struct io_slice s; u32 lo, hi;
    CHECK(io_dispatch(500) == 0 && reads == 0 && conversions == 0);
    CHECK(io_clock_us(&lo, &hi) == 0 && lo == 123 && hi == 456);
    io_clock_set_ops(0); CHECK(io_clock_us(&lo, &hi) == OS32_ERR_NOSYS);
    CHECK(io_slice_begin(&s, 440, 4) == OS32_ERR_NOSYS);
    io_clock_set_ops(&ops);
    for (factor = 1; factor <= 3; ++factor) {
        source = factor; ticks = ~0u - 5;
        CHECK(io_slice_begin(&s, 440, 4) == 0 && s.limit_count == 440 * factor);
        for (unsigned int n = 0; n < 4; ++n) { CHECK(io_slice_take(&s)); ticks += 100 * factor; }
        CHECK(!io_slice_take(&s));
        CHECK(io_slice_begin(&s, 10, 2) == 0); ticks += 10 * factor; CHECK(!io_slice_take(&s));
        CHECK(io_slice_begin(&s, 10, 2) == 0); ++generation; CHECK(!io_slice_take(&s));
        CHECK(io_slice_begin(&s, 10, 2) == 0); ++source; CHECK(!io_slice_take(&s));
    }
    fail_convert = 1; CHECK(io_slice_begin(&s, 10, 1) == OS32_ERR_AGAIN);
    reset(); compat_count = 1; io_clock_set_ops(0);
    CHECK(io_dispatch(500) == 1 && compat_hits == 1 && !running);
    io_clock_set_ops(&ops); CHECK(io_dispatch(500) == 1 && compat_hits == 2);
}
static void scheduling(void)
{
    reset(); add(3); add(2); add(4); add(1);
    CHECK(io_dispatch(500) == 4);
    CHECK(trace[0] == 1 && trace[1] == 2 && trace[2] == 4 && trace[3] == 3);
    u32 converted = conversions, looked = lookups;
    CHECK(io_dispatch(500) == 4 && conversions == converted && lookups == looked);
    ++generation; CHECK(io_dispatch(500) == 4 && conversions > converted);
    mode[1] = POLICY; CHECK(io_dispatch(500) == 4 && observed[3] == 120);
    mode[1] = 0; traced = 0; CHECK(io_dispatch(500) == 4 && observed[3] == 10);
    CHECK(trace[0] == 3 && trace[1] == 1); /* First registered, new Tier 1 next round. */
    reset(); add(1); add(2); add(3); add(5);
    for (unsigned int n = 0; n < 7; ++n) CHECK(io_dispatch(10) == 1 && trace[n] == 1);
    CHECK(io_dispatch(10) == 1 && trace[7] == 2);
    CHECK(io_dispatch(10) == 1 && trace[8] == 3); /* Mature right carries across round 8. */
    for (unsigned int n = 0; n < 6; ++n) CHECK(io_dispatch(10) == 1);
    CHECK(io_dispatch(10) == 1 && trace[15] == 5); /* Tier-local cursor. */
    reset(); add(1); add(2); add(3);
    for (unsigned int n = 0; n < 7; ++n) CHECK(io_dispatch(10) == 1);
    CHECK(io_dispatch(20) == 2 && trace[7] == 2 && trace[8] == 3);
    reset(); add(1); add(2); add(3);
    for (unsigned int n = 0; n < 8; ++n) CHECK(io_dispatch(10) == 1);
    for (unsigned int n = 0; n < 7; ++n) CHECK(io_dispatch(5) == 1);
    CHECK(io_dispatch(10) == 1 && trace[15] == 3); /* Both mature again: other Tier first. */
    reset(); add(1); add(2); add(3);
    for (unsigned int n = 0; n < 7; ++n) CHECK(io_dispatch(10) == 1);
    traced = 0; read_cost = 1; cost[1] = 9;
    /* Tier 3 fits at selection, but its pre-read leaves only 9 counts. */
    CHECK(io_dispatch(22) == 2 && trace[0] == 2 && trace[1] == 1);
    CHECK(observed[1] == 9 && rounds[1] == IO_FAIR_ROUNDS && cursor[1] == 0);
    read_cost = 0;
    CHECK(io_dispatch(10) == 1 && trace[2] == 3); /* Unserved right survives. */
}
static void requests_and_failures(void)
{
    reset(); add(1); add(2); mode[1] = REQUEST | NEST; results[1] = IO_DONE;
    CHECK(io_dispatch(500) == 2 && (pending[0] & 1));
    mode[1] = 0; results[2] = IO_WAIT; CHECK(io_dispatch(500) == 2 && !any_pending());
    CHECK(io_dispatch(500) == 0);
    io_request(1); results[1] = -99; mode[1] = REQUEST;
    CHECK(io_dispatch(500) == 1 && stats.last_error == -99 && pending[0]);
    mode[1] = 0; CHECK(io_dispatch(500) == 1 && !pending[0]);
    for (u32 flag = CHANGE; flag <= FAIL_POST; flag <<= 1) {
        reset(); add(1); add(2); mode[1] = flag; results[1] = IO_MORE;
        CHECK(io_dispatch(500) == 1 && traced == 1 && pending[0] && pending[1]);
        CHECK(stats.clock_failures && !running);
        mode[1] = 0; fail_read = 0; CHECK(io_dispatch(500) == 2);
    }
    const int terminal[] = { -99, IO_DONE, IO_WAIT };
    for (unsigned int n = 0; n < 3; ++n) for (unsigned int requested = 0; requested < 2; ++requested) {
        reset(); add(1); add(2); results[1] = terminal[n];
        mode[1] = FAIL_POST | (requested ? REQUEST : 0);
        CHECK(io_dispatch(500) == 1 && traced == 1 && !!pending[0] == requested);
        CHECK(pending[1] && stats.clock_failures == 1 && !running);
        CHECK(stats.last_error == (terminal[n] < 0 ? terminal[n] : 0));
        mode[1] = 0; fail_read = 0;
        CHECK(io_dispatch(500) == (requested ? 2 : 1));
        CHECK(trace[1] == (requested ? 1u : 2u));
    }
    reset(); add(1); fail_read = 1; CHECK(io_dispatch(500) == 0 && pending[0]);
    fail_read = 0; fail_convert = 1; CHECK(io_dispatch(500) == 0 && pending[0]);
    fail_convert = 0; CHECK(io_dispatch(500) == 1);
    reset(); add(4); units[4] = 4; cost[4] = 110;
    CHECK(io_dispatch(500) == 1 && ticks == 440 && reads <= 7 && observed[4] == 440);
    CHECK(stats.total_count == 440 && stats.max_count == 440);
    CHECK(io_dispatch(120) == 1 && ticks == 660 && observed[4] == 120 && stats.overruns == 1);
    CHECK(stats.total_count == 660 && stats.max_count == 440);
    fail_convert = 2; CHECK(io_dispatch(100) == 0); fail_convert = 0;
    CHECK(io_dispatch(120) == 1 && observed[4] == 120);
    reset(); add(1); add(4); cost[1] = 80; units[4] = 4; cost[4] = 110;
    CHECK(io_dispatch(500) == 2 && observed[4] == 420 && ticks == 520);
    reset(); add(4); budgets[4] = 500; ++pg; units[4] = 10; cost[4] = 110;
    CHECK(io_dispatch(1000) == 1 && observed[4] == 500 && ticks == 550);
    CHECK(stats.reads == reads && stats.callbacks == 1 && stats.overruns == 1);
    CHECK(io_dispatch(0) == OS32_ERR_INVAL && !running);
    reset(); add(1); add(2); read_cost = 10;
    CHECK(io_dispatch(35) == 1 && traced == 1 && reads <= 1 + 2 + 1);
    CHECK(pending[1] && stats.reads == reads);
}
void _start(void) { registration(); slices(); scheduling(); requests_and_failures(); finish(0); }
