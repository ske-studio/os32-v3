/* Runs against S5's fake DMA backend and logical clock in snd2_stream_host.c. */
#include "pcm_contract.h"

static int v1_contract(void)
{
    SND2_DevInfo info = {.kind = SND2_KIND_PCM_OUT, .flags = SND2_DEV_DEFAULT};
    u32 free_bytes, counters;
    CHECK(pcm_open(44100) == OS32_ERR_NOSYS && pcm_open(7) == OS32_ERR_INVAL);
    CHECK(pcm_write_check() == OS32_ERR_INVAL && pcm_close() == OS32_ERR_INVAL);
    desc.fmt = (SND2_Fmt){44100, 16, 2, SND2_ENC_S16LE, 0};
    CHECK(!snd2_register(&backend, 0, &info));
    CHECK(!pcm_open(44100) && pcm_open(44100) == PCM_ERR_BUSY);
    Snd2Stream *s = &snd2_device(0)->stream;
    CHECK(s->period == 2048 && s->lead == 1 && s->v1);
    CHECK(!pcm_status(&free_bytes, &counters) && free_bytes == 32768 && !counters);
    ++owner;
    CHECK(pcm_status(0, 0) == OS32_ERR_INVAL && pcm_write(samples, 0) == OS32_ERR_INVAL);
    --owner;
    CHECK(pcm_write(0, 4) == OS32_ERR_INVAL && !pcm_write(samples, 3));
    for (int i = 0; i < 3; ++i) CHECK(pcm_write(samples, 4096) == 4096);
    CHECK(pcm_write(samples, 4095) == 4092 && !start_calls);
    CHECK(pcm_write(samples, 4) == 4 && start_calls == 1 && s->copied == 4096);
    CHECK(!pcm_status(&free_bytes, 0) && free_bytes == 16384);
    CHECK(!pcm_set_volume(0) && volume_left == 0 && volume_right == 0);
    CHECK(!pcm_set_volume(50) && volume_left == 33 && volume_right == 33);
    CHECK(!pcm_set_volume(100) && volume_left == 64 && volume_right == 64);
    CHECK(pcm_set_volume(101) == OS32_ERR_INVAL);
    hw = 100;
    snd2_advance(0);
    hw = 99;
    snd2_advance(0);
    CHECK(!pcm_status(0, &counters) && counters == 0x00010001);
    s->epoch = 65536;
    s->repeats = s->underruns = 256;
    CHECK(!pcm_status(0, &counters) && counters == 0xffffffffU);
    snd2_reclaim(owner);
    CHECK(pcm_write_check() == OS32_ERR_INVAL);
    CHECK(!pcm_open(44100) && pcm_write(samples, 4) == 4);
    u32 begin = now;
    CHECK(pcm_close() == OS32_ERR_IO && now - begin == 220 && !s->owner);
    CHECK(!snd2_unregister(0));
    desc.fmt.rate = 22050;
    CHECK(!snd2_register(&backend, 0, &info) && !pcm_open(22050));
    CHECK(pcm_write(samples, 8) == 8);
    drain_progress = 1;
    hw = 0;
    int before = start_calls;
    CHECK(!pcm_close() && start_calls == before + 1 && hw == 2048 && !s->owner);
    drain_progress = 0;
    CHECK(!snd2_unregister(0));
    return 0;
}
