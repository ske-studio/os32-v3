#include "snd2_refill_diag.h"
#include "snd2.h"
#include "kstring.h"

int snd2_caps(u32 dev, SND2_Caps *out, u32 size)
{
    if (dev & SND2_CAPS_REFILL) {
        Snd2Device *active = snd2_device(dev & ~SND2_CAPS_REFILL);
        if (!active || !out || size != sizeof(*out)) return OS32_ERR_INVAL;
        return snd2_refill_caps(active, out);
    }
    Snd2Device *d = snd2_device(dev);
    if (!d || !out || !size || (size & 3) || size > sizeof(*out))
        return OS32_ERR_INVAL;
    kmemcpy(out, &d->caps, size);
    return 0;
}

int snd2_validate_open(const SND2_StreamDesc *desc, int v1,
                       u32 *period, u32 *lead, u32 *cap)
{
    if (!desc || desc->size != sizeof(*desc))
        return OS32_ERR_INVAL;
    Snd2Device *d = snd2_device(desc->dev);
    if (!d)
        return OS32_ERR_INVAL;
    if (d->info.flags & SND2_DEV_FAULTED)
        return OS32_ERR_IO;
    const SND2_Caps *c = &d->caps;
    u32 i;
    for (i = 0; i < c->fmt_count; ++i) {
        const SND2_Fmt *f = &c->fmt[i];
        if (f->rate == desc->fmt.rate && f->bits == desc->fmt.bits &&
            f->channels == desc->fmt.channels && f->enc == desc->fmt.enc &&
            f->flags == desc->fmt.flags)
            break;
    }
    u32 p = desc->period_frames ? desc->period_frames : c->period_max;
    u32 l = desc->lead_periods ? desc->lead_periods : 1;
    if (i == c->fmt_count || p < c->period_min || p > c->period_max ||
        (p & (p - 1)) || c->ring_frames % p || c->ring_frames / p > SND2_MAX_SEGMENTS ||
        l >= c->ring_frames / p)
        return OS32_ERR_INVAL;
    u32 capacity = v1 ? SND2_STG_FRAMES : (l + 1) * p;
    if (d->stream.owner)
        return OS32_ERR_BUSY;
    *period = p;
    *lead = l;
    *cap = capacity;
    return 0;
}
