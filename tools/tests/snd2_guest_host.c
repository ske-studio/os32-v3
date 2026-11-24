/* Execute the guest's acceptance verdict against controlled PCM observations. */
#include "os32api.h"
#include <string.h>
#define main snd2_guest_main
#include "guest.inc"
#undef main

enum scenario { NORMAL, NO_CHANGE, DOUBLE_CHANGE, NO_CONTINUATION, NO_AFTER, NO_KEY };
static enum scenario scene;
static int latency_run;
static u32 cursor, tick, marker, requests;

static int caps(u32 dev, SND2_Caps *out, u32 size)
{
    (void)size;
    *out = (SND2_Caps){.dev = dev, .fmt_count = 1};
    out->fmt[0].rate = 44100;
    out->fmt[0].enc = SND2_ENC_S16LE;
    out->fmt[0].channels = 2;
    return 0;
}
static int open_stream(const SND2_StreamDesc *desc, u32 *h)
{
    (void)desc;
    *h = 1;
    return 0;
}
static int write_stream(u32 h, const void *buf, u32 bytes)
{
    (void)h;
    if (((const short *)buf)[1] == -23456) {
        /* The tagged sample must survive a full queue and a partial write. */
        if (++requests == 1) return 0;
        marker++;
        bytes = 4;
    }
    cursor += bytes / 4;
    return bytes;
}
static int position(u32 h, SND2_Pos *p)
{
    (void)h;
    p->written = cursor;
    p->played = cursor;
    p->epoch = scene == NO_AFTER ? 0 : 7;
    if (cursor >= 65536 && scene != NO_CHANGE && scene != NO_AFTER) {
        p->epoch += scene == DOUBLE_CHANGE ? 2 : 1;
        /* Recovery at the final sample leaves no proof of continuation. */
        if (scene == NO_CONTINUATION && cursor < 44100 * 30) p->epoch = 7;
    }
    p->time_ms = tick;
    p->underruns = 0;
    return 0;
}
static int wait_stream(u32 h, u32 f, u32 ms)
{
    (void)h; (void)f; (void)ms;
    return 0;
}
static int start_stream(u32 h) { (void)h; return 0; }
static int close_stream(u32 h, u32 how) { (void)h; (void)how; return 0; }
static u32 clock_tick(void) { return ++tick; }
static int key(void)
{
    if (latency_run) return scene != NO_KEY && requests == 0 ? 'x' : -1;
    if (scene == NO_AFTER || cursor < 65536) return 'b';
    if (scene == NO_CONTINUATION && cursor < 44100 * 30) return -1;
    return 'a';
}
static int clock_us(u32 *lo, u32 *hi) { *lo = tick; *hi = 0; return 0; }
static i32 yield_cpu(void) { return 0; }
static void report(u8 color, const char *fmt, ...) { (void)color; (void)fmt; }

int main(void)
{
    KernelAPI api = {.snd2_caps = caps, .snd2_stream_open = open_stream,
        .snd2_stream_write = write_stream, .snd2_stream_start = start_stream,
        .snd2_stream_wait = wait_stream, .snd2_stream_pos = position,
        .snd2_stream_close = close_stream, .get_tick = clock_tick,
        .kbd_trygetchar = key, .sys_time_now = clock_us, .sys_yield = yield_cpu,
        .kprintf = report};
    for (scene = NORMAL; scene <= NO_AFTER; scene++) {
        cursor = 0;
        if ((playback(&api, 0, "epoch") == 0) != (scene == NORMAL)) return 1;
    }
    latency_run = 1;
    scene = NORMAL;
    cursor = 0;
    if (playback(&api, 0, "latency") || marker != 1 || requests != 2) return 1;
    scene = NO_KEY;
    cursor = 0;
    return playback(&api, 0, "latency") == 0;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

void _start(void)
{
    int rc = main();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
