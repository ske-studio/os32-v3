#include "av_contract.h"

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
