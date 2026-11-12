/* Fixed storage only; no I/O in IRQ context. CPU reference: Intel SDM,
 * Vol.2 CPUID/RDTSC (CPUID.1:EDX.TSC[4], EFLAGS.ID availability test).
 * https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developers-manual.pdf
 * RDTSC is deliberately raw: no SSE/RDTSCP requirement, no frequency guess. */
#define TICK_PROBE_INTERNAL
#include "tick_probe.h"
#ifndef TICK_PROBE_HOST
#include "io.h"
#include "irq.h"
#include "kstring.h"
#else
extern void *kmemset(void *, int, unsigned int);
extern unsigned long long tp_host_clock;
extern unsigned int kctx_irq_depth;
#endif
static struct tp_snapshot data;
static struct tp_mark pending[TP_METRICS], interval, if0;
static unsigned int splash, epoch;
static unsigned long long window;
static struct tp_mark net_start, net_part[TP_NET_COUNT], pio_mark;
static unsigned long long net_values[TP_NET_COUNT], pio_cycles[TP_CONTEXTS];
static unsigned int net_mask, irq5_active;
static unsigned long if0_caller;
_Static_assert(sizeof(data) + sizeof(pending) + sizeof(interval) + sizeof(if0) +
               sizeof(splash) + sizeof(epoch) + sizeof(window) <= 16384,
               "diagnostic fixed storage exceeds 16KiB");

static unsigned long long now(void)
{
#ifdef TICK_PROBE_HOST
    return tp_host_clock;
#else
    unsigned int lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
    return ((unsigned long long)hi << 32) | lo;
#endif
}
static struct tp_mark mark(void)
{
    return (struct tp_mark){now(), epoch, splash, 1};
}
static void add_stat(struct tp_stat *s, unsigned long long d)
{
    unsigned long long bound = 1;
    unsigned int bin = 0;
    while (bin < TP_BINS - 1 && d > bound) { bin++; bound <<= 1; }
    if (s->count == ~0U || ~0ULL - s->total < d) { data.lost++; return; }
    s->count++; s->hist[bin]++; s->total += d;
    if (d > s->worst) s->worst = d;
}
static void add(unsigned int id, struct tp_mark m, unsigned long long end)
{
    if (!m.valid || end < m.t) { data.lost++; return; }
    unsigned int bank = m.splash || splash || m.epoch != epoch;
    add_stat(&data.bank[bank][id], end - m.t);
}
unsigned long long tick_probe_p99(const struct tp_stat *s)
{
    if (!s->count) return 0;
    unsigned int rank = s->count - s->count / 100, n = 0;
    for (unsigned int i = 0; i < TP_BINS; i++) {
        n += s->hist[i];
        if (n >= rank) {
            unsigned long long upper = 1ULL << i;
            return upper < s->worst && i < TP_BINS - 1 ? upper : s->worst;
        }
    }
    return s->worst;
}
void tick_probe_begin(unsigned int id)
{
    if (!data.running) return;
    if (pending[id].valid) data.lost++;
    pending[id] = mark();
}
void tick_probe_end(unsigned int id)
{
    if (!data.running) return;
    unsigned long long t = now(), duration = t - pending[id].t;
    add(id, pending[id], t);
    pending[id].valid = 0;
    pending[id].t = duration;
}
void tick_probe_irq_enter(unsigned int irq)
{
    if (!data.running) return;
    if (irq == 5) irq5_active = 1;
    if (irq == 0) {
        unsigned long long t = now();
        if (pending[TP_IRQ0].valid) data.lost++;
        pending[TP_IRQ0] = (struct tp_mark){t, epoch, splash, 1};
        if (interval.valid) add(TP_INTERVAL, interval, t);
        interval = (struct tp_mark){t, epoch, splash, 1};
    } else if (irq == 1 || irq == 10) {
        data.arrival[irq == 10] = now();
        if (data.arrivals[irq == 10] != ~0U) data.arrivals[irq == 10]++;
        else data.lost++;
    }
}
void tick_probe_irq_leave(unsigned int irq)
{
    if (irq == 5) { irq5_active = 0; return; }
    if (!data.running || irq != 0) return;
    /* Short path excludes timer AND separately charged logical work. */
    unsigned int bank = pending[TP_IRQ0].splash || splash ||
                        pending[TP_IRQ0].epoch != epoch;
    unsigned long long end = now(), total = end - pending[TP_IRQ0].t;
    unsigned long long logical = 0;
    for (unsigned int i = TP_TIMER; i <= TP_TIMEOUT; i++)
        if (i == TP_TIMER || i == TP_REFLECT || i == TP_TIMEOUT)
            logical += pending[i].t; /* completed per-IRQ durations */
    if (total >= logical) {
        struct tp_mark m = {end - (total - logical), epoch, bank, 1};
        add(TP_SHORT, m, end);
    } else data.lost++;
    add(TP_IRQ0, pending[TP_IRQ0], end);
    pending[TP_IRQ0].valid = 0;
    pending[TP_TIMER].t = pending[TP_REFLECT].t = pending[TP_TIMEOUT].t = 0;
}
void tick_probe_if_begin(unsigned int flags)
{
    if (data.running && !kctx_irq_depth && (flags & (1U << 9)) && !if0.valid)
        { if0 = mark(); if0_caller = (unsigned long)__builtin_return_address(0); }
}
void tick_probe_if_end(unsigned int flags)
{
    if (data.running && !kctx_irq_depth && (flags & (1U << 9)) && if0.valid) {
        unsigned long long end = now();
        unsigned int bank = if0.splash || splash || if0.epoch != epoch;
        if (end >= if0.t && end - if0.t > data.bank[bank][TP_IF0].worst)
            data.if0_caller[bank] = if0_caller;
        add(TP_IF0, if0, end);
        if0.valid = 0;
    }
}
static unsigned int context(void)
{
    if (!kctx_irq_depth) return TP_FOREGROUND;
    if (irq5_active) return TP_IRQ5_CTX;
    if (pending[TP_IRQ0].valid) return TP_IRQ0_CTX;
    return TP_CONTEXTS; /* other interrupts are not foreground */
}
void tick_probe_net_start(void)
{
    if (!data.running) return;
    if (net_start.valid) data.lost++;
    net_start = mark(); net_mask = 0;
}
void tick_probe_net_begin(unsigned int id)
{
    if (!data.running || !net_start.valid) return;
    if (net_part[id].valid) data.lost++;
    net_part[id] = mark();
}
void tick_probe_net_end(unsigned int id)
{
    if (!data.running || !net_start.valid) return;
    unsigned long long t = now();
    if (!net_part[id].valid || t < net_part[id].t) { data.lost++; return; }
    net_values[id] = t - net_part[id].t;
    net_part[id].valid = 0; net_mask |= 1U << id;
}
void tick_probe_net_finish(void)
{
    if (!data.running || !net_start.valid) return;
    unsigned long long t = now();
    unsigned int bank = net_start.splash || splash || net_start.epoch != epoch;
    if (t < net_start.t || net_mask != (1U << TP_NET_COUNT) - 1) data.lost++;
    else if (t - net_start.t > data.net[bank].total) {
        data.net[bank].total = t - net_start.t;
        for (unsigned int i = 0; i < TP_NET_COUNT; i++) data.net[bank].part[i] = net_values[i];
    }
    net_start.valid = 0;
    for (unsigned int i = 0; i < TP_NET_COUNT; i++) net_part[i].valid = 0;
}
struct tp_device_mark tick_probe_device_start(void)
{
    struct tp_device_mark m = {{0, 0, 0, 0}, 0, TP_CONTEXTS};
    if (data.running) {
        m.context = context();
        if (m.context < TP_CONTEXTS) { m.start = mark(); m.pio = pio_cycles[m.context]; }
        else data.lost++;
    }
    return m;
}
void tick_probe_device_end(unsigned int id, struct tp_device_mark m, unsigned int bytes)
{
    if (!data.running || !m.start.valid) return;
    unsigned long long t = now();
    if (t < m.start.t || pio_cycles[m.context] < m.pio) { data.lost++; return; }
    unsigned int bank = m.start.splash || splash || m.start.epoch != epoch;
    struct tp_device *d = &data.device[bank][id][m.context];
    if (~0ULL - d->bytes < bytes) { data.lost++; return; }
    d->bytes += bytes;
    add_stat(&d->full, t - m.start.t);
    add_stat(&d->pio, pio_cycles[m.context] - m.pio);
}
void tick_probe_pio_begin(void)
{
    if (data.running) pio_mark = mark();
}
void tick_probe_pio_end(void)
{
    if (!data.running || !pio_mark.valid) return;
    unsigned long long t = now(); unsigned int c = context();
    if (t < pio_mark.t || c == TP_CONTEXTS || ~0ULL - pio_cycles[c] < t - pio_mark.t) data.lost++;
    else pio_cycles[c] += t - pio_mark.t;
    pio_mark.valid = 0;
}
void tick_probe_splash(unsigned int active)
{
#ifndef TICK_PROBE_HOST
    unsigned int flags = irq_save();
#endif
    unsigned long long t = data.available ? now() : 0;
    if (data.running) data.elapsed[splash] += t - window;
    window = t;
    if (active) data.splash_enter = t;
    else data.splash_leave = t;
    splash = active;
    epoch++; /* crossing samples stay excluded even after returning */
#ifndef TICK_PROBE_HOST
    irq_restore(flags);
#endif
}
void tick_probe_boot(void)
{
#ifdef TICK_PROBE_HOST
    data.available = 1;
#else
    unsigned int before, after, a, b, c, d;
    __asm__ volatile("pushfl; popl %0" : "=r"(before));
    after = before ^ (1U << 21);
    __asm__ volatile("pushl %0; popfl; pushfl; popl %1; pushl %2; popfl"
                     : "+r"(after), "=&r"(a) : "r"(before) : "cc", "memory");
    if (!((a ^ before) & (1U << 21))) return;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    if (a < 1) return;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    data.available = !!(d & (1U << 4));
#endif
    if (data.available) {
        unsigned long long t = now();
        data.empty_cost = now() - t;
        window = now();
        data.running = 1; /* capture boot splash before shell exists */
    }
}
int tick_probe_control(struct tp_control *c)
{
    /* Caller is cooperative, IF=1. Freeze with a tiny raw CLI section. */
    if (c->action == 1) {
        if (data.running) return -1;
        unsigned int available = data.available;
        unsigned long long cost = data.empty_cost;
        kmemset(&data, 0, sizeof(data));
        kmemset(pending, 0, sizeof(pending));
        interval.valid = if0.valid = net_start.valid = pio_mark.valid = 0;
        kmemset(net_part, 0, sizeof(net_part));
        kmemset(pio_cycles, 0, sizeof(pio_cycles));
        irq5_active = 0;
        data.available = available; data.empty_cost = cost;
        if (available) { window = now(); data.running = 1; }
        c->snapshot = 0;
        return 0;
    } else if (c->action == 2) {
#ifndef TICK_PROBE_HOST
        __asm__ volatile("cli" : : : "memory");
#endif
        if (data.running) {
            data.elapsed[splash] += now() - window;
            for (unsigned int i = 0; i < TP_METRICS; i++)
                if (pending[i].valid) data.lost++;
            if (if0.valid) data.lost++;
            if (net_start.valid || pio_mark.valid) data.lost++;
        }
        data.running = 0;
#ifndef TICK_PROBE_HOST
        __asm__ volatile("sti" : : : "memory");
#endif
    } else if (c->action != 3) return -1;
    if (data.running) return -1; /* never scan mutable statistics with IF=0 */
    for (unsigned int bank = 0; bank < 2; bank++)
        for (unsigned int id = 0; id < TP_METRICS; id++)
            data.bank[bank][id].p99_upper = tick_probe_p99(&data.bank[bank][id]);
    for (unsigned int b = 0; b < 2; b++)
        for (unsigned int i = 0; i < TP_DEVICE_COUNT; i++)
            for (unsigned int j = 0; j < TP_CONTEXTS; j++) {
                struct tp_device *d = &data.device[b][i][j];
                d->full.p99_upper = tick_probe_p99(&d->full);
                d->pio.p99_upper = tick_probe_p99(&d->pio);
            }
    c->snapshot = &data;
    return 0;
}
