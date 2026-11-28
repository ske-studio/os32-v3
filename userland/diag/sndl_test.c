#include "sndl.h"
#include <string.h>

/* Bind the diagnostic's private KAPI table to the shared mixer so we can
 * sample its actual stream immediately before drain close. */
extern KernelAPI *kapi;
static KernelAPI observed_api;
static KernelAPI *real_api;
static SND2_Pos final_pos;
static int have_pos;

static int observed_close(u32 stream, u32 how)
{
    have_pos = real_api->snd2_stream_pos(stream, &final_pos) == 0;
    return real_api->snd2_stream_close(stream, how);
}

/* Algorithm 7, carrier 1 only, left output. The right channel marks each
 * 256-frame boundary so a capture can be aligned without a guest clock. */
static int soft_tone(KernelAPI *api)
{
    static const SND2_RegW tone[] = {
        {0,0x30,1,0}, {0,0x40,12,0}, {0,0x50,31,0}, {0,0x80,15,0},
        {0,0xb0,7,0}, {0,0xb4,0x80,0}, {0,0xa4,0x22,0}, {0,0xa0,0x69,0},
        {0,0x28,0x10,0}
    };
    static short marker[512] = {0, 12345};
    SND2_Fmt fmt = {44100, 16, 2, SND2_ENC_S16LE, 0};
    SNDL_ChipReq req = {.size=sizeof req, .chip=SND2_CHIP_OPNA,
                        .flags=SNDL_REQ_PREFER_SOFT, .pol={SNDL_POL_SOFT,SNDL_POL_FAIL}};
    SNDL_ChipGot got;
    u32 chip, voice, buf, start, now, hi;
    if (sndl_init(api) || sndl_chip_open(&req, &chip, &got)) return 2;
    if (got.backend != SNDL_SOFT || sndl_voice_new(&fmt, &voice) ||
        sndl_voice_submit(voice, marker, sizeof marker, SNDL_LOOP, &buf) ||
        sndl_voice_play(voice) || api->sys_time_now(&start, &hi) ||
        sndl_chip_write(chip, tone, sizeof tone / sizeof tone[0], start) != sizeof tone / sizeof tone[0])
        return 3;
    do {
        if (sndl_pump() || api->sys_time_now(&now, &hi)) return 4;
    } while (now-start < 2000000);
    if (sndl_voice_delete(voice) || sndl_chip_close(chip)) return 5;
    api->kprintf(7, "sndl soft OPNA left=fnum0269 block4 right=12345 period=256\n");
    return 0;
}

int main(int argc, char **argv, KernelAPI *api)
{
    SND2_Fmt fmt = {22050, 16, 1, SND2_ENC_S16LE, 0};
    short tone[] = {4096, -4096}, out[4] = {0};
    unsigned long voice, buffer, done, how;
    if (argc == 2 && !strcmp(argv[1], "soft")) {
        real_api = api;
        observed_api = *api;
        observed_api.snd2_stream_close = observed_close;
        kapi = &observed_api;
        int rc = soft_tone(kapi);
        kapi = api;
        sndl_init(api);
        if (have_pos) api->kprintf(7, "sndl soft underruns=%u\n", final_pos.underruns);
        if (!rc && (!have_pos || final_pos.underruns)) rc = 6;
        if (rc) api->kprintf(7, "sndl_test soft FAIL rc=%d\n", rc);
        else api->kprintf(7, "sndl_test soft PASS rc=0\n");
        return rc;
    }
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
