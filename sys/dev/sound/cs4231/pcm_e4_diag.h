#ifndef PCM_E4_DIAG_H
#define PCM_E4_DIAG_H

#include "irq.h"

enum pcm_e4_case {
    PCM_E4_OFF, PCM_E4_PCM, PCM_E4_FAKE, PCM_E4_BOTH,
    PCM_E4_SECOND, PCM_E4_TICK
};
enum pcm_e4_actor { PCM_E4_FAKE_HANDLER, PCM_E4_PCM_HANDLER, PCM_E4_FAKE_TICK };
#define PCM_E4_TRACE_MAX 4
struct pcm_e4_event { int actor, result; };
struct pcm_e4_sample {
    enum pcm_e4_case scene;
    unsigned int dispatches, pi, count;
    int pending, capturing, done;
    struct pcm_e4_event trace[PCM_E4_TRACE_MAX];
};

/* Callers hold IF=0; guest runner reads the sample under irq_save too. */
extern struct pcm_e4_sample pcm_e4_sample;
void pcm_e4_arm(enum pcm_e4_case scene);
void pcm_e4_attach(irq_handler_fn fn);
int pcm_e4_irq(unsigned int irq, void *arg);
int pcm_e4_fake_irq(unsigned int irq, void *arg);
void pcm_e4_dispatch_enter(unsigned int irq);
void pcm_e4_dispatch_leave(unsigned int irq);
void pcm_e4_tick(void);
int pcm_e4_passed(void);
int pcm_e4_run(void);

#endif
