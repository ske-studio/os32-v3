/* Test device: proves software dispatch order, never electrical IRQ sharing. */
#include "pcm_e4_diag.h"
#include "pcm_cs4231.h"
#include "io.h"
#include "kstring.h"

struct pcm_e4_sample pcm_e4_sample;
static irq_handler_fn pcm_handler;

void pcm_e4_attach(irq_handler_fn fn) { pcm_handler = fn; }

void pcm_e4_arm(enum pcm_e4_case scene)
{
    kmemset(&pcm_e4_sample, 0, sizeof(pcm_e4_sample));
    pcm_e4_sample.scene = scene;
    if (scene == PCM_E4_FAKE || scene == PCM_E4_TICK)
        pcm_e4_sample.pending = 1;
}

static void record(int actor, int result)
{
    struct pcm_e4_sample *s = &pcm_e4_sample;
    if (s->capturing && s->count < PCM_E4_TRACE_MAX) {
        s->trace[s->count].actor = actor;
        s->trace[s->count++].result = result;
    }
}

void pcm_e4_dispatch_enter(unsigned int irq)
{
    struct pcm_e4_sample *s = &pcm_e4_sample;
    if (irq != PCM_IRQ || s->scene == PCM_E4_OFF || s->done) return;
    if (s->scene == PCM_E4_TICK) {
        s->dispatches++; /* any IRQ10 is interference in the no-IRQ case */
        return;
    }
    /* This is before any handler, hence before the real PCM I24 ack.
     * Only arm during stable playback (no MCE/INIT). Restore the index. */
    unsigned int index = inp(PCM_PORT_R0);
    outp(PCM_PORT_R0, PCM_I_ALTSTAT);
    s->pi = (inp(PCM_PORT_R1) & PCM_ALT_PI) != 0;
    outp(PCM_PORT_R0, index);
    if (s->scene != PCM_E4_FAKE && !s->pi) return;
    s->dispatches++; /* count the dispatch selected by the PI trigger */
    s->capturing = 1;
    if (s->scene == PCM_E4_BOTH) s->pending = 1;
}

int pcm_e4_fake_irq(unsigned int irq, void *arg)
{
    (void)irq; (void)arg;
    struct pcm_e4_sample *s = &pcm_e4_sample;
    int rc = IRQ_NONE;
    if (s->capturing && s->pending) {
        s->pending = 0;
        rc = IRQ_HANDLED;
    }
    record(PCM_E4_FAKE_HANDLER, rc);
    return rc;
}

int pcm_e4_irq(unsigned int irq, void *arg)
{
    int rc = pcm_handler(irq, arg);
    struct pcm_e4_sample *s = &pcm_e4_sample;
    record(PCM_E4_PCM_HANDLER, rc);
    /* Fake slot is first: it has already returned NONE this round. */
    if (s->capturing && s->scene == PCM_E4_SECOND && rc == IRQ_HANDLED)
        s->pending = 1;
    return rc;
}

void pcm_e4_dispatch_leave(unsigned int irq)
{
    struct pcm_e4_sample *s = &pcm_e4_sample;
    if (irq == PCM_IRQ && s->capturing) {
        s->capturing = 0;
        s->done = 1;
    }
}

void pcm_e4_tick(void)
{
    struct pcm_e4_sample *s = &pcm_e4_sample;
    if (s->scene != PCM_E4_TICK || !s->pending || s->done) return;
    s->capturing = 1;
    s->pending = 0;
    record(PCM_E4_FAKE_TICK, IRQ_HANDLED);
    s->capturing = 0;
    s->done = 1;
}

int pcm_e4_passed(void)
{
    const struct pcm_e4_sample *s = &pcm_e4_sample;
    if (!s->done || s->pending) return 0;
    if (s->scene == PCM_E4_TICK)
        return s->dispatches == 0 && s->count == 1 &&
               s->trace[0].actor == PCM_E4_FAKE_TICK &&
               s->trace[0].result == IRQ_HANDLED;
    if (s->dispatches != 1 || s->count != PCM_E4_TRACE_MAX ||
        s->pi != (s->scene != PCM_E4_FAKE)) return 0;
    for (unsigned int i = 0; i < PCM_E4_TRACE_MAX; i++) {
        int want = IRQ_NONE;
        if (s->trace[i].actor != (int)(i % 2 ? PCM_E4_PCM_HANDLER : PCM_E4_FAKE_HANDLER))
            return 0;
        if ((i == 0 && (s->scene == PCM_E4_FAKE || s->scene == PCM_E4_BOTH)) ||
            (i == 1 && s->scene != PCM_E4_FAKE) ||
            (i == 2 && s->scene == PCM_E4_SECOND)) want = IRQ_HANDLED;
        if (s->trace[i].result != want) return 0;
    }
    return 1;
}
