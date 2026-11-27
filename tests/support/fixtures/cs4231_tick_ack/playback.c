#include <stdio.h>
#include <stdlib.h>
#include "sys/dev/sound/cs4231/pcm_cs4231.c"

static unsigned int index_reg, status, reads, clears, advances;

static unsigned int r0_reads, selects, r2_reads, observed_pi;

unsigned int inp(unsigned int port)
{
    if (port == PCM_PORT_R0) { ++r0_reads; return index_reg; }
    if (port == PCM_PORT_R2) { ++r2_reads; return status ? 1 : 0; }
    if (port != PCM_PORT_R1 || index_reg != PCM_I_ALTSTAT) abort();
    observed_pi = status & PCM_ALT_PI;
    ++reads;
    return status;
}

void outp(unsigned int port, unsigned int value)
{
    if (port == PCM_PORT_R0) {
        ++selects;
        index_reg = value;
        return;
    }
    if (port != PCM_PORT_R1 || index_reg != PCM_I_ALTSTAT || value || !status ||
        (!observed_pi && status != 0xff)) abort();
    observed_pi = 0;
    ++clears;
    status = 0;
}

static int playback_edge(void)
{
    int rising = !(status & PCM_ALT_PI);
    status |= PCM_ALT_PI;
    return rising;
}

static void advance(void) { ++advances; }

int main(void)
{
    dma_owned = mode2_verified = 1;
    s_hal_advance = advance;
    g_pcm.state = PCM_ST_RUNNING;
    if (!playback_edge() || playback_edge()) return 1;
    pcm_tick();
    if (reads != 1 || clears != 1 || status || advances != 1) {
        fprintf(stderr, "missed PI after tick: reads=%u clears=%u status=%#x advances=%u\n",
                reads, clears, status, advances);
        return 1;
    }
    if (PCM_IRQ != 12 || !playback_edge() || pcm_irq(12, 0) != IRQ_HANDLED) {
        fprintf(stderr, "IRQ12 delivery failed\n");
        return 1;
    }
    if (pcm_irq(12, 0) != IRQ_NONE || advances != 2) {
        fprintf(stderr, "duplicate playback advance\n");
        return 1;
    }
    if (status || reads != 2 || clears != 2) return 1;
    status = 0xff;
    if (pcm_irq(12, 0) != IRQ_NONE || clears != 2 || advances != 2) {
        fprintf(stderr, "invalid I24 claimed by IRQ12\n");
        return 1;
    }
    pcm_tick();
    if (reads != 4 || clears != 2 || advances != 3) {
        fprintf(stderr, "invalid I24 tick recovery\n");
        return 1;
    }
    const int states[] = {PCM_ST_OPEN, PCM_ST_STOP_DONE, PCM_ST_RS_STOP,
                          PCM_ST_RS_RESTART, PCM_ST_STOP_REQ, PCM_ST_DRAINING,
                          PCM_ST_RUNNING, PCM_ST_OPENING, PCM_ST_CLOSED, PCM_ST_FAULTED};
    int failures = 0;
    for (unsigned i = 0; i < sizeof(states) / sizeof(*states); ++i) {
        g_pcm.state = states[i];
        int active = states[i] != PCM_ST_OPENING && states[i] != PCM_ST_CLOSED &&
                     states[i] != PCM_ST_FAULTED;
        for (unsigned irq = 10; irq <= 12; irq += 2) {
            for (int from_irq = 0; from_irq <= 1; ++from_irq) {
                for (int pi = 0; pi <= 1; ++pi) {
                    reads = clears = selects = r0_reads = r2_reads = 0;
                    status = pi ? PCM_ALT_PI : 0;
                    int handled = 0;
                    if (from_irq) handled = pcm_irq(irq, 0) == IRQ_HANDLED;
                    else pcm_tick();
                    int probe = active && (!from_irq || pi);
                    if (reads != probe || r0_reads != probe || clears != (active && pi) ||
                        selects != reads + clears || r2_reads != (active && from_irq) ||
                        handled != (active && from_irq && pi)) {
                        fprintf(stderr, "PI access state=%d irq=%u from_irq=%d pi=%d\n",
                                states[i], irq, from_irq, pi);
                        ++failures;
                    }
                }
            }
        }
    }
    g_pcm.state = PCM_ST_RUNNING;
    for (int guard = 0; guard < 4; ++guard) {
        dma_owned = guard != 0;
        mode2_verified = guard != 1;
        s_claimed = guard == 2;
        s_mce_busy = guard == 3;
        status = PCM_ALT_PI;
        reads = clears = selects = r0_reads = r2_reads = advances = 0;
        pcm_tick();
        if (pcm_irq(12, 0) != IRQ_NONE || reads || clears || selects ||
            r0_reads || r2_reads || advances) return 1;
    }
    return failures != 0;
}

/* Legacy advance shares these entry points. A HAL tick must bypass it. */
volatile u32 tick_count;
int sys_time_now(u32 *lo, u32 *hi) { abort(); }
void tick_clock_jiffy64(u32 *lo, u32 *hi) { abort(); }
void *kmemcpy(void *dst, const void *src, u32 size) { abort(); }
void *kmemset(void *dst, int value, u32 size) { abort(); }
void dma_chan_ack_tc(unsigned int ch, const void *owner) { abort(); }
void dma_chan_mask(unsigned int ch, const void *owner) { abort(); }
void dma_chan_unmask(unsigned int ch, const void *owner) { abort(); }
int dma_chan_setup(unsigned int ch, const void *owner, u32 phys, u32 bytes, int dir, int mode) { abort(); }
int dma_chan_remaining(unsigned int ch, const void *owner, u32 *left, int *tc) { abort(); }
