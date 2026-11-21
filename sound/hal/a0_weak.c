#include "snd2_int.h"

__attribute__((weak)) int snd2_dev_count(void)
{
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_dev_info(u32 dev, SND2_DevInfo *o)
{
    (void)dev;
    (void)o;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_caps(u32 dev, SND2_Caps *o, u32 size)
{
    (void)dev;
    (void)o;
    (void)size;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_dsp_unit(u32 dev, u32 unit, SND2_DspUnit *o)
{
    (void)dev;
    (void)unit;
    (void)o;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_dsp_param(u32 dev, u32 unit, u32 i, SND2_DspParam *o)
{
    (void)dev;
    (void)unit;
    (void)i;
    (void)o;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_dsp_insert(u32 stream, u32 unit, const i32 *params, u32 n)
{
    (void)stream;
    (void)unit;
    (void)params;
    (void)n;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_dsp_process(u32 dev, u32 unit, const i32 *params, u32 n, const void *in, void *out, u32 frames, u32 fmt_index)
{
    (void)dev;
    (void)unit;
    (void)params;
    (void)n;
    (void)in;
    (void)out;
    (void)frames;
    (void)fmt_index;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_stream_open(const SND2_StreamDesc *d, u32 *stream)
{
    (void)d;
    (void)stream;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_stream_write(u32 stream, const void *buf, u32 bytes)
{
    (void)stream;
    (void)buf;
    (void)bytes;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_stream_start(u32 stream)
{
    (void)stream;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_stream_pause(u32 stream)
{
    (void)stream;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_stream_pos(u32 stream, SND2_Pos *o)
{
    (void)stream;
    (void)o;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_stream_wait(u32 stream, u32 frame, u32 timeout_ms)
{
    (void)stream;
    (void)frame;
    (void)timeout_ms;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_stream_volume(u32 stream, u32 left, u32 right)
{
    (void)stream;
    (void)left;
    (void)right;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_stream_close(u32 stream, u32 how)
{
    (void)stream;
    (void)how;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int av_focus_set(int owner)
{
    (void)owner;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int av_focus_get(void)
{
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int v86s_begin(const V86_Begin *b, u32 *sid)
{
    (void)b;
    (void)sid;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int v86s_run(u32 sid, const V86_RunIn *in, V86_Exit *ex)
{
    (void)sid;
    (void)in;
    (void)ex;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int v86s_fm_drain(u32 sid, SND2_FmEv *out, u32 max, u32 *last_seq)
{
    (void)sid;
    (void)out;
    (void)max;
    (void)last_seq;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int v86s_end(u32 sid)
{
    (void)sid;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_chip_caps(u32 dev, SND2_ChipCaps *o, u32 size)
{
    (void)dev;
    (void)o;
    (void)size;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_chip_open(u32 dev, u32 *h)
{
    (void)dev;
    (void)h;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_chip_write(u32 h, const SND2_RegW *w, u32 n)
{
    (void)h;
    (void)w;
    (void)n;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_chip_read(u32 h, u32 bank, u32 what, u32 reg)
{
    (void)h;
    (void)bank;
    (void)what;
    (void)reg;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_chip_reset(u32 h)
{
    (void)h;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int snd2_chip_close(u32 h)
{
    (void)h;
    return OS32_ERR_NOSYS;
}
