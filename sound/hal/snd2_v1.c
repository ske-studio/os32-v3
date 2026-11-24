#include "snd2.h"
#include "pcm_contract.h"
#include "io.h"

/* The implicit v1 handle shares S5's generation and ownership checks. */
static u32 stream_id;

int pcm_open(u32 rate)
{
    SND2_DevInfo info;
    for (u32 i = 0; snd2_dev_info(i, &info) == 0; ++i) {
        if (!(info.flags & SND2_DEV_DEFAULT)) continue;
        if (info.flags & SND2_DEV_FAULTED) return OS32_ERR_IO;
        if (snd2_device(info.dev)->stream.owner) return PCM_ERR_BUSY;
        if (rate != PCM_RATE_44100 && rate != PCM_RATE_22050) return OS32_ERR_INVAL;
        SND2_StreamDesc desc = {
            .size = sizeof(desc), .dev = info.dev,
            .fmt = {rate, 16, 2, SND2_ENC_S16LE, 0},
            .period_frames = 2048, .lead_periods = 1
        };
        int rc = snd2_open_mode(&desc, &stream_id, 1);
        return rc == OS32_ERR_BUSY ? PCM_ERR_BUSY : rc;
    }
    return rate == PCM_RATE_44100 || rate == PCM_RATE_22050 ? OS32_ERR_NOSYS : OS32_ERR_INVAL;
}

static Snd2Stream *owned(void)
{
    Snd2Device *d;
    if (snd2_owned_device(stream_id, &d) < 0 || !d->stream.v1) return 0;
    return &d->stream;
}

int pcm_write_check(void)
{
    Snd2Stream *s = owned();
    return s && (s->state == SND2_ST_IDLE || s->state == SND2_ST_RUN)
           ? 0 : OS32_ERR_INVAL;
}

int pcm_write(const void *buf, u32 bytes)
{
    if (pcm_write_check() < 0) return OS32_ERR_INVAL;
    int n = snd2_stream_write(stream_id, buf, bytes);
    Snd2Stream *s = owned();
    if (n > 0 && s && s->state == SND2_ST_IDLE && s->written - s->played >= 4096)
        (void)snd2_stream_start(stream_id);
    return n == OS32_ERR_STALE ? OS32_ERR_INVAL : n;
}

static u32 saturate(u32 n, u32 max)
{
    return n > max ? max : n;
}

int pcm_status(u32 *free_bytes, u32 *counters)
{
    unsigned flags = irq_save();
    Snd2Stream *s = owned();
    if (s) {
        if (free_bytes) *free_bytes = (s->cap - (s->written - s->played)) * PCM_FRAME_BYTES;
        if (counters)
            *counters = (saturate(s->underruns, PCM_CNT_U8_MAX) << PCM_CNT_UND_SHIFT) |
                        (saturate(s->repeats, PCM_CNT_U8_MAX) << PCM_CNT_REP_SHIFT) |
                        saturate(s->epoch, PCM_CNT_U16_MAX);
    }
    irq_restore(flags);
    return s ? 0 : OS32_ERR_INVAL;
}

int pcm_set_volume(u32 percent)
{
    if (!owned() || percent > PCM_VOL_MAX) return OS32_ERR_INVAL;
    Snd2Device *d = snd2_device(stream_id & 15U);
    u32 steps = d->caps.volume_steps;
    u32 level = percent && steps
              ? steps - (PCM_VOL_MAX - percent) * (steps - 1) / (PCM_VOL_MAX - 1) : 0;
    return snd2_stream_volume(stream_id, level, level);
}

int pcm_close(void)
{
    if (!owned()) return OS32_ERR_INVAL;
    return snd2_stream_close(stream_id, SND2_CLOSE_DRAIN);
}

void pcm_reclaim(int owner)
{
    snd2_reclaim(owner);
}
