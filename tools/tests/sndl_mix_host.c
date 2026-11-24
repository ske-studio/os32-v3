#include <assert.h>
#include <stdio.h>
#include "mix.h"

static uint32_t calls;
static int accept(void *ctx, const SndlMixSpan *s)
{
    assert(s->count <= 1024 && s->step16);
    ++calls;
    return ctx == NULL;
}

static void render(int16_t *out, unsigned n, unsigned rate)
{
    int32_t acc[32] = {0};
    assert(n <= 16);
    sndl_mix_render(acc, n, rate, accept, NULL);
    sndl_mix_finish(acc, out, n);
}

int main(void)
{
    SND2_Fmt fmt = {4, 8, 1, SND2_ENC_U8, 0};
    unsigned long v, b, id, how, frame;
    const uint8_t pcm[] = {129, 130, 131};
    int16_t out[32];
    assert(!sndl_voice_new(&fmt, &v));
    assert(!sndl_voice_submit(v, pcm, sizeof(pcm), SNDL_LOOP, &b));
    assert(!sndl_voice_play(v));
    render(out, 3, 8);
    assert(out[0] == 256 && out[2] == 256 && out[4] == 512);
    assert(!sndl_voice_stop(v));
    render(out, 2, 8);
    assert(!out[0] && !out[2]);
    assert(!sndl_mix_position(v, &id, &frame) && id == b && frame == 1);
    assert(!sndl_voice_play(v));
    render(out, 4, 8);
    assert(out[0] == 512 && out[2] == 768 && out[4] == 768 && out[6] == 256);
    unsigned long next;
    assert(!sndl_voice_submit(v, pcm, 1, 0, &next));
    render(out, 3, 4);
    assert(sndl_voice_done(v, &id, &how) == SNDL_AGAIN);
    sndl_mix_played(v, b);
    assert(!sndl_voice_done(v, &id, &how) && id == b && how == SNDL_PLAYED);
    assert(!sndl_voice_reset(v));
    assert(!sndl_voice_done(v, &id, &how) && id == next && how == SNDL_RESET);
    assert(!sndl_mix_position(v, &id, &frame) && !id && !frame);
    fmt.bits = 16;
    fmt.channels = 2;
    fmt.enc = SND2_ENC_S16LE;
    unsigned long w, extra;
    const uint8_t stereo[] = {255,127,0,128};
    assert(!sndl_voice_new(&fmt, &w));
    assert(!sndl_voice_submit(w, stereo, 4, SNDL_LOOP, &b));
    assert(!sndl_voice_play(w));
    assert(!sndl_voice_set(w, 4, -100, 0));
    render(out, 1, 4);
    assert(out[0] == 29203 && out[1] == -29204);
    assert(!sndl_voice_set(w, 4, -99, 0));
    render(out, 1, 4);
    assert(out[0] == 29237);
    assert(!sndl_voice_set(w, 4, -10000, 0));
    render(out, 1, 4);
    assert(!out[0] && !out[1]);
    assert(!sndl_voice_set(w, 4, 0, -10000));
    render(out, 1, 4);
    assert(out[0] == 32767 && !out[1]);
    assert(!sndl_voice_set(w, 4, 0, 0));
    assert(!sndl_voice_new(&fmt, &extra));
    assert(!sndl_voice_submit(extra, stereo, 4, SNDL_LOOP, &next));
    assert(!sndl_voice_play(extra));
    render(out, 1, 4);
    assert(out[0] == 32767 && out[1] == -32768);
    assert(!sndl_voice_delete(w));
    assert(!sndl_voice_done(w, &id, &how) && id == b && how == SNDL_RESET);
    unsigned long slots[SNDL_VOICE_MAX - 2];
    for (unsigned i = 0; i < SNDL_VOICE_MAX - 2; ++i)
        assert(!sndl_voice_new(&fmt, &slots[i]));
    assert(sndl_voice_new(&fmt, &id) == SNDL_AGAIN);
    assert(sndl_voice_play(w) < 0 && calls);
    puts("sndl_mix_host OK");
}
