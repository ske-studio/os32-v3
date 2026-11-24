#include "snd2.h"
#include "kstring.h"

#define CHECK(e) do { if (!(e)) return 1; } while (0)
static Snd2Device device;
static unsigned locked;
static u32 now, yields;
static int action, error, owner = 1;
enum { TICK, ADVANCE, CLOSE, FAULT };

unsigned irq_save(void) { unsigned prev = locked; locked = 1; return prev; }
void irq_restore(unsigned prev) { locked = prev; }
int res_owner_get(void) { if (!locked) error = 1; return owner; }
Snd2Device *snd2_device(u32 dev) { return dev ? 0 : &device; }
void snd2_fault(Snd2Device *d) { (void)d; error = 1; }
u32 tick_clock_ms(void) { return now; }
void kfree(void *ptr) { (void)ptr; }
void *kmemset(void *p, int c, u32 n) { for (u32 i = 0; i < n; ++i) ((u8 *)p)[i] = c; return p; }
void *kmemcpy(void *p, const void *s, u32 n)
{
    for (u32 i = 0; i < n; ++i) ((u8 *)p)[i] = ((const u8 *)s)[i];
    return p;
}
static int stop(void *ctx) { (void)ctx; return 0; }
static void release(void *ctx, int leak) { (void)ctx; if (leak) error = 1; }
static const SndBackend backend = { .hw_stop = stop, .hw_release = release };

i32 exec_sys_yield(void)
{
    if (locked) error = 1;
    ++now;
    ++yields;
    if (action == ADVANCE) ++device.stream.last_hw;
    if (action == CLOSE && snd2_stream_close(0, SND2_CLOSE_ABORT)) error = 1;
    if (action == FAULT) device.stream.state = SND2_ST_FAULTED;
    return 0;
}

static int probe(void)
{
    Snd2Stream *s = &device.stream;
    device.be = &backend;
    s->owner = owner;
    s->period = 256;
    s->cap = 512;
    s->played = 0xfffffffcU;
    s->written = 5;
    s->last_hw = 260;
    s->seg_n[1] = 6;
    s->underruns = 3;
    s->epoch = 7;
    now = 0xffffffffU;
    SND2_Pos pos;
    for (int state = SND2_ST_IDLE; state <= SND2_ST_DRAIN; ++state) {
        s->state = state;
        CHECK(!snd2_stream_pos(0, &pos));
        CHECK(pos.size == sizeof(pos) && pos.state == (u32)state && pos.played == 0);
        CHECK(pos.written == 5 && pos.cap == 512 && pos.underruns == 3);
        CHECK(pos.epoch == 7 && pos.time_ms == now);
        CHECK(!snd2_stream_wait(0, pos.played, 0));
    }
    CHECK(snd2_stream_wait(0, 6, 0) == OS32_ERR_INVAL);
    CHECK(snd2_stream_wait(0, 1, 0) == OS32_ERR_TIMEOUT && !yields);
    CHECK(snd2_stream_wait(0, 1, 2) == OS32_ERR_TIMEOUT && yields == 2);
    action = ADVANCE;
    CHECK(!snd2_stream_wait(0, 2, 2) && yields == 4);
    s->last_hw += 10;
    CHECK(!snd2_stream_pos(0, &pos) && pos.played == 2);
    CHECK(snd2_stream_pos(0, 0) == OS32_ERR_INVAL);
    ++owner;
    CHECK(snd2_stream_wait(0, 2, 0) == OS32_ERR_INVAL);
    --owner;
    action = FAULT;
    CHECK(snd2_stream_wait(0, 3, 1) == OS32_ERR_IO);
    s->state = SND2_ST_RUN;
    action = CLOSE;
    CHECK(snd2_stream_wait(0, 3, 1) == OS32_ERR_STALE);
    CHECK(snd2_stream_pos(0, &pos) == OS32_ERR_STALE);
    return error || locked;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
