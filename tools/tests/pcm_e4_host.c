/* Reuse the PCM device/clock/DMA model and all real PCM implementation.
 * Its IRQ stubs are renamed; E4 below uses real irq_register/dispatch. */
#include "irq.h"
#define irq_register pcm_model_irq_register
#define irq_unregister pcm_model_irq_unregister
#define main pcm_model_main
#include "pcm_cs4231_host.c"
#undef main
#undef irq_register
#undef irq_unregister

#include "../../kernel/irq/irq_math.c"
#include "../../platform/pc98/irq_policy.c"
#include "../../platform/pc98/irq_complete.c"
#include "../../kernel/irq/irq.c"
#include "../../kernel/irq/tick.c"
#include "../../drivers/sound/cs4231/pcm_e4_diag.c"

volatile u32 kctx_irq_depth;
static unsigned int eois;
void irq_enable(unsigned int irq) { CHECK(irq == PCM_IRQ); }
void irq_disable(unsigned int irq) { CHECK(irq == PCM_IRQ); }
void pic_eoi(unsigned int irq) { CHECK(irq == PCM_IRQ); eois++; }
void platform_irq_eoi_master(void) { CHECK(0); }
int platform_irq_slave_last_in_service(void) { CHECK(0); return 0; }
void isr_unexpected_report(u32 irq) { (void)irq; CHECK(0); }

static void test_case(enum pcm_e4_case scene)
{
    reset_all();
    kmemset(irq_lines, 0, sizeof(irq_lines));
    eois = 0;
    pcm_e4_arm(PCM_E4_OFF);
    CHECK(irq_register(PCM_IRQ, pcm_e4_fake_irq, 0, IRQ_F_SHARED) == 0);
    CHECK(pcm_open(PCM_RATE_44100) == 0);
    pcm_e4_attach(pcm_irq); /* actual handler, not a scripted return value */
    CHECK(irq_register(PCM_IRQ, pcm_e4_irq, 0, IRQ_F_SHARED) == 0);
    pcm_e4_arm(scene);
    if (scene == PCM_E4_PCM || scene == PCM_E4_BOTH || scene == PCM_E4_SECOND) {
        /* A tick can ack PI before a delayed IRQ. Wait for a real PI entry;
         * a PI=0 entry must neither inject fake work nor complete the sample. */
        pcm_e4_dispatch_enter(PCM_IRQ);
        pcm_e4_dispatch_leave(PCM_IRQ);
        CHECK(!pcm_e4_sample.pending && !pcm_e4_sample.done);
        CHECK(pcm_e4_sample.dispatches == 0 && pcm_e4_sample.count == 0);
    }
    dev_reg[PCM_I_ALTSTAT] = scene != PCM_E4_FAKE && scene != PCM_E4_TICK ? PCM_ALT_PI : 0;
    u32 writes = io_writes;
    if (scene == PCM_E4_TICK) {
        CHECK(pcm_e4_sample.pending && !pcm_e4_sample.done && !eois);
        CHECK(tick_register(0, pcm_tick) == 0);
        CHECK(tick_register(1, pcm_e4_tick) == 0);
        tick_dispatch();
        CHECK(eois == 0);
    } else {
        unsigned int flags = irq_save();
        kctx_irq_depth++;
        irq_dispatch(PCM_IRQ);
        kctx_irq_depth--;
        irq_restore(flags);
        CHECK(eois == 1);
        CHECK(io_writes > writes);
        CHECK(dev_reg[PCM_I_ALTSTAT] == 0); /* real PCM ack */
    }
    CHECK(pcm_e4_passed());
    /* Independent assertions fix the order, not merely the shared oracle. */
    if (scene == PCM_E4_BOTH) {
        CHECK(pcm_e4_sample.count == 4);
        CHECK(pcm_e4_sample.trace[0].actor == PCM_E4_FAKE_HANDLER);
        CHECK(pcm_e4_sample.trace[0].result == IRQ_HANDLED);
        CHECK(pcm_e4_sample.trace[1].actor == PCM_E4_PCM_HANDLER);
        CHECK(pcm_e4_sample.trace[1].result == IRQ_HANDLED);
        CHECK(pcm_e4_sample.pi == 1);
    }
    if (scene == PCM_E4_SECOND) {
        CHECK(pcm_e4_sample.count == 4);
        CHECK(pcm_e4_sample.trace[0].result == IRQ_NONE);
        CHECK(pcm_e4_sample.trace[1].result == IRQ_HANDLED);
        CHECK(pcm_e4_sample.trace[2].actor == PCM_E4_FAKE_HANDLER);
        CHECK(pcm_e4_sample.trace[2].result == IRQ_HANDLED);
        CHECK(pcm_e4_sample.trace[3].result == IRQ_NONE);
    }
    CHECK(pcm_shim_irq_depth == 0);
    printf("E4 case=%d %s\n", scene, failed ? "FAIL" : "PASS");
}

int main(void)
{
    for (enum pcm_e4_case scene = PCM_E4_PCM; scene <= PCM_E4_TICK; scene++)
        test_case(scene);
    return failed != 0;
}

/* Clock-independent registry fixture. */
void tick_budget_begin(void) { }
void tick_budget_end(void) { }
