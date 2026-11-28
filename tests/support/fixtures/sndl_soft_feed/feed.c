#include <stdio.h>
#include <stdlib.h>
#include "userland/lib/snd/pump.c"
#include "userland/lib/snd/chip.c"

/* Begin a refill with one period still audible and three periods free.
 * Model synthesis at 1.5 times real time. DMA continues while it runs. */
static unsigned dma, generated, accepted = 256, silence;
static short samples[1024];
KernelAPI *sound_api;

static int position(u32 id, SND2_Pos *p)
{
    *p = (SND2_Pos){.cap = 1024, .played = 0, .state = SND2_ST_RUN};
    return 0;
}

static int publish(u32 id, const void *pcm, u32 bytes)
{
    const short *src = pcm;
    for (unsigned i = 0; i < bytes/4; ++i) {
        if (src[2*i] != accepted + 1) exit(2);
        samples[accepted++] = src[2*i];
    }
    return bytes;
}

static int clock_us(u32 *lo, u32 *hi)
{
    *lo = *hi = 0;
    return 0;
}

int fmv_render_part(FmVoice *v, uint32_t now, unsigned char *out,
                    uint32_t frames, uint32_t offset, uint32_t n)
{
    short *pcm = (short *)out;
    for (unsigned i = 0; i < n; ++i) {
        ++generated;
        pcm[2*i] = generated + 256;
        pcm[2*i+1] = 0;
        while (dma < generated*2/3) {
            if (dma >= accepted) ++silence;
            else if (samples[dma] != dma+1) exit(3);
            ++dma;
        }
    }
    return 0;
}

int main(void)
{
    static KernelAPI api = {.snd2_stream_pos = position, .snd2_stream_write = publish,
                            .sys_time_now = clock_us};
    sound_api = &api;
    refs = stream = 1;
    rate = 44100;
    written = accepted;
    slots[0].fm = (FmVoice *)&api;
    for (unsigned i = 0; i < accepted; ++i) samples[i] = i+1;
    if (sndl_pump() || accepted != 1024 || generated != 768) return 1;
    if (silence) {
        fprintf(stderr, "PCM supply gap: %u frames\n", silence);
        return 1;
    }
    return 0;
}
