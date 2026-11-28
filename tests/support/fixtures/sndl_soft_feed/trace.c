#define SNDL_PUMP_DIAG
#define main guest_main
#include "userland/diag/sndl_test.c"
#include "../fm_control/check.h"

static u32 output_bytes;
static int position(u32 stream, SND2_Pos *pos)
{
    *pos = (SND2_Pos){.played=100, .written=150, .underruns=2};
    return 0;
}

static int accept(u32 stream, const void *buf, u32 bytes) { return 12; }
static int open_trace(const char *path, int flags) { return 1; }
static void close_trace(int fd) {}
static int write_trace(int fd, const void *buf, u32 bytes)
{
    const struct pump_sample *sample = buf;
    check(bytes >= sizeof *sample && sample->sequence == 3 + output_bytes / sizeof *sample,
          "chronological trace after wrap");
    output_bytes += sizeof *sample;
    return sizeof *sample;
}

void _start(void)
{
    real_api = &observed_api;
    real_api->snd2_stream_pos = position;
    real_api->snd2_stream_write = accept;
    real_api->sys_open = open_trace;
    real_api->sys_close = close_trace;
    real_api->sys_write = write_trace;
    current = samples;
    need_position = 1;
    SND2_Pos pos;
    observed_pos(1, &pos);
    observed_write(1, 0, 128);
    check(current->queued == 50 && current->played == 100 && current->written == 150 &&
          current->underruns == 2, "HAL trace position");
    check(current->added == 3, "accepted frames");
    u32 capacity = sizeof samples / sizeof samples[0];
    for (count = 0; count < capacity + 3; ++count)
        samples[count % capacity].sequence = count;
    check(!dump_samples() && output_bytes == sizeof samples, "complete ring");
    passed();
}
