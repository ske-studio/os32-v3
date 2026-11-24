#include "os32api.h"
#include "mix.h"

static KernelAPI *sound_api;

int sndl_init(KernelAPI *api)
{
    if (!api) return -1;
    sound_api = api;
    return 0;
}

/* Offline rendering consumes each interval synchronously. Keep completions
 * until the mixer has moved each finished buffer to GENERATED. */
struct completions {
    unsigned count;
    struct { uint32_t voice, buffer; } items[512];
};
static struct completions completed;

static int record(void *ctx, const SndlMixSpan *span)
{
    struct completions *c = ctx;
    if (span->last) {
        if (c->count == 512) return 0;
        c->items[c->count].voice = span->voice;
        c->items[c->count++].buffer = span->buf_id;
    }
    return 1;
}

int sndl_render(short *pcm, unsigned long frames, unsigned long rate)
{
    static int32_t stereo[2048];
    if (!sound_api || !pcm || !frames || frames > 1024) return -1;
    for (unsigned i = 0; i < frames * 2; i++) stereo[i] = 0;
    completed.count = 0;
    if (sndl_mix_render(stereo, frames, rate, record, &completed)) return -1;
    sndl_mix_finish(stereo, pcm, frames);
    for (unsigned i = 0; i < completed.count; i++)
        sndl_mix_played(completed.items[i].voice, completed.items[i].buffer);
    return 0;
}
