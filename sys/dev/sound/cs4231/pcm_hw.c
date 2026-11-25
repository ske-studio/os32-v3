#include "sys/hal/sound/snd2_refill_diag.h"
#include "pcm_hw.h"
#include "pcm_module.h"
#include "pcm_cs4231.h"
#include "sys/hal/sound/snd2_int.h"

static int dev = -1;

static void advance(void)
{
    snd2_advance((u32)dev);
}

static int caps(void *ctx, SND2_Caps *out)
{
    (void)ctx;
    *out = (SND2_Caps){
        .fmt_count = 2,
        .fmt = {{PCM_RATE_44100, 16, 2, SND2_ENC_S16LE, 0},
                {PCM_RATE_22050, 16, 2, SND2_ENC_S16LE, 0}},
        .period_min = 256, .period_max = PCM_HALF_FRAMES,
        .ring_frames = PCM_RING_FRAMES, .volume_steps = PCM_DA_ATT_MAX + 1
    };
#ifdef SND2_REFILL_DIAG
    u32 pi, ticks, max_us;
    pcm_codec_counters(&pi, &ticks, &max_us);
    out->reserved[SND2_DIAG_PI] = pi;
    out->reserved[SND2_DIAG_TICK] = ticks;
    out->reserved[SND2_DIAG_MAX_US] = max_us;
#endif
    return 0;
}

static int hw_open(void *ctx, const SND2_Fmt *fmt, u32 period, u8 **ring)
{
    (void)ctx;
    return pcm_codec_open(fmt->rate, period, advance, ring);
}

static int hw_start(void *ctx)
{
    (void)ctx;
    int rc = pcm_codec_start();
    if (!rc) pcm_module_refill_tick(dev);
    return rc;
}

static int hw_pos(void *ctx, u32 *frame)
{
    (void)ctx;
    return pcm_codec_pos(frame);
}

static int hw_ack(void *ctx)
{
    (void)ctx;
    return pcm_codec_ack();
}

static int hw_stop(void *ctx)
{
    (void)ctx;
    pcm_module_refill_tick(-1);
    return pcm_codec_stop();
}

static void hw_release(void *ctx, int leak)
{
    (void)ctx;
    pcm_codec_release(leak);
}

static u8 attenuation(u32 step)
{
    return step ? PCM_DA_ATT_MAX + 1 - step : PCM_DA_MUTE;
}

static int hw_volume(void *ctx, u32 left, u32 right)
{
    (void)ctx;
    pcm_codec_volume(attenuation(left), attenuation(right));
    return 0;
}

static const SndBackend backend = {
    .abi = SND_HAL2_ABI, .caps = caps, .hw_open = hw_open,
    .hw_start = hw_start, .hw_pos = hw_pos, .hw_ack = hw_ack,
    .hw_stop = hw_stop, .hw_release = hw_release, .hw_volume = hw_volume
};

int pcm_hw_register(void)
{
    const SND2_DevInfo info = {
        .kind = SND2_KIND_PCM_OUT, .flags = SND2_DEV_DEFAULT,
        .name = "CS4231", .driver = "cs4231"
    };
    dev = snd2_register(&backend, 0, &info);
    return dev >= 0 ? 0 : dev;
}

int pcm_hw_quiet(void)
{
    pcm_codec_finish_release();
    if (dev >= 0) {
        if (snd2_unregister((u32)dev) < 0) return 0;
        dev = -1;
    }
    return pcm_module_quiet();
}
