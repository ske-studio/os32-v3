#include "../hal/sound/snd2.h"
#include "user_read_contract.h"
#include "exec.h"

static void check_read(const void *buf, u32 bytes)
{
    if (bytes && !ring3_user_range_ok((u32)buf, bytes))
        ring3_fault_kill();
}

static int check_audio(const void *in, void *out, u32 frames, const SND2_Fmt *fmt)
{
    u32 stride = (fmt->bits / 8U) * fmt->channels;
    unsigned long long bytes = (unsigned long long)frames * stride;
    if (!stride || bytes > ~(u32)0)
        return 0;
    check_read(in, (u32)bytes);
    if (bytes && !ring3_user_ranges_writable((u32)out, (u32)bytes, 0, 0))
        ring3_fault_kill();
    return 1;
}

int kapi_snd2_dsp_process(u32 dev, u32 unit, const i32 *params, u32 n,
                          const void *in, void *out, u32 frames, u32 fmt_index)
{
    return snd2_process_checked(dev, unit, params, n, in, out, frames, fmt_index, check_audio);
}

int kapi_snd2_stream_write(u32 stream, const void *buf, u32 bytes)
{
    return snd2_write_checked(stream, buf, bytes, check_read);
}
