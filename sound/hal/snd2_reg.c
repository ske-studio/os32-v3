#include "snd2.h"

#define SND2_MAX_DEV 16
static Snd2Device snd2_devs[SND2_MAX_DEV];

Snd2Device *snd2_device(u32 dev)
{
    if (dev >= SND2_MAX_DEV || !snd2_devs[dev].be)
        return 0;
    return &snd2_devs[dev];
}

int snd2_register(const SndBackend *be, void *ctx, const SND2_DevInfo *info)
{
    SND2_Caps caps = {0};
    if (!info || info->kind != SND2_KIND_PCM_OUT || !be ||
        be->abi != SND_HAL2_ABI || !be->caps || !be->hw_open ||
        !be->hw_start || !be->hw_pos || !be->hw_ack || !be->hw_stop ||
        !be->hw_release || !be->hw_volume)
        return OS32_ERR_INVAL;

    u32 dev;
    for (dev = 0; dev < SND2_MAX_DEV && snd2_devs[dev].be; ++dev) {}
    if (dev == SND2_MAX_DEV)
        return OS32_ERR_FULL;
    int rc = be->caps(ctx, &caps);
    if (rc < 0)
        return rc;
    if (!caps.fmt_count || caps.fmt_count > SND2_MAX_FMT ||
        !caps.period_min || caps.period_min > caps.period_max ||
        caps.period_max > caps.ring_frames / 2 || caps.ring_frames > 4096 ||
        (caps.dsp_units && !be->dsp_unit))
        return OS32_ERR_INVAL;

    Snd2Device *d = &snd2_devs[dev];
    d->info = *info;
    d->info.size = sizeof(d->info);
    d->info.dev = dev;
    d->info.flags = (info->flags & SND2_DEV_DEFAULT) | SND2_DEV_PRESENT | SND2_DEV_READY;
    caps.dev = dev;
    caps.size = sizeof(caps);
    caps.kind = info->kind;
    d->caps = caps;
    d->ctx = ctx;
    d->be = be;
    return dev;
}

int snd2_dev_count(void)
{
    int count = 0;
    for (u32 i = 0; i < SND2_MAX_DEV; ++i)
        if (snd2_device(i))
            ++count;
    return count;
}

/* Enumerate densely even when an earlier device has been removed.
 * The returned info.dev is the stable slot used by backend and stream calls. */
int snd2_dev_info(u32 index, SND2_DevInfo *out)
{
    for (u32 i = 0; out && i < SND2_MAX_DEV; ++i) {
        Snd2Device *d = snd2_device(i);
        if (d && index-- == 0) {
            *out = d->info;
            return 0;
        }
    }
    return OS32_ERR_INVAL;
}

void snd2_fault(Snd2Device *d)
{
    d->info.flags &= ~SND2_DEV_READY;
    d->info.flags |= SND2_DEV_FAULTED;
}

int snd2_unregister(u32 dev)
{
    Snd2Device *d = snd2_device(dev);
    if (!d)
        return OS32_ERR_INVAL;
    if (d->stream.closing || (d->info.flags & SND2_DEV_FAULTED))
        return OS32_ERR_BUSY;
    if (d->stream.owner)
        snd2_teardown_device(d);
    if (d->stream.owner || (d->info.flags & SND2_DEV_FAULTED))
        return OS32_ERR_BUSY;
    d->be = 0;
    d->ctx = 0;
    return 0;
}
