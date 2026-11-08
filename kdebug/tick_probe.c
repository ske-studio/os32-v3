/* Read stopped resident counters through an existing private import. */
#include "tick_probe.h"
#include "debug_probe.h"
#include "io.h"
#include "kprintf.h"
#include "generated/kdebug_import_macros.h"
static const char *const names[TP_METRICS] = {
    "irq0", "timer", "slot0", "slot1", "slot2", "slot3", "slot4",
    "slot5", "slot6", "eoi", "reflect", "timeout", "short", "interval",
    "if0-resident", "irq1-delay", "irq10-delay"
};
static void cycles(const char *name, unsigned long long value)
{
    kprintf(7, "%s=0x%08x%08x ", name,
            (unsigned int)(value >> 32), (unsigned int)value);
}
int tick_probe_call(void *buf, unsigned int size)
{
    if (!buf || size != sizeof(struct tp_request) || !_irq_enabled()) return -1;
    struct tp_control c = {((struct tp_request *)buf)->action, 0};
    int rc = debug_probe_control(TP_CONTROL, &c, sizeof(c));
    if (rc) { kprintf(7, "tickprobe: stop before start/show\n"); return rc; }
    if (!c.snapshot) { kprintf(7, "tickprobe: started (100Hz)\n"); return 0; }
    const struct tp_snapshot *s = c.snapshot;
    kprintf(7, "tickprobe: stopped TSC=%s observation_lost=%u\n",
            s->available ? "raw cycles" : "unmeasured", s->lost);
    if (!s->available) return 0;
    cycles("empty-cost", s->empty_cost);
    cycles("splash-enter", s->splash_enter);
    cycles("splash-leave", s->splash_leave);
    kprintf(7, "\n");
    for (unsigned int bank = 0; bank < 2; bank++) {
        kprintf(7, "bank=%s ", bank ? "splash/crossing" : "normal");
        cycles("elapsed", s->elapsed[bank]); kprintf(7, "\n");
        for (unsigned int id = 0; id < TP_METRICS; id++) {
            const struct tp_stat *v = &s->bank[bank][id];
            if (id >= TP_IRQ1_DELAY) continue;
            kprintf(7, "%s count=%u ", names[id], v->count);
            cycles("worst", v->worst); cycles("p99-upper", v->p99_upper);
            cycles("total", v->total); kprintf(7, "\n hist:");
            for (unsigned int i = 0; i < TP_BINS; i++) kprintf(7, " %u", v->hist[i]);
            kprintf(7, "\n");
        }
        /* Exact occupancy ratio, no TSC frequency guess or 64bit division. */
        cycles("irq0-occupancy-numerator", s->bank[bank][TP_IRQ0].total);
        cycles("occupancy-denominator", s->elapsed[bank]); kprintf(7, "\n");
    }
    kprintf(7, "IRQ1 count=%u IRQ10 count=%u\n", s->arrivals[0], s->arrivals[1]);
    cycles("irq1-last-arrival", s->arrival[0]);
    cycles("irq10-last-arrival", s->arrival[1]);
    kprintf(7, "\nIRQ1/IRQ10 request-to-delivery and lost requests: unmeasured; external trace required\n");
    kprintf(7, "IF0: resident C wrappers only; raw ASM/modules unmeasured; no gate verdict\n");
    return 0;
}
