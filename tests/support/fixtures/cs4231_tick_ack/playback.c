#include <stdio.h>
#include <stdlib.h>
#include "sys/dev/sound/cs4231/pcm_cs4231.c"

static unsigned int index_reg, status, reads, clears, advances;

unsigned int inp(unsigned int port)
{
    if (port != PCM_PORT_R1 || index_reg != PCM_I_ALTSTAT) abort();
    ++reads;
    return status;
}

void outp(unsigned int port, unsigned int value)
{
    if (port == PCM_PORT_R0) {
        index_reg = value;
        return;
    }
    if (port != PCM_PORT_R1 || index_reg != PCM_I_ALTSTAT || value || !status ||
        reads != clears + 1) abort();
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
    s_hal_advance = advance;
    g_pcm.state = PCM_ST_RUNNING;
    if (!playback_edge() || playback_edge()) return 1;
    pcm_tick();
    if (reads != 1 || clears != 1 || status || advances != 1) {
        fprintf(stderr, "missed PI after tick: reads=%u clears=%u status=%#x advances=%u\n",
                reads, clears, status, advances);
        return 1;
    }
    if (!playback_edge() || pcm_irq(PCM_IRQ, 0) != IRQ_HANDLED) return 1;
    return status || advances != 2 || reads != 2 || clears != 2;
}

/* Legacy advance shares these entry points. A HAL tick must bypass it. */
volatile u32 tick_count;
int sys_time_now(u32 *lo, u32 *hi) { abort(); }
void tick_clock_jiffy64(u32 *lo, u32 *hi) { abort(); }
void *kmemcpy(void *dst, const void *src, u32 size) { abort(); }
void *kmemset(void *dst, int value, u32 size) { abort(); }
void dma_chan_ack_tc(unsigned int ch) { abort(); }
void dma_chan_mask(unsigned int ch) { abort(); }
void dma_chan_unmask(unsigned int ch) { abort(); }
int dma_chan_setup(unsigned int ch, u32 phys, u32 bytes, int dir, int mode) { abort(); }
int dma_chan_remaining(unsigned int ch, u32 *left, int *tc) { abort(); }
