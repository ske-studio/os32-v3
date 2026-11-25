/* Execute the guest's acceptance verdict against controlled PCM observations. */
#include "os32api.h"
#include <string.h>
#include <stdarg.h>
#define main snd2_guest_main
#include "guest.inc"
#undef main

enum scenario { NORMAL, NO_CHANGE, DOUBLE_CHANGE, NO_CONTINUATION, NO_AFTER, NO_KEY };
static enum scenario scene;
static u32 latency_us = 29409;
static int latency_run, wave_run, diagnostic, inject_delay;
#ifdef SND2_WRITER_DIAG
static unsigned gap_requests;
#endif
static u32 retired, elapsed_us, reported_us, gaps, waits, time_reads;

static u32 cursor, tick, marker, requests;
static int partial_run, stuck, mid_period;
static u32 public_played, pending_frames, immediate;

/* One simulated interrupt per period while the foreground writer sleeps. */
static void consume_period(void)
{
    elapsed_us += 5805;
    if (cursor - retired >= PERIOD) retired += PERIOD;
    else ++gaps;
}


static int caps(u32 dev, SND2_Caps *out, u32 size)
{
    (void)size;
#ifdef SND2_WRITER_DIAG
    if (dev == SND2_CAPS_OBSERVATION_GAP) ++gap_requests;
#endif
    if ((dev & SND2_CAPS_REFILL) && !diagnostic) return -1;
    *out = (SND2_Caps){.dev = dev, .ring_frames = 4096, .fmt_count = 1};
    out->fmt[0].rate = 44100;
    out->fmt[0].enc = SND2_ENC_S16LE;
    out->fmt[0].channels = 2;
    return 0;
}
static u32 queue_capacity, expected_seconds = 30;
static int open_stream(const SND2_StreamDesc *desc, u32 *h)
{
    queue_capacity = (desc->lead_periods + 1) * PERIOD;
    *h = 1;
    return 0;
}
static int write_stream(u32 h, const void *buf, u32 bytes)
{
    (void)h;
    if (wave_run) {
        pending_frames = bytes / 4;
        u32 free = queue_capacity - (cursor - retired);
        if (bytes > free * 4) bytes = free * 4;
        if (bytes > 128 * 4) bytes = 128 * 4; /* Exercise scratch suffixes. */
        const short *pcm = buf;
        for (u32 i = 0; i < bytes / 4; ++i) {
            u32 frame = cursor + i;
            int mark = frame % PERIOD ? 0 : 10000 + frame / PERIOD % 16000;
            if (pcm[2*i+1] != mark) return -1;
            /* First table entry of each half-cycle is zero. */
            u32 phase = (frame % 441 * 1000) % 44100;
            if ((pcm[2*i] == 0) != ((phase * 32 / 44100) % 16 == 0)) return -1;
        }
    }
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
    if (latency_run) {
        p->epoch = 0;
        if (marker && cursor == queue_capacity + 1) p->played = queue_capacity;
    }
    p->time_ms = tick;
    p->underruns = gaps;
    return 0;
}
static int wait_stream(u32 h, u32 f, u32 ms)
{
    (void)h; (void)ms;
    if (wave_run && f != 44100 * expected_seconds - PERIOD) {
        u32 needed = retired + pending_frames;
        /* At the final short write DMA can reach the unrounded target
         * before retirement. An already satisfied wait advances no time. */
        if (!partial_run && needed % PERIOD) {
            public_played = needed;
            mid_period = 1;
            if (f <= public_played) return 0;
        }
        if (f < needed || (f - needed >= PERIOD &&
                           !(partial_run && immediate && f == needed + PERIOD))) return -1;
        if (partial_run) {
            ++waits;
            if (waits == 1) {
                /* A half-filled segment retires, then DMA enters its successor. */
                retired += PERIOD / 2;
                public_played = retired + PERIOD / 2;
            } else if (public_played >= f || stuck) {
                /* Playback position satisfies wait while the queue is still full. */
                if (++immediate > PERIOD + 1) return -1;
                /* An immediately satisfied wait does not retire a segment. */
            } else {
                do { retired += PERIOD; } while (retired < f);
                public_played = retired;
            }
            return 0;
        }
        ++waits;
        do { consume_period(); } while (retired < f);
        if (inject_delay && waits == 1)
            for (int i = 0; i < 4; ++i) consume_period();
    }
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
static int clock_us(u32 *lo, u32 *hi)
{
    ++time_reads;
    unsigned long long now = 0xfffffff0ULL;
    if (!requests) now -= 100;
    if (marker && cursor > queue_capacity + 1) now += latency_us;
    *lo = wave_run ? elapsed_us : (u32)now;
    *hi = wave_run ? 0 : (u32)(now >> 32);
    return 0;
}
static i32 yield_cpu(void) { if (wave_run) consume_period(); return 0; }
static void report(u8 color, const char *fmt, ...) {
    (void)color;
    if (!strcmp(fmt, "max_writer_us=%u\n")) {
        va_list args;
        va_start(args, fmt);
        reported_us = va_arg(args, u32);
        va_end(args);
    }
}

static int device_count(void) { cursor = retired = gaps = 0; return 1; }
static int device_info(u32 i, SND2_DevInfo *info) { (void)i; *info = (SND2_DevInfo){.kind = SND2_KIND_PCM_OUT}; return 0; }

int main(void)
{
    KernelAPI api = {.snd2_caps = caps, .snd2_stream_open = open_stream,
        .snd2_stream_write = write_stream, .snd2_stream_start = start_stream,
        .snd2_stream_wait = wait_stream, .snd2_stream_pos = position,
        .snd2_stream_close = close_stream, .get_tick = clock_tick,
        .kbd_trygetchar = key, .sys_time_now = clock_us, .sys_yield = yield_cpu,
        .kprintf = report};
    char *args[] = {"snd2_test", "wave", "lead=4", "secs=60"};
    api.snd2_dev_count = device_count;
    api.snd2_dev_info = device_info;
    wave_run = 1;
    if (snd2_guest_main(3, args, &api) || queue_capacity != 5 * PERIOD) return 1;
    expected_seconds = 60;
    if (snd2_guest_main(4, args, &api) || cursor != 44100 * 60 || queue_capacity != 5 * PERIOD) return 1;
    expected_seconds = 30;
    args[2] = "lead=0";
    if (snd2_guest_main(3, args, &api) || queue_capacity != 2 * PERIOD) return 1;
    const char *bad[] = {"lead=16", "lead=4294967297", "lead=", "4", "lead=-1", "lead=4x", "secs=0", "secs=61"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        args[2] = (char *)bad[i];
        if (!snd2_guest_main(3, args, &api)) return 1;
    }
    wave_run = 0;
    for (scene = NORMAL; scene <= NO_AFTER; scene++) {
        cursor = 0;
        if ((playback(&api, 0, "epoch", 3, 30) == 0) != (scene == NORMAL)) return 1;
    }
#ifdef SND2_WRITER_DIAG
    if (gap_requests != NO_AFTER + 1) return 1;
#endif
    latency_run = 1;
    scene = NORMAL;
    cursor = 0;
    if (playback(&api, 0, "latency", 3, 30) || marker != 1 || requests != 2) return 1;
    cursor = requests = marker = 0;
    latency_us = 29410;
    if (!playback(&api, 0, "latency", 3, 30)) return 1;
    scene = NO_KEY;
    cursor = 0;
    if (!playback(&api, 0, "latency", 3, 30)) return 1;
    wave_run = 1;
    for (int run = 0; run < 3; ++run) {
        mid_period = 0;
        diagnostic = run > 0;
        inject_delay = run == 2;
        elapsed_us = 0xfffff000U;
        cursor = retired = gaps = waits = reported_us = time_reads = 0;
        int rc = playback(&api, 0, "wave", 3, 30);
        if (rc != (inject_delay ? 6 : 0) || gaps != (u32)inject_delay || !waits)
            return 1;
        if (!inject_delay && !mid_period) return 1;
        if (reported_us != (diagnostic ? (inject_delay ? 5 : 1) * 5805U : 0) ||
            (!diagnostic && time_reads)) return 1;
    }
    partial_run = 1;
    diagnostic = inject_delay = 0;
    for (stuck = 0; stuck <= 1; ++stuck) {
        cursor = retired = gaps = waits = immediate = public_played = 0;
        int rc = playback(&api, 0, "wave", 3, 30);
        if (rc != (stuck ? 3 : 0) || !immediate || immediate > PERIOD) return 1;
    }
    return 0;
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
