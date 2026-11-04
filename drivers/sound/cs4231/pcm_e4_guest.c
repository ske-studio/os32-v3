/* pcm_test e4 -> existing kdebug_call, present only in the diagnostic image. */
#include "pcm_e4_diag.h"
#include "pcm_cs4231.h"
#include "idt.h"
#include "io.h"
#include "serial.h"
#include "kprintf.h"

#define PCM_E4_WAIT_TICKS (2U * PIT_HZ)
#define PCM_E4_NUM_CHARS 12
static u8 silence[PCM_STG_BYTES];

static void number(u32 value)
{
    char buf[PCM_E4_NUM_CHARS];
    kutoa_dec(value, buf, sizeof(buf));
    serial_puts(buf);
}

static void report(enum pcm_e4_case scene, int passed)
{
    static const char *const names[] = {"off", "pcm", "fake", "both", "second", "tick"};
    const struct pcm_e4_sample *s = &pcm_e4_sample;
    serial_puts("[pcm-e4] case="); serial_puts(names[scene]);
    serial_puts(" pi="); number(s->pi);
    serial_puts(" dispatch="); number(s->dispatches);
    serial_puts(" trace=");
    for (unsigned int i = 0; i < s->count; i++) {
        if (i) serial_puts(",");
        serial_puts(s->trace[i].actor == PCM_E4_PCM_HANDLER ? "P" :
                    s->trace[i].actor == PCM_E4_FAKE_HANDLER ? "F" : "T");
        number((u32)s->trace[i].result);
    }
    serial_puts(passed ? " PASS\n" : " FAIL\n");
}

static int run_case(enum pcm_e4_case scene)
{
    /* Register fake first so SECOND really needs a second scan. */
    int rc = irq_register(PCM_IRQ, pcm_e4_fake_irq, 0, IRQ_F_SHARED);
    if (rc) return rc;
    rc = pcm_open(PCM_RATE_44100);
    if (rc) {
        irq_unregister(PCM_IRQ, pcm_e4_fake_irq, 0);
        return rc;
    }
    unsigned int flags = irq_save();
    pcm_e4_arm(scene);
    if (scene == PCM_E4_FAKE) {
        /* Software-only stimulus, no second physical interrupt source. */
        irq_dispatch(PCM_IRQ);
    } else if (scene != PCM_E4_TICK) {
        rc = pcm_write(silence, sizeof(silence));
        if (rc != (int)sizeof(silence)) rc = OS32_ERR_IO;
        else rc = 0;
    }
    u32 deadline = tick_count + PCM_E4_WAIT_TICKS;
    irq_restore(flags);
    for (;;) {
        flags = irq_save();
        int stop = pcm_e4_sample.done || (int)(tick_count - deadline) >= 0 || rc;
        irq_restore(flags);
        if (stop) break;
        _halt();
    }
    flags = irq_save();
    int passed = !rc && pcm_e4_passed();
    pcm_e4_sample.scene = PCM_E4_OFF; /* freeze evidence before close/tick */
    irq_restore(flags);
    report(scene, passed);
    int closed = pcm_close();
    int removed = irq_unregister(PCM_IRQ, pcm_e4_fake_irq, 0);
    return passed && !closed && !removed ? 0 : OS32_ERR_IO;
}

int pcm_e4_run(void)
{
    if (!_irq_enabled()) return OS32_ERR_IO;
    if (pcm_state() != PCM_ST_CLOSED || irq_lines[irq_dyn_index(PCM_IRQ)].count)
        return OS32_ERR_BUSY;
    serial_puts("[pcm-e4] software sharing only; electrical sharing requires 82557 hardware\n");
    for (enum pcm_e4_case scene = PCM_E4_PCM; scene <= PCM_E4_TICK; scene++) {
        int rc = run_case(scene);
        if (rc) {
            serial_puts("[pcm-e4] FAIL (case/setup/close)\n");
            return rc;
        }
    }
    serial_puts("[pcm-e4] 5/5 PASS\n");
    return 0;
}
