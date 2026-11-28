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
static unsigned batch, turns, irq_phase;
static unsigned short dma_count[4];

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
static unsigned now_ms(void) { return dma_frame * 10 / 441; }

static void advance(unsigned frames)
{
    while (frames-- && sink.state == SND2_ST_RUN) {
        unsigned hw = dma_frame % 4096;
        if (!ring[hw][0]) ++silent;
        else if (ring[hw][0] != marker(dma_frame - silent) || ring[hw][1]) exit(3);
        ++dma_frame;
        dma_count[channel] -= 4;
        if (dma_count[channel] == 0xffff) dma_count[channel] = sizeof ring - 1;
        hw = hw_position();
        if (!((dma_frame + irq_phase) % 44) || !(hw % 256))
            if (snd2_seg_observe(&sink, 1, hw, now_ms())) exit(4);
    }
}
static int position(u32 id, SND2_Pos *p)
{
    advance(2);
    *p = (SND2_Pos){.cap = sink.cap, .written = sink.written,
        .played = snd2_seg_played_now(&sink), .state = sink.state, .time_ms = now_ms()};
    return 0;
}
static int publish(u32 id, const void *pcm, u32 bytes)
{
    advance(12);
    unsigned n = bytes/4;
    unsigned room = sink.cap - (sink.written - sink.played);
    if (n > room) n = room;
    for (unsigned i = 0; i < n; ++i)
        memcpy(staging[(sink.written+i) % 8192], (const short *)pcm+2*i, 4);
    sink.written += n;
    if (sink.state == SND2_ST_RUN && n) {
        unsigned gap = dma_frame - last_publish;
        if (gap > max_gap) max_gap = gap;
        last_publish = dma_frame;
        snd2_seg_after_publish(&sink, hw_position(), now_ms());
    }
    return n*4;
}
static int start(u32 id)
{
    snd2_seg_prime(&sink);
    sink.state = SND2_ST_RUN;
    return 0;
}
static int clock_us(u32 *lo, u32 *hi)
{
    *lo = (uint64_t)dma_frame * 1000000 / 44100;
    *hi = 0;
    return 0;
}
int fmv_render_part(FmVoice *v, uint32_t now, unsigned char *out,
                    uint32_t frames, uint32_t offset, uint32_t n)
{
    if (!out) {
        advance(14); /* fixed cost of synchronizing a foreground FM instance */
        return 0;
    }
    advance(8 + (n + 2)/3);
    if (sink.state == SND2_ST_RUN && n > 256) ++batch;
    for (unsigned i = 0; i < n; ++i) {
        ((short *)out)[2*i] = marker(sequence++);
        ((short *)out)[2*i+1] = 0;
    }
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    channel = atoi(argv[1]);
    int dos = atoi(argv[2]);
    /* Both 8237 channels count bytes. Change service phase between routes. */
    dma_count[channel] = sizeof ring - 1;
    irq_phase = channel == 3 ? 0 : 29;
    static KernelAPI api = {.snd2_stream_pos = position, .snd2_stream_write = publish,
        .snd2_stream_start = start, .sys_time_now = clock_us};
    sound_api = &api;
    refs = stream = 1;
    pcm_period = 256;
    rate = 44100;
    slots[0].fm = (FmVoice *)&api;
    sink = (Snd2Stream){.fmt = {.rate=44100}, .period=256, .nseg=16, .lead=3,
        .margin=128, .cap=1024, .ring=(u8 *)ring, .state=SND2_ST_IDLE};
    if (sndl_pump()) return 5;
    while (dma_frame < 132300) {
        if (dos) {
            if (sndl_prime_slice()) return 6;
            advance(88); /* resume low memory and re-enter DOS */
            advance(441 - dma_frame % 441); /* next logical jiffy */
            advance(60); /* restore and drain */
        } else if (!(turns++ % 8)) {
            advance(380); /* occasional foreground work between ordinary pumps */
        }
        if (sndl_pump()) return 7;
    }
    printf("DMA#%u %s: underruns=%lu silent=%u max_gap=%.3f ms batches=%u\n",
           channel, dos ? "fmtone" : "soft", sink.underruns, silent, max_gap/44.1, batch);
    if (sink.underruns || silent || max_gap * 100000ULL >= 1741ULL * 44100) {
        fprintf(stderr, "DMA supply deadline\n");
        return 1;
    }
    if (!batch) {
        fprintf(stderr, "missing foreground batch\n");
        return 1;
    }
    return 0;
}
