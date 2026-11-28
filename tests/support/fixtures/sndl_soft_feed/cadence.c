#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userland/lib/snd/pump.c"
#include "userland/lib/snd/chip.c"
#include "sys/hal/sound/snd2_seg.c"
#include "sys/dev/sound/cs4231/pcm_cs4231.h"

/* Time is measured in 44.1 kHz output frames. Drive the real segment guard,
 * retirement and refill with a period IRQ and a 1 ms position observer. */
static short ring[4096][2], staging[8192][2];
static Snd2Stream sink;
static unsigned dma_frame, last_publish, max_gap, silent, sequence, channel;
static unsigned clock_frame, next_tick_sample;
static unsigned width, next_transfer, variable, max_wait, max_prime;
static unsigned batch, turns, irq_phase, pi_phase, divisor;
static unsigned short dma_count[4];
/* External accounting in sample-clock frames, excluding pre-start priming. */
static unsigned pos_cost, publish_cost, sync_cost, render_cost, slices;
static void costs(void)
{
    printf("DMA#%u cost 1/%u: slices=%u resume=%u drain=%u position=%u "
           "sync=%u render=%u publish=%u (44.1 kHz clock frames)\n",
           channel, divisor, slices, slices * 88, slices * 60,
           pos_cost, sync_cost, render_cost, publish_cost);
}

static unsigned hw_position(void)
{
    return pcm_pos_frames(dma_count[channel] + 1);
}
KernelAPI *sound_api;
void *kmemset(void *p, int c, u32 n) { return memset(p, c, n); }
u32 snd2_frame_bytes(const Snd2Stream *s) { return 4; }
void snd2_stg_read(const Snd2Stream *s, u32 first, void *dst, u32 n)
{
    for (unsigned i = 0; i < n; ++i)
        memcpy((short *)dst + 2*i, staging[(first + i) % 8192], 4);
}
static short marker(unsigned frame) { return 1 + frame % 30000; }
static unsigned now_ms(void) { return clock_frame * 10 / 441; }

static void advance(unsigned frames)
{
    if (sink.state != SND2_ST_RUN) return;
    clock_frame += frames;
    /* DMA can advance several frames between visible count samples. */
    while (next_transfer <= clock_frame) {
        unsigned previous = dma_frame;
        while (dma_frame < next_transfer) {
            unsigned hw = dma_frame % 4096;
            if (!ring[hw][0]) ++silent;
            else if (ring[hw][0] != marker(dma_frame - silent) || ring[hw][1]) exit(3);
            ++dma_frame;
            dma_count[channel] -= 4;
            if (dma_count[channel] == 0xffff) dma_count[channel] = sizeof ring - 1;
        }
        unsigned hw = hw_position();
        unsigned room = sink.cap - (sink.written - sink.played);
        unsigned transferred = dma_frame - previous;
        next_transfer += variable ? 1 + (room + transferred * 17 + hw) % width : width;
        int period_irq = (dma_frame + pi_phase) / 256 != (previous + pi_phase) / 256;
        int tick = (dma_frame + irq_phase) / 441 !=
                   (previous + irq_phase) / 441;
        if (period_irq || tick || sink.written != sink.copied || dma_frame >= next_tick_sample) {
            next_tick_sample = dma_frame + 255 - hw % 256;
            if (snd2_seg_observe(&sink, 1, hw, dma_frame * 10 / 441)) exit(4);
        }
    }
}
static int position(u32 id, SND2_Pos *p)
{
    if (sink.state == SND2_ST_RUN) pos_cost += 2;
    advance(2);
    *p = (SND2_Pos){.cap = sink.cap, .written = sink.written,
        .played = snd2_seg_played_now(&sink), .state = sink.state, .underruns = sink.underruns, .time_ms = now_ms()};
    return 0;
}
static int publish(u32 id, const void *pcm, u32 bytes)
{
    if (sink.state == SND2_ST_RUN) publish_cost += 12;
    advance(12);
    unsigned n = bytes/4;
    unsigned room = sink.cap - (sink.written - sink.played);
    if (n > room) n = room;
    for (unsigned i = 0; i < n; ++i)
        memcpy(staging[(sink.written+i) % 8192], (const short *)pcm+2*i, 4);
    sink.written += n;
    if (sink.state == SND2_ST_RUN && n) {
        unsigned gap = clock_frame - last_publish;
        if (gap > max_gap) max_gap = gap;
        last_publish = clock_frame;
        snd2_seg_after_publish(&sink, hw_position(), now_ms());
    }
    return n*4;
}
static int devices(void) { return 1; }
static int device_info(u32 id, SND2_DevInfo *info)
{
    *info = (SND2_DevInfo){.kind = SND2_KIND_PCM_OUT};
    return 0;
}
static int open_stream(const SND2_StreamDesc *desc, u32 *id)
{
    *id = 1;
    sink = (Snd2Stream){.fmt=desc->fmt, .period=desc->period_frames,
        .nseg=16, .lead=desc->lead_periods, .margin=128,
        .cap=(desc->lead_periods + 1) * desc->period_frames,
        .ring=(u8 *)ring, .state=SND2_ST_IDLE};
    return 0;
}
static int start(u32 id)
{
    snd2_seg_prime(&sink);
    sink.state = SND2_ST_RUN;
    return 0;
}
static int clock_us(u32 *lo, u32 *hi)
{
    *lo = (uint64_t)clock_frame * 1000000 / 44100;
    *hi = 0;
    return 0;
}
int fmv_render_part(FmVoice *v, uint32_t now, unsigned char *out,
                    uint32_t frames, uint32_t offset, uint32_t n)
{
    if (!out) {
        if (sink.state == SND2_ST_RUN) sync_cost += 14;
        advance(14); /* fixed cost of synchronizing a foreground FM instance */
        return 0;
    }
    unsigned cost = 8 + (n + divisor - 1)/divisor;
    if (sink.state == SND2_ST_RUN) render_cost += cost;
    advance(cost);
    if (sink.state == SND2_ST_RUN && n > 256) ++batch;
    for (unsigned i = 0; i < n; ++i) {
        ((short *)out)[2*i] = marker(sequence++);
        ((short *)out)[2*i+1] = 0;
    }
    return 0;
}
static int scenario(int dos, unsigned phase, unsigned pi_offset)
{
    dma_frame = last_publish = max_gap = silent = sequence = 0;
    clock_frame = next_tick_sample = max_wait = max_prime = 0;
    batch = turns = slices = 0;
    pos_cost = publish_cost = sync_cost = render_cost = 0;
    refs = 0;
    memset(ring, 0, sizeof ring);
    pi_phase = pi_offset;
    next_transfer = 1 + (width - 1 + phase) % width;
    /* Both 8237 channels count bytes. Change service phase between routes. */
    dma_count[channel] = sizeof ring - 1;
    irq_phase = channel == 3 ? 0 : 29;
    static KernelAPI api = {.snd2_dev_count=devices, .snd2_dev_info=device_info,
        .snd2_stream_open=open_stream, .snd2_stream_pos = position, .snd2_stream_write = publish,
        .snd2_stream_start = start, .sys_time_now = clock_us};
    sound_api = &api;
    SND2_Fmt fmt = {.rate=44100, .bits=16, .channels=2, .enc=SND2_ENC_S16LE};
    slots[0].fm = (FmVoice *)&api;
    if (sndl_soft_pcm_acquire(&fmt) || sndl_pump()) return 5;
    unsigned refill_start = clock_frame;
    while (dma_frame < 132300) {
        if (dos) {
            unsigned before = clock_frame;
            int rc = sndl_prime_slice();
            unsigned first_again = clock_frame;
            while (rc == OS32_ERR_AGAIN && clock_frame - first_again < 44100 / 20) {
                advance(44); /* yield until the next foreground opportunity */
                rc = sndl_prime_slice();
            }
            if (rc) {
                costs();
                fprintf(stderr, "DOS refill deadline rc=%d\n", rc);
                return 6;
            }
            if (clock_frame - before > max_prime) max_prime = clock_frame - before;
            unsigned waited = clock_frame - refill_start;
            if (waited > max_wait) max_wait = waited;
            ++slices;
            advance(88); /* resume low memory and re-enter DOS */
            advance(441 - clock_frame % 441); /* next logical jiffy */
            advance(60); /* restore and drain */
            refill_start = clock_frame;
        } else if (!(turns++ % 8)) {
            advance(380); /* occasional foreground work between ordinary pumps */
        }
        if (sndl_pump()) return 7;
    }
    if (sink.underruns || silent || max_gap * 100000ULL >= 1741ULL * 44100) {
        fprintf(stderr, "DMA supply deadline: underruns=%lu silent=%u gap=%u\n",
                sink.underruns, silent, max_gap);
        return 1;
    }
    /* Across three seconds, require a slice per three jiffies on average.
     * Bound both prime alone and the complete drain-to-reentry refill. */
    if (dos && (slices < 100 || max_wait > 4 * 441 || max_prime > 2 * 441)) {
        fprintf(stderr, "DOS refill progress: slices=%u foreground=%u prime=%u\n",
                slices, max_wait, max_prime);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    channel = atoi(argv[1]);
    int dos = atoi(argv[2]);
    divisor = atoi(argv[3]);
    unsigned longest_gap = 0, longest_wait = 0, longest_prime = 0, fewest_slices = ~0U, batches = 0;
    for (width = 1; width <= 256; ++width) {
        for (variable = 0; variable <= 1; ++variable) {
            for (unsigned alignment = 0; alignment < 4; ++alignment) {
                unsigned phase = (alignment & 1) ? 97 : 0;
                unsigned pi_offset = (alignment & 2) ? 97 : 0;
                if (scenario(dos, phase, pi_offset)) {
                    costs();
                    fprintf(stderr, "width=%u variable=%u phase=%u PI=%u\n",
                            width, variable, phase, pi_offset);
                    return 1;
                }
                if (width == 44 && !variable && !alignment) costs();
                if (max_gap > longest_gap) longest_gap = max_gap;
                if (max_prime > longest_prime) longest_prime = max_prime;
                if (max_wait > longest_wait) longest_wait = max_wait;
                if (slices < fewest_slices) fewest_slices = slices;
                batches += batch;
            }
        }
    }
    if (!batches) {
        fprintf(stderr, "missing foreground batch\n");
        return 1;
    }
    printf("DMA#%u %s cost 1/%u: 2048 cases, underruns=0 silent=0 gap=%.3f ms "
           "foreground=%.3f ms prime=%.3f ms slices_min=%u batches=%u\n",
           channel, dos ? "fmtone" : "soft", divisor, longest_gap/44.1,
           longest_wait/44.1, longest_prime/44.1, fewest_slices, batches);
    return 0;
}
