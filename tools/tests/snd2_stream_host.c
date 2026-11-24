#include "snd2.h"
#include "kstring.h"

#define CHECK(e) do { if (!(e)) return 1; } while (0)
static u32 now, hw, id, samples[1024], storage[8192], ring[4096], deadline;
static int owner = 2, stops, releases, leaked, starts, error, stop_error;
static int interrupted, in_copy, freed, alloc_error, open_error, pos_error;
static int yields, drain_progress, start_calls;
static SND2_StreamDesc desc;
unsigned locked;
unsigned irq_save(void) { unsigned prev = locked; locked = 1; return prev; }
void irq_restore(unsigned prev) { locked = prev; }
int res_owner_get(void) { return owner; }
u32 tick_clock_ms(void) { return now; }
i32 exec_sys_yield(void)
{
    Snd2Stream *s = &snd2_device(desc.dev)->stream;
    if (locked || s->state != SND2_ST_DRAIN || s->paused) error = 1;
    ++yields;
    now += 10;
    if (drain_progress) {
        hw = (hw + 256) % 4096;
        snd2_advance(desc.dev);
    }
    return 0;
}
void *kmalloc(u32 n) { return !alloc_error && n == sizeof(storage) ? storage : 0; }
void kfree(void *p) { if (in_copy || p != storage) error = 1; ++freed; }
void *kmemset(void *dst, int value, u32 n)
{
    for (u32 i = 0; i < n; ++i) ((u8 *)dst)[i] = value;
    return dst;
}
void *kmemcpy(void *dst, const void *src, u32 n)
{
    if (dst == storage && interrupted) {
        if (locked) error = 1;
        in_copy = 1;
        snd2_advance(0);
        in_copy = 0;
    }
    for (u32 i = 0; i < n; ++i) ((u8 *)dst)[i] = ((const u8 *)src)[i];
    return dst;
}
static int caps(void *ctx, SND2_Caps *c)
{
    (void)ctx;
    c->fmt_count = 1;
    c->fmt[0] = desc.fmt;
    c->ring_frames = 4096;
    c->period_min = 256;
    c->period_max = 2048;
    c->volume_steps = 64;
    return 0;
}
static int hw_open(void *ctx, const SND2_Fmt *fmt, u32 p, u8 **out)
{
    (void)ctx; (void)fmt; (void)p;
    *out = (u8 *)ring;
    return open_error;
}
static int start(void *ctx) { (void)ctx; if (!locked) error = 1; ++start_calls; return starts; }
static int pos(void *ctx, u32 *p) { (void)ctx; *p = hw; return pos_error; }
static int ack(void *ctx) { (void)ctx; return 1; }
static int stop(void *ctx)
{
    (void)ctx;
    if (!locked || (interrupted && stop_error && ring[0] != 123)) error = 1;
    ++stops;
    return stop_error;
}
static void release(void *ctx, int leak)
{
    (void)ctx;
    ++releases;
    leaked = leak;
    snd2_advance(0);
}
static u32 volume_left, volume_right;
static int volume(void *ctx, u32 l, u32 r) { (void)ctx; volume_left = l; volume_right = r; return 0; }
static const SndBackend backend = {
    .abi = SND_HAL2_ABI, .caps = caps, .hw_open = hw_open, .hw_start = start,
    .hw_pos = pos, .hw_ack = ack, .hw_stop = stop, .hw_release = release,
    .hw_volume = volume
};

static u32 checked;
int ring3_user_range_ok(u32 addr, u32 n)
{
    if (addr != (u32)samples || locked) error = 1;
    checked = n;
    return 1;
}
void ring3_fault_kill(void) { __builtin_trap(); }

static int matrix(void)
{
    for (int state = 0; state < 6; ++state) {
        for (int op = 0; op < 6; ++op) {
            CHECK(!snd2_stream_open(&desc, &id));
            Snd2Stream *s = &snd2_device(0)->stream;
            s->state = state;
            s->paused = state == SND2_ST_PAUSE;
            if (state == 5) snd2_reclaim(owner);
            int before = yields;
            int rc;
            switch (op) {
            case 0: rc = snd2_stream_write(id, samples, 4); break;
            case 1: rc = snd2_stream_start(id); break;
            case 2: rc = snd2_stream_pause(id); break;
            case 3: rc = snd2_stream_close(id, SND2_CLOSE_DRAIN); break;
            case 4: rc = snd2_stream_close(id, SND2_CLOSE_ABORT); break;
            default: rc = snd2_stream_volume(id, 64, 0); break;
            }
            int want = 0;
            if (state == 5) want = OS32_ERR_STALE;
            else if (state == SND2_ST_FAULTED && op != 3 && op != 4) want = OS32_ERR_IO;
            else if ((state == SND2_ST_DRAIN && op <= 2) ||
                     (state == SND2_ST_IDLE && op == 2)) want = OS32_ERR_INVAL;
            else if (!op) want = 4;
            CHECK(rc == want);
            if (op == 3 && (state == SND2_ST_RUN || state == SND2_ST_PAUSE))
                CHECK(yields > before);
            int next = state;
            if (!rc && (op == 4 || (op == 3 && state != SND2_ST_DRAIN)))
                next = 5;
            else if (!rc && op == 1) next = SND2_ST_RUN;
            else if (!rc && op == 2) next = SND2_ST_PAUSE;
            CHECK((s->owner ? s->state : 5) == next);
            snd2_reclaim(owner);
        }
    }
    return 0;
}

#include "snd2_v1_host.c"

static int probe(void)
{
    CHECK(!v1_contract());
    now = hw = start_calls = stops = releases = freed = yields = 0;
    desc = (SND2_StreamDesc){.size=sizeof(desc), .fmt={44100,16,2,SND2_ENC_S16LE,0},
                             .period_frames=256, .lead_periods=3};
    SND2_DevInfo info = {.kind=SND2_KIND_PCM_OUT};
    CHECK(!snd2_register(&backend, 0, &info));
    CHECK(!matrix());
    Snd2Stream *stream = &snd2_device(0)->stream;
    for (int paused = 0; paused <= 1; ++paused) {
        CHECK(!snd2_stream_open(&desc, &id));
        CHECK(snd2_stream_write(id, samples, 4) == 4);
        CHECK(!snd2_stream_start(id));
        now += 100;
        starts = SND2_START_STOPPED;
        if (paused) CHECK(snd2_stream_pause(id) == OS32_ERR_IO);
        else snd2_advance(0);
        CHECK(stream->state == (paused ? SND2_ST_PAUSE : SND2_ST_RUN));
        CHECK(!stream->copied && !stream->played);
        starts = 0;
        int calls = start_calls;
        snd2_advance(0);
        CHECK(start_calls == calls + 1);
        CHECK(stream->copied == (paused ? 0U : 1U) && stream->epoch == 1);
        CHECK(!snd2_stream_close(id, SND2_CLOSE_ABORT));
    }
    CHECK(!snd2_stream_open(&desc, &id));
    CHECK(snd2_stream_write(id, samples, 4) == 4);
    CHECK(!snd2_stream_start(id));
    now += 100;
    starts = SND2_START_STOPPED;
    drain_progress = 1;
    CHECK(snd2_stream_close(id, 0) == OS32_ERR_IO);
    CHECK(!stream->owner && !stream->played);
    drain_progress = 0;
    starts = 0;
    hw = 0;
    alloc_error = 1;
    CHECK(snd2_stream_open(&desc, &id) == OS32_ERR_NOSPC);
    alloc_error = 0;
    open_error = OS32_ERR_IO;
    CHECK(snd2_stream_open(&desc, &id) == OS32_ERR_IO);
    open_error = 0;
    CHECK(!snd2_open_mode(&desc, &id, 1));
    Snd2Stream *s = &snd2_device(0)->stream;
    CHECK(s->cap == 8192);
    u32 old = id;
    snd2_reclaim(owner);
    int count = releases;
    snd2_reclaim(owner);
    CHECK(count == releases);
    CHECK(!snd2_stream_open(&desc, &id));
    CHECK(snd2_stream_start(old) == OS32_ERR_STALE);
    ++owner;
    CHECK(snd2_stream_write(id, 0, 0) == OS32_ERR_INVAL);
    --owner;
    CHECK(snd2_stream_write(id, 0, 0) == OS32_ERR_INVAL);
    CHECK(!snd2_stream_write(id, samples, 3));
    samples[0] = 123;
    CHECK(kapi_snd2_stream_write(id, samples, sizeof(samples)+4) == sizeof(samples));
    CHECK(checked == sizeof(samples));
    CHECK(!snd2_stream_write(id, samples, 4));
    int n = stops;
    starts = SND2_START_STOPPED;
    CHECK(snd2_stream_start(id) == OS32_ERR_IO && s->state == SND2_ST_IDLE);
    CHECK(!ring[0] && !s->copied && s->written == 1024 && stops == n);
    starts = 0;
    CHECK(!snd2_stream_start(id) && ring[0] == 123);
    pos_error = -1;
    CHECK(snd2_stream_pause(id) == OS32_ERR_IO);
    pos_error = 0;
    CHECK(!snd2_stream_pause(id));
    now += 100;
    snd2_advance(0);
    CHECK(s->state == SND2_ST_PAUSE && s->epoch == 1 && !s->copied && !ring[0]);
    CHECK(!snd2_stream_start(id));
    hw = 1;
    snd2_advance(0);
    CHECK(ring[256] == 123);
    drain_progress = 1;
    yields = 0;
    CHECK(!snd2_stream_close(id, 0) && yields == 5 && !s->owner);
    drain_progress = 0;
    CHECK(!snd2_stream_open(&desc, &id));
    CHECK(snd2_stream_write(id, samples, 4) == 4);
    deadline = now + 60; /* ceil(4 periods / 44100 / 10ms) + 3 ticks */
    CHECK(snd2_stream_close(id, 0) == OS32_ERR_IO && now == deadline && !s->owner);
    CHECK(!snd2_stream_open(&desc, &id));
    starts = -1;
    CHECK(snd2_stream_start(id) == OS32_ERR_IO && !s->owner && !leaked);
    starts = 0;
    CHECK(!snd2_stream_open(&desc, &id));
    CHECK(!snd2_stream_start(id));
    ring[0] = 123;
    now += 100;
    stop_error = -1;
    interrupted = 1;
    n = freed;
    CHECK(snd2_stream_write(id, samples, 4) == OS32_ERR_STALE);
    CHECK(freed == n+1 && leaked && !s->epoch && ring[0] == 123);
    CHECK(snd2_stream_open(&desc, &id) == OS32_ERR_IO);
    CHECK(snd2_unregister(0) == OS32_ERR_BUSY);
    interrupted = 0;
    info.flags = SND2_DEV_DEFAULT;
    desc.dev = snd2_register(&backend, 0, &info);
    CHECK(desc.dev == 1 && !pcm_open(44100));
    hw = 0;
    drain_progress = 1;
    CHECK(pcm_write(samples, 4) == 4 && pcm_close() == OS32_ERR_IO);
    s = &snd2_device(desc.dev)->stream;
    CHECK(s->played == 1 && !s->owner && leaked && pcm_open(44100) == OS32_ERR_IO);
    desc.dev = snd2_register(&backend, 0, &info);
    /* A v2 drain can finish from advance. A failed stop there must
     * reach the foreground even after all submitted frames played. */
    CHECK(desc.dev == 2 && !snd2_stream_open(&desc, &id));
    hw = 0;
    CHECK(snd2_stream_write(id, samples, 4) == 4);
    CHECK(snd2_stream_close(id, SND2_CLOSE_DRAIN) == OS32_ERR_IO);
    CHECK(snd2_device(desc.dev)->stream.played == 1 && leaked);
    desc.dev = snd2_register(&backend, 0, &info);
    CHECK(desc.dev == 3 && !snd2_stream_open(&desc, &id));
    CHECK(snd2_stream_close(id, SND2_CLOSE_ABORT) == OS32_ERR_IO);
    CHECK(snd2_stream_start(id) == OS32_ERR_STALE && leaked);
    return error || locked;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
