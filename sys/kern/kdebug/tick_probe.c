/* Read stopped resident counters through an existing private import. */
#include "tick_probe.h"
#include "debug_probe.h"
#include "io.h"
#include "kprintf.h"
#include "../../platform/pc98/kdebug/generated/kdebug_import_macros.h"
static const char *const names[TP_METRICS] = {
    "irq0", "timer", "slot0", "slot1", "slot2", "slot3", "slot4",
    "slot5", "slot6", "slot7", "eoi", "reflect", "timeout", "short", "interval",
    "if0-resident", "irq1-delay", "irq10-delay"
};
static void cycles(const char *name, unsigned long long value)
{
    kprintf(7, "%s=0x%08x%08x ", name,
            (unsigned int)(value >> 32), (unsigned int)value);
}
/* Bitwise division avoids a libgcc dependency in this module. */
static unsigned long long average(unsigned long long total, unsigned int count)
{
    unsigned long long q = 0, remainder = 0;
    for (int bit = 63; bit >= 0; bit--) {
        remainder = (remainder << 1) | ((total >> bit) & 1);
        if (remainder >= count) { remainder -= count; q |= 1ULL << bit; }
    }
    return q;
}
static void codec_regs(const char *stage, const struct irq10_regs *r)
{
    kprintf(7, "%s valid=%u I12=%02x I9=%02x I10=%02x I14=%02x I15=%02x\n",
            stage, r->valid, r->i12, r->i9, r->i10, r->i14, r->i15);
}
static void show_irq10(const struct irq10_diag *d)
{
    cycles("irq10-total", d->arrivals); cycles("first-tsc", d->first);
    cycles("boot-window-tsc", d->window_start); kprintf(7, "\n");
    kprintf(7, "OHCI 0:13.1 captured=%u attach=%d has_irq=%u enable=%08x status=%08x control=%08x\n",
            d->attached, d->attach_rc, d->has_irq, d->enable, d->status, d->control);
    kprintf(7, "COMMAND=%04x LINE=%u PIN=%u PIRQ[60..63]=%02x %02x %02x %02x\n",
            d->command, d->line, d->pin, d->pirq[0], d->pirq[1], d->pirq[2], d->pirq[3]);
    for (unsigned int i = 0; i < 2; ++i) {
        kprintf(7, "OHCI %s count=%u first-bits=%08x ", i ? "service" : "irq",
                d->usb[i].count, d->usb[i].bits);
        cycles("first-tsc", d->usb[i].first); kprintf(7, "\n");
    }
    const struct irq10_pcm *p = &d->pcm;
    codec_regs("open", &p->open); codec_regs("start", &p->start);
    kprintf(7, "0F40 writes=%u step=%u value=%02x tickack=%u tick-pi=%u tick-acks=%u\n",
            p->route_count, p->route_step, p->route_value, d->tickack, p->tick_pi, p->tick_acks);
    kprintf(7, "first-PI source=%u I24=%02x R2=%02x R0=%02x slave IRR=%02x ISR=%02x IMR=%02x master IMR=%02x\n",
            p->source, p->i24, p->r2, p->r0, p->irr, p->isr, p->imr, p->master_imr);
}
int tick_probe_call(void *buf, unsigned int size)
{
    if (!buf || size != sizeof(struct tp_request) || !_irq_enabled()) return -1;
    struct tp_control c = {((struct tp_request *)buf)->action, 0};
    int rc = debug_probe_control(TP_CONTROL, &c, sizeof(c));
    if (rc) { kprintf(7, "tickprobe: stop before start/show\n"); return rc; }
    if (c.diag) {
        if (c.action == TP_IRQ10_SHOW) show_irq10(c.diag);
        else kprintf(7, "tickack=%u\n", c.diag->tickack);
        return 0;
    }
    if (!c.snapshot) { kprintf(7, "tickprobe: started\n"); return 0; }
    const struct tp_snapshot *s = c.snapshot;
    kprintf(7, "tickprobe: stopped TSC=%s observation_lost=%u\n",
            s->available ? "raw cycles" : "unmeasured", s->lost);
    if (s->s1_mode)
        kprintf(7, "s1-mode=%u notifications=%u\n", s->s1_mode, s->s1_notifications);
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
        kprintf(7, "interval-over-2^22=%u (bins start at 2^8)\n", s->bank[bank][TP_INTERVAL].hist[15]);
        for (unsigned int id = 0; id < TP_EXTRA_COUNT; id++) {
            const struct tp_stat *v = &s->extra[bank][id];
            kprintf(7, "%s count=%u ", (const char *const[]){"mtrr-wc", "irq3", "irq5", "slot-budget", "pit-read",
                "snd-other", "snd-start", "snd-pause", "snd-advance", "snd-resync", "snd-teardown", "irq10"}[id], v->count);
            cycles("worst", v->worst); cycles("total", v->total);
            if (id == TP_PIT && v->count) cycles("average", average(v->total, v->count));
            kprintf(7, "\n");
        }
        kprintf(7, "mtrr-last-caller=0x%08x\n", (unsigned int)s->wc_caller[bank]);
        cycles("net-worst", s->net[bank].total);
        for (unsigned int i = 0; i < TP_NET_COUNT; i++)
            cycles((const char *const[]){"demux", "rx-pump", "deadlines", "timers", "tx-round", "notify"}[i], s->net[bank].part[i]);
        kprintf(7, "\nif0-worst-caller=0x%08x\n", (unsigned int)s->if0_caller[bank]);
        for (unsigned int i = 0; i < TP_DEVICE_COUNT; i++)
            for (unsigned int j = 0; j < TP_CONTEXTS; j++) {
                const struct tp_device *d = &s->device[bank][i][j];
                kprintf(7, "ne2k=%s context=%s count=%u ",
                        (const char *const[]){"rx-one", "send", "dma"}[i],
                        (const char *const[]){"foreground", "irq0", "irq5"}[j], d->full.count);
                cycles("bytes", d->bytes);
                cycles("full-worst", d->full.worst); cycles("full-p99", d->full.p99_upper);
                cycles("full-total", d->full.total); cycles("pio-worst", d->pio.worst);
                cycles("pio-p99", d->pio.p99_upper); cycles("pio-total", d->pio.total);
                kprintf(7, "\n");
            }
        /* Exact occupancy ratio, no TSC frequency guess or 64bit division. */
        cycles("irq0-occupancy-numerator", s->bank[bank][TP_IRQ0].total);
        cycles("occupancy-denominator", s->elapsed[bank]); kprintf(7, "\n");
    }
    cycles("scb-polls-total-module-lifetime", s->scb_polls_total);
    kprintf(7, "scb-polls-max=%u\n", s->scb_polls_max);
    kprintf(7, "IRQ1 count=%u IRQ10 count=%u\n", s->arrivals[0], s->arrivals[1]);
    cycles("irq1-last-arrival", s->arrival[0]);
    cycles("irq10-last-arrival", s->arrival[1]);
    kprintf(7, "\nIRQ1/IRQ10 request-to-delivery and lost requests: unmeasured; external trace required\n");
    kprintf(7, "IF0: resident C wrappers only; raw ASM/modules unmeasured; no gate verdict\n");
    return 0;
}
