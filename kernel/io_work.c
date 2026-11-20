#include "io_work.h"
#include "idle_work.h"
#include "io_clock.h"
#include "io_policy.h"
#include "io.h"
#include "os32_kapi_shared.h"

#define IO_FAIR_ROUNDS 8u
#define IO_SLOTS 16u

static struct {
    struct io_desc desc;
    u32 budget_us, budget_count, unit_count;
    u8 tier;
    struct {
        u32 us, count, source, gen, policy;
        const struct io_clock_ops *ops;
        int valid;
    } limit; /* P8 conversions share P1's fixed service table. */
} slots[IO_SLOTS];
static unsigned int count, running;
static volatile u32 pending[3];
static u32 policy_gen, cache_source, cache_generation, cache_us, pass_count;
static unsigned int cache_valid, policy_valid, clock_failed;
static unsigned int rounds[2], cursor[2], fair_next;
static const struct io_clock_ops *clock_ops;
static struct {
    u32 max_count, total_count, callbacks, overruns, clock_failures, reads;
    int last_error;
} stats;

u32 io_dispatch_total_count(void) { return stats.total_count; }

static u32 minimum(u32 a, u32 b) { return a < b ? a : b; }
static u32 any_pending(void) { return pending[0] | pending[1] | pending[2]; }
static int clock_error(int rc)
{
    ++stats.clock_failures;
    if (running) clock_failed = 1;
    return rc;
}
static int raw_read(u32 *now, u32 *source, u32 *gen)
{
    if (!clock_ops || !clock_ops->read) return clock_error(OS32_ERR_NOSYS);
    ++stats.reads;
    int rc = clock_ops->read(now, source, gen);
    return rc < 0 ? clock_error(rc) : rc;
}
static int convert(u32 us, u32 source, u32 gen, u32 *out)
{
    if (!clock_ops || !clock_ops->us_to_count) return clock_error(OS32_ERR_NOSYS);
    int rc = clock_ops->us_to_count(us, source, gen, out);
    if (rc < 0) return clock_error(rc);
    return *out < 0x80000000u ? 0 : clock_error(OS32_ERR_AGAIN);
}
static int token_read(const struct io_slice *s, u32 *now)
{
    u32 source, gen;
    if (raw_read(now, &source, &gen) < 0) return 0;
    if (source != s->clock_source || gen != s->clock_generation)
        return clock_error(OS32_ERR_AGAIN), 0;
    return 1;
}
void io_clock_set_ops(const struct io_clock_ops *ops)
{
    clock_ops = ops;
    cache_valid = 0;
}
int io_clock_us(u32 *lo, u32 *hi)
{
    return clock_ops && clock_ops->read_us ? clock_ops->read_us(lo, hi) : OS32_ERR_NOSYS;
}
int io_slice_begin(struct io_slice *s, u32 budget_us, u32 max_units)
{
    struct io_slice next = { .max_units = max_units };
    int rc = raw_read(&next.start_count, &next.clock_source, &next.clock_generation);
    if (rc < 0) return rc;
    rc = convert(budget_us, next.clock_source, next.clock_generation, &next.limit_count);
    if (rc < 0) return rc;
    *s = next;
    return 0;
}
int io_slice_limit(struct io_slice *s, u32 budget_us, u32 max_units)
{
    /* P8: retain conversions by token/policy, like the dispatch budget table.
     * Sixteen entries match the maximum number of resident providers. */
    static unsigned int next;
    u32 now, bound_count, policy = io_policy_generation();
    if (!token_read(s, &now)) return OS32_ERR_AGAIN;
    unsigned int i;
    for (i = 0; i < IO_SLOTS; ++i)
        if (slots[i].limit.valid && slots[i].limit.ops == clock_ops && slots[i].limit.us == budget_us &&
            slots[i].limit.source == s->clock_source && slots[i].limit.gen == s->clock_generation &&
            slots[i].limit.policy == policy) break;
    if (i < IO_SLOTS) bound_count = slots[i].limit.count;
    else {
        int rc = convert(budget_us, s->clock_source, s->clock_generation, &bound_count);
        if (rc < 0) return rc;
        i = next++ % IO_SLOTS;
        slots[i].limit.us = budget_us; slots[i].limit.count = bound_count;
        slots[i].limit.source = s->clock_source; slots[i].limit.gen = s->clock_generation;
        slots[i].limit.policy = policy; slots[i].limit.ops = clock_ops; slots[i].limit.valid = 1;
    }
    u32 elapsed = now - s->start_count;
    u32 remaining = elapsed < s->limit_count ? s->limit_count - elapsed : 0;
    u32 units_left = s->units < s->max_units ? s->max_units - s->units : 0;
    s->start_count = now;
    s->limit_count = minimum(remaining, bound_count);
    s->max_units = minimum(units_left, max_units);
    s->units = 0;
    return 0;
}
int io_slice_take(struct io_slice *s)
{
    u32 now;
    if ((running && clock_failed) || s->units >= s->max_units) return 0;
    if (!token_read(s, &now) || now - s->start_count >= s->limit_count) return 0;
    ++s->units;
    return 1;
}
int io_dispatch_running(void) { return running; }
static int find(u32 id)
{
    for (unsigned int i = 0; i < count; ++i)
        if (slots[i].desc.id == id) return (int)i;
    return -1;
}
int io_register(const struct io_desc *d)
{
    u8 tier; u32 budget, low, high;
    /* Allowed while io_dispatch runs (a provider may bind from a compat
     * callback, e.g. PPC net_start): the slot is only appended, its budget
     * count stays 0 until the next round's refresh, so this round skips it. */
    if (!d || !d->step || io_policy_limits(d->id, &tier, &budget, &low, &high) < 0)
        return OS32_ERR_INVAL;
    if (d->default_tier < 1 || d->default_tier > 3 || d->default_tier != tier ||
        d->default_budget_us != budget || d->budget_min_us != low ||
        d->budget_max_us != high || budget < low || budget > high || !d->max_unit_us)
        return OS32_ERR_INVAL;
    if (find(d->id) >= 0) return OS32_ERR_EXIST;
    if (count == IO_SLOTS) return OS32_ERR_FULL;
    if (io_policy_lookup(d->id, &tier, &budget) < 0) return OS32_ERR_INVAL;
    unsigned int flags = irq_save();
    slots[count].budget_count = slots[count].unit_count = 0; /* skipped until refresh */
    slots[count].limit.valid = 0;
    slots[count].desc = *d;
    slots[count].tier = tier;
    slots[count++].budget_us = budget;
    cache_valid = 0;
    irq_restore(flags);
    return 0;
}
int io_unregister(u32 id)
{
    if (running) return OS32_ERR_BUSY;
    int i = find(id);
    if (i < 0) return OS32_ERR_NOTFOUND;
    unsigned int flags = irq_save();
    for (unsigned int t = 0; t < 3; ++t)
        pending[t] = (pending[t] & ((1u << i) - 1)) | ((pending[t] >> (i + 1)) << i);
    for (unsigned int j = (unsigned int)i; j + 1 < count; ++j) slots[j] = slots[j + 1];
    --count;
    slots[count].desc = (struct io_desc){0};
    for (unsigned int t = 0; t < 2; ++t)
        if (cursor[t] > (unsigned int)i) --cursor[t];
    cache_valid = 0;
    irq_restore(flags);
    return 0;
}
void io_request(u32 id)
{
    unsigned int flags = irq_save();
    int i = find(id);
    if (i >= 0) pending[slots[i].tier - 1] |= 1u << i;
    irq_restore(flags);
}
static void refresh_policy(void)
{
    u32 gen = io_policy_generation();
    if (policy_valid && policy_gen == gen) return;
    for (unsigned int i = 0; i < count; ++i) {
        u8 tier; u32 budget;
        io_policy_lookup(slots[i].desc.id, &tier, &budget);
        unsigned int flags = irq_save();
        u32 bit = 1u << i;
        if (pending[slots[i].tier - 1] & bit) {
            pending[slots[i].tier - 1] &= ~bit;
            pending[tier - 1] |= bit;
        }
        slots[i].tier = tier;
        slots[i].budget_us = budget;
        irq_restore(flags);
    }
    policy_gen = gen;
    policy_valid = 1;
    cache_valid = 0;
}
static int refresh_counts(const struct io_slice *pass, u32 us)
{
    int same = cache_valid && cache_source == pass->clock_source &&
        cache_generation == pass->clock_generation;
    if (!same) {
        cache_valid = 0;
        for (unsigned int i = 0; i < count; ++i) {
            if (convert(slots[i].budget_us, pass->clock_source, pass->clock_generation,
                        &slots[i].budget_count) < 0 ||
                convert(slots[i].desc.max_unit_us, pass->clock_source, pass->clock_generation,
                        &slots[i].unit_count) < 0 ||
                convert(slots[i].desc.default_budget_us, pass->clock_source, pass->clock_generation,
                        &slots[i].limit.count) < 0) return 0;
            /* P8 limits use the default NIC ceiling even when policy raises
             * its active budget. Prepare it at the P1 round-entry boundary. */
            slots[i].limit.us = slots[i].desc.default_budget_us;
            slots[i].limit.source = pass->clock_source;
            slots[i].limit.gen = pass->clock_generation;
            slots[i].limit.policy = policy_gen;
            slots[i].limit.ops = clock_ops;
            slots[i].limit.valid = 1;
        }
    }
    if (!same || cache_us != us) {
        cache_valid = 0;
        if (convert(us, pass->clock_source, pass->clock_generation, &pass_count) < 0) return 0;
    }
    cache_source = pass->clock_source;
    cache_generation = pass->clock_generation;
    cache_us = us;
    cache_valid = 1;
    return 1;
}
static int first(u32 mask) { return mask ? __builtin_ctz(mask) : -1; }
static int tier_pick(u32 visited)
{
    int i = -1;
    for (unsigned int t = 0; t < 3 && i < 0; ++t)
        i = first(pending[t] & ~visited);
    return i;
}
static int fair_pick(u32 mask, unsigned int start)
{
    int i = first(mask & (0xffffu << start));
    return i >= 0 ? i : first(mask);
}
/* Compatibility callbacks remain independent of clock readiness. */
static void compat_run(void)
{
    idle_work_compat_run();
}
int io_dispatch(u32 budget_us)
{
    if (running) return 0;
    if (idle_work_compat_count() == 0 && !any_pending()) return 0;
    if (!budget_us) return OS32_ERR_INVAL;
    running = 1;
    clock_failed = 0;
    refresh_policy();
    struct io_slice pass = {0};
    u32 visited = 0, now = 0, elapsed = 0;
    unsigned int served = 0, ready = (!!pending[1]) | ((!!pending[2]) << 1);
    int calls = 0;
    if (raw_read(&pass.start_count, &pass.clock_source, &pass.clock_generation) < 0 ||
        !refresh_counts(&pass, minimum(budget_us, IO_PASS_US))) goto finish;
    pass.limit_count = pass_count;
    now = pass.start_count;
    while (!clock_failed && elapsed < pass.limit_count) {
        int i = -1, fair = -1;
        u32 remaining = pass.limit_count - elapsed;
        for (unsigned int n = 0; n < 2; ++n) {
            unsigned int t = (fair_next + n) & 1;
            if (!(served & (1u << t)) && rounds[t] >= IO_FAIR_ROUNDS - 1) {
                int candidate = fair_pick(pending[t + 1] & ~visited, cursor[t]);
                if (candidate >= 0 && slots[candidate].unit_count &&
                    slots[candidate].unit_count <= remaining) { i = candidate; fair = (int)t; break; }
            }
        }
        if (i < 0) i = tier_pick(visited);
        if (i < 0) break;
        if (!slots[i].budget_count) { visited |= 1u << i; continue; }
        /* Entry doubles as the first pre-read; a final expired pre-read then
         * still fits 1+2K+M. Setup time is charged to the first slice. */
        if (calls && !token_read(&pass, &now)) break;
        elapsed = now - pass.start_count;
        if (elapsed >= pass.limit_count) break;
        remaining = pass.limit_count - elapsed;
        if (fair >= 0 && slots[i].unit_count > remaining) {
            fair = -1; /* Preserve the unserved right; use ordinary Tier order. */
            i = tier_pick(visited);
            if (i < 0) break;
        }
        struct io_slice slice = { now, minimum(slots[i].budget_count, remaining),
                                  fair >= 0 ? 1u : ~0u, 0, pass.clock_source, pass.clock_generation };
        if (!slice.limit_count) { visited |= 1u << i; continue; }
        if (fair >= 0) {
            slice.limit_count = minimum(slice.limit_count, slots[i].unit_count);
            served |= 1u << fair;
            cursor[fair] = ((unsigned int)i + 1) % IO_SLOTS;
            fair_next = (unsigned int)fair ^ 1u;
        }
        unsigned int flags = irq_save();
        pending[slots[i].tier - 1] &= ~(1u << i);
        irq_restore(flags);
        visited |= 1u << i;
        int result = slots[i].desc.step(slots[i].desc.state, &slice);
        ++calls;
        ++stats.callbacks;
        if (result < 0) stats.last_error = result;
        int valid = token_read(&pass, &now);
        if (valid) {
            elapsed = now - pass.start_count;
            if (now - slice.start_count > slice.limit_count) ++stats.overruns;
        }
        if (result == IO_MORE) io_request(slots[i].desc.id);
        if (!valid || clock_failed) break;
    }
finish:
    if (idle_work_compat_count()) {
        compat_run();
        calls += (int)idle_work_compat_count();
        stats.callbacks += idle_work_compat_count();
        if (!clock_failed && token_read(&pass, &now)) elapsed = now - pass.start_count;
    }
    if (!clock_failed) for (unsigned int t = 0; t < 2; ++t) {
        if (served & (1u << t)) rounds[t] = 0;
        else if (ready & (1u << t)) rounds[t] = minimum(rounds[t] + 1, IO_FAIR_ROUNDS);
        else rounds[t] = 0;
    }
    stats.total_count += elapsed;
    if (elapsed > stats.max_count) stats.max_count = elapsed;
    running = 0;
    return calls;
}
