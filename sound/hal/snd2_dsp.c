#include "snd2.h"

static int unit_info(Snd2Device *d, u32 unit, SND2_DspUnit *out)
{
    if (!d)
        return OS32_ERR_INVAL;
    if (!d->caps.dsp_units)
        return OS32_ERR_NOSYS;
    if (!out || !unit || unit > d->caps.dsp_units)
        return OS32_ERR_INVAL;
    SND2_DspUnit info = {0};
    int rc = d->be->dsp_unit(d->ctx, unit, &info);
    if (rc < 0)
        return rc;
    if (info.param_count > SND2_DSP_MAX_PARAMS)
        return OS32_ERR_INVAL;
    *out = info;
    return 0;
}

int snd2_dsp_unit(u32 dev, u32 unit, SND2_DspUnit *out)
{
    return unit_info(snd2_device(dev), unit, out);
}

int snd2_dsp_param(u32 dev, u32 unit, u32 i, SND2_DspParam *out)
{
    Snd2Device *d = snd2_device(dev);
    SND2_DspUnit info;
    int rc = unit_info(d, unit, &info);
    if (rc < 0)
        return rc;
    if (!out || i >= info.param_count)
        return OS32_ERR_INVAL;
    if (!d->be->dsp_param)
        return OS32_ERR_NOSYS;
    SND2_DspParam param = {0};
    rc = d->be->dsp_param(d->ctx, unit, i, &param);
    if (rc >= 0)
        *out = param;
    return rc;
}

int snd2_dsp_insert(u32 stream, u32 unit, const i32 *params, u32 n)
{
    Snd2Device *d;
    int rc = snd2_owned_device(stream, &d);
    if (rc < 0)
        return rc;
    if (!d->caps.dsp_units)
        return OS32_ERR_NOSYS;
    if (unit) {
        SND2_DspUnit info;
        rc = unit_info(d, unit, &info);
        if (rc < 0)
            return rc;
        if (!(info.modes & SND2_DSP_MODE_INSERT) || n > info.param_count ||
            (n && !params))
            return OS32_ERR_INVAL;
    } else if (n) {
        return OS32_ERR_INVAL;
    }
    if (!d->be->dsp_insert)
        return OS32_ERR_NOSYS;
    return d->be->dsp_insert(d->ctx, unit, params, n);
}

int snd2_dsp_process(u32 dev, u32 unit, const i32 *params, u32 n,
                     const void *in, void *out, u32 frames, u32 fi)
{
    Snd2Device *d = snd2_device(dev);
    SND2_DspUnit info;
    int rc = unit_info(d, unit, &info);
    if (rc < 0)
        return rc;
    if (!(info.modes & SND2_DSP_MODE_PROCESS) || n > info.param_count ||
        (n && !params) || frames > info.max_frames || (frames && (!in || !out)) ||
        fi >= d->caps.fmt_count || !(info.in_fmt_mask & info.out_fmt_mask & (1u << fi)))
        return OS32_ERR_INVAL;
    if (!d->be->dsp_process)
        return OS32_ERR_NOSYS;
    return d->be->dsp_process(d->ctx, unit, params, n, in, out, frames, fi);
}
