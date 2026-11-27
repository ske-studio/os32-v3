/* Private diagnostic contract. Durations are raw TSC cycles, never PIT time. */
#ifndef TICK_PROBE_H
#define TICK_PROBE_H
#define TP_BINS 16
#define TP_OP 21U
#define TP_CONTROL 0x545000U
#include "tick_probe_ids.h"
struct tp_stat {
    unsigned long long total, worst, p99_upper;
    unsigned int count, hist[TP_BINS];
};
enum { TP_NET0, TP_NET1, TP_NET2, TP_NET3, TP_NET4, TP_NET5, TP_NET_COUNT };
enum { TP_RX, TP_TX, TP_DMA, TP_DEVICE_COUNT };
enum { TP_FOREGROUND, TP_IRQ0_CTX, TP_IRQ5_CTX, TP_CONTEXTS };
struct tp_net_worst { unsigned long long total, part[TP_NET_COUNT]; };
struct tp_device { struct tp_stat full, pio; unsigned long long bytes; };
enum { TP_WC, TP_IRQ3, TP_IRQ5, TP_BUDGET, TP_PIT,
       TP_SND_OTHER, TP_SND_START, TP_SND_PAUSE, TP_SND_ADVANCE, TP_SND_RESYNC,
       TP_SND_TEARDOWN, TP_PCM_IRQ10, TP_EXTRA_COUNT };
struct tp_snapshot {
    struct tp_stat bank[2][TP_METRICS]; /* normal, splash/crossing */
    unsigned long long elapsed[2], splash_enter, splash_leave, empty_cost;
    unsigned long long arrival[2]; /* IRQ1 / IRQ10; external trace correlation */
    unsigned int s1_mode, s1_notifications;
    unsigned int available, running, lost, arrivals[2];
    struct tp_net_worst net[2];
    struct tp_device device[2][TP_DEVICE_COUNT][TP_CONTEXTS];
    unsigned long if0_caller[2];
    struct tp_stat extra[2][TP_EXTRA_COUNT];
    unsigned long wc_caller[2];
    unsigned long long scb_polls_total;
    unsigned int scb_polls_max;
};
struct tp_mark { unsigned long long t; unsigned int epoch, splash, valid; };
struct tp_device_mark { struct tp_mark start; unsigned long long pio; unsigned int context; };
void tick_probe_sound_begin(unsigned int metric);
void tick_probe_sound_metric(unsigned int metric);
void tick_probe_sound_end(void);
void tick_probe_extra_begin(unsigned int id);
void tick_probe_extra_end(unsigned int id);
void tick_probe_wc_begin(unsigned long caller);
void tick_probe_wc_end(void);
void tick_probe_net_start(void);
void tick_probe_net_begin(unsigned int id);
void tick_probe_net_end(unsigned int id);
void tick_probe_net_finish(void);
struct tp_device_mark tick_probe_device_start(void);
void tick_probe_device_end(unsigned int id, struct tp_device_mark m, unsigned int bytes);
void tick_probe_pio_begin(void);
void tick_probe_pio_end(void);
enum { TP_S1_IDLE = 4, TP_S1_WAIT, TP_S1_DUE, TP_S1_FIXTURE };
enum { TP_IRQ10_SHOW = 8, TP_TICKACK_ON, TP_TICKACK_OFF, TP_PCMIRQ_10, TP_PCMIRQ_12, TP_PCMIRQ_CONTROL };
struct irq10_event {
    unsigned int count, bits;
    unsigned long long first;
};
struct irq10_line {
    unsigned int valid, gen, count, shared, storm, isolated, master, slave;
    unsigned int irr[2], isr[2];
};
struct irq10_window_event {
    struct irq10_event event;
    unsigned int gen, enable;
};
struct irq10_regs { unsigned int gen; unsigned char valid, i12, i9, i10, i14, i15; };
struct irq10_pcm {
    struct irq10_regs open, start;
    unsigned int generation, pi_gen, route_gen, tick_gen, tick_gen_pi, tick_gen_acks, i11;
    unsigned int route_count, route_step, route_value;
    unsigned int source, i24, r2, r0, irr, isr, imr, master_imr;
    unsigned int tick_pi, tick_acks;
    unsigned long long before_play, after_play;
    unsigned int sampling, arrivals_saved, samples_read;
    struct { unsigned long long tsc; unsigned int i24, r2, batch; } sample[4];
};
struct irq10_diag {
    unsigned long long arrivals, first, window_start;
    unsigned long long irq12_arrivals, irq12_first;
    unsigned int pcmirq, pcm_busy, irq12_owned, irq12_blocked, mask_control;
    int (*v86_open)(void);
    unsigned int tickack, attached, has_irq, command, line, pin, pirq[4];
    int attach_rc;
    unsigned int enable, status, control;
    struct irq10_event usb[2];
    struct { struct irq10_window_event wdh, rhsc; } window[2];
    unsigned int collecting;
    struct irq10_line attach, begin, end, service_wdh, service_rhsc, pcm_open;
    void (*capture)(struct irq10_line *, int);
    struct irq10_pcm pcm;
};
extern struct irq10_diag irq10_diag;
struct tp_request { unsigned int action; }; /* 1=start, 2=stop, 3=show */
struct tp_control { unsigned int action; const struct tp_snapshot *snapshot; const struct irq10_diag *diag; };
void tick_probe_boot(void);
void tick_probe_splash(unsigned int active);
void tick_probe_begin(unsigned int metric);
void tick_probe_end(unsigned int metric);
void tick_probe_irq_enter(unsigned int irq);
void tick_probe_irq_leave(unsigned int irq);
void tick_probe_if_begin(unsigned int flags);
void tick_probe_if_end(unsigned int flags);
int tick_probe_control(struct tp_control *control);
unsigned long long tick_probe_p99(const struct tp_stat *stat);
#endif
