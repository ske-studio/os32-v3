#include "sndl.h"

int main(int argc, char **argv, KernelAPI *api)
{
    SND2_Fmt fmt = {22050, 16, 1, SND2_ENC_S16LE, 0};
    short tone[] = {4096, -4096}, out[4] = {0};
    unsigned long voice, buffer, done, how;
    (void)argc;
    (void)argv;
    if (sndl_init(api) || sndl_voice_new(&fmt, &voice) ||
        sndl_voice_submit(voice, tone, sizeof(tone), 0, &buffer) ||
        sndl_voice_set(voice, fmt.rate, 0, 0) || sndl_voice_play(voice) || sndl_render(out, 2, fmt.rate) ||
        out[0] != tone[0] || out[1] != tone[0] ||
        out[2] != tone[1] || out[3] != tone[1] ||
        sndl_voice_done(voice, &done, &how) || done != buffer || how != SNDL_PLAYED ||
        sndl_voice_stop(voice) || sndl_voice_reset(voice) || sndl_voice_delete(voice)) return 1;
    api->kprintf(7, "sndl_test PASS\n");
    return 0;
}
