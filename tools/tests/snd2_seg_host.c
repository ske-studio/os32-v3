#include "sound/hal/snd2_int.h"
#include "kstring.h"
#include "mm/kmalloc.h"

#define REQUIRE(expr) do { if (!(expr)) return 1; } while (0)
static u32 ring[4096], staging[SND2_STG_FRAMES], samples[SND2_STG_FRAMES];
static Snd2Stream stream;
static int refuse_alloc, frees;

void *kmalloc(u32 n)
{
    return !refuse_alloc && n == sizeof(staging) ? staging : 0;
}

void kfree(void *p)
{
    if (p == staging) ++frees;
}

void *kmemcpy(void *dst, const void *src, u32 bytes)
{
    for (u32 i = 0; i < bytes; ++i) ((u8 *)dst)[i] = ((const u8 *)src)[i];
    return dst;
}

void *kmemset(void *dst, int value, u32 bytes)
{
    for (u32 i = 0; i < bytes; ++i) ((u8 *)dst)[i] = value;
    return dst;
}

static void setup(u32 period, u32 lead, u32 cap, u32 origin)
{
    Snd2Stream *s = &stream;
    kmemset(s, 0, sizeof(*s));
    s->fmt = (SND2_Fmt){44100, 16, 2, 0, 0};
    s->state = SND2_ST_RUN;
    s->period = period;
    s->lead = lead;
    s->margin = period / 2 < 512 ? period / 2 : 512;
    s->nseg = 4096 / period;
    s->cap = cap;
    s->ring = (u8 *)ring;
    (void)snd2_stg_alloc(s);
    s->written = s->played = origin;
    snd2_seg_reset(s, 0);
}

static void write_frames(u32 first, u32 n)
{
    u32 off;
    n = snd2_stg_reserve(&stream, n, &off);
    snd2_stg_write(&stream, off, samples + first, n);
    snd2_stg_publish(&stream, n);
}

static int append_and_pause(void)
{
    Snd2Stream *s = &stream;
    setup(256, 3, 1024, 0);
    snd2_seg_prime(s);
    REQUIRE(snd2_seg_observe(s, 1, 256, 5) == 0 && !s->underruns);
    write_frames(0, 100);
    REQUIRE(snd2_seg_observe(s, 1, 257, 6) == 0);
    REQUIRE(s->seg_n[2] == 100 && ring[611] == 100 && !ring[612]);
    write_frames(100, 668);
    REQUIRE(snd2_seg_observe(s, 1, 383, 8) == 0);
    REQUIRE(s->seg_n[2] == 256 && ring[612] == 101 && ring[1279] == 768);
    REQUIRE(snd2_seg_observe(s, 1, 384, 9) == 0);
    snd2_seg_pause(s);
    REQUIRE(!ring[768] && ring[767] == 256 && s->copied == 256);
    REQUIRE(snd2_seg_observe(s, 1, 600, 14) == 0 && snd2_seg_played_now(s) == 88);
    REQUIRE(snd2_seg_observe(s, 1, 768, 18) == 0 && s->played == 256 && !s->underruns);
    s->paused = 0;
    REQUIRE(snd2_seg_observe(s, 1, 769, 18) == 0 && ring[1024] == 257);
    return 0;
}

static int empty_period_then_refill(void)
{
    Snd2Stream *s = &stream;
    setup(256, 3, 1024, 0);
    write_frames(0, 1024);
    snd2_seg_prime(s);
    /* A delayed observation lands in the first unprimed segment. */
    REQUIRE(snd2_seg_observe(s, 1, 1024, 24) == 0);
    write_frames(1024, 256);
    REQUIRE(snd2_seg_observe(s, 1, 1025, 24) == 0);
    for (u32 i = 0; i < 256; ++i)
        REQUIRE(!ring[1024 + i] && ring[1280 + i] == samples[1024 + i]);
    REQUIRE(snd2_seg_observe(s, 1, 1280, 30) == 0);
    REQUIRE(s->played == 1024 && s->underruns == 1 && !s->epoch);
    REQUIRE(snd2_seg_observe(s, 1, 1536, 35) == 0);
    REQUIRE(s->played == 1280 && s->underruns == 1);
    return 0;
}

static int margins_and_loss(void)
{
    Snd2Stream *s = &stream;
    setup(256, 3, 1024, 0);
    write_frames(0, 100);
    snd2_seg_prime(s);
    write_frames(100, 500);
    REQUIRE(snd2_seg_observe(s, 1, 128, 3) == 0 && s->copied == 100 && !ring[256]);
    snd2_seg_pause(s);
    REQUIRE(s->copied == 100 && snd2_seg_played_now(s) == 100);
    s->paused = 0;
    REQUIRE(snd2_seg_observe(s, 1, 256, 6) == 0 && s->played == 100);
    REQUIRE(s->underruns == 1 && s->copied == 600);
    REQUIRE(snd2_seg_observe(s, 1, 300, 7) == 0);
    REQUIRE(snd2_seg_observe(s, 1, 299, 8) < 0 && s->copied == 600 && ring[512] == 101);
    snd2_seg_resync(s, 9);
    REQUIRE(s->epoch == 1 && ring[0] == 101 && ring[499] == 600 && !ring[500]);
    REQUIRE(snd2_seg_observe(s, 1, 0, 100) == 0);
    REQUIRE(snd2_seg_observe(s, 0, 0, 103) < 0);
    REQUIRE(snd2_seg_observe(s, 1, 1024, 194) < 0 && s->played == 100);
    REQUIRE(snd2_seg_observe(s, 1, 4096, 101) < 0);
    s->paused = 1;
    snd2_seg_resync(s, 195);
    REQUIRE(s->copied == 100 && s->played == 100 && !ring[0] && s->epoch == 2);
    return 0;
}

static int drain_and_late_observations(void)
{
    Snd2Stream *s = &stream;
    setup(256, 3, 1024, 0);
    write_frames(0, 700);
    snd2_seg_prime(s);
    s->state = SND2_ST_DRAIN;
    REQUIRE(snd2_seg_observe(s, 0, 999, 5) == 0 && !s->last_obs_ms);
    REQUIRE(snd2_seg_observe(s, 1, 800, 20) == 0);
    REQUIRE(s->played == 700 && !s->underruns && !ring[699]);
    snd2_seg_pause(s);
    REQUIRE(s->copied == s->played);

    setup(2048, 1, 8192, 0);
    write_frames(0, 4096);
    snd2_seg_prime(s);
    REQUIRE(snd2_seg_observe(s, 1, 4096 - 512, 82) < 0);
    REQUIRE(!s->played && !s->last_hw && ring[0] == 1 && s->seg_n[0] == 2048);
    return 0;
}

static int revolution_boundary(void)
{
    Snd2Stream *s = &stream;
    setup(256, 3, 1024, 0);
    /* A new position after a gap could already include a full revolution. */
    REQUIRE(snd2_seg_observe(s, 1, 256, 92) < 0 && !s->last_hw);
    REQUIRE(snd2_seg_observe(s, 1, 0, 91) == 0);
    REQUIRE(snd2_seg_observe(s, 1, 0, 92) < 0);

    s->fmt.rate = 32000; /* 4096 frames span exactly 128 ms. */
    snd2_seg_reset(s, 0);
    REQUIRE(snd2_seg_observe(s, 1, 256, 127) == 0);
    REQUIRE(snd2_seg_observe(s, 1, 512, 255) < 0);
    s->fmt.rate = 0;
    REQUIRE(snd2_seg_observe(s, 1, 512, 128) < 0);
    return 0;
}

static int capacity_and_wrap(void)
{
    Snd2Stream *s = &stream;
    u32 off;
    for (u32 period = 256; period <= 2048; period *= 2) {
        u32 cap = period == 2048 ? 8192 : period * 2;
        setup(period, 1, cap, 0xfffffff0U);
        REQUIRE(snd2_stg_reserve(s, ~0U, &off) == cap);
        write_frames(0, cap);
        snd2_seg_prime(s);
        REQUIRE(s->seg_n[0] == period && s->seg_n[1] == period);
        REQUIRE(ring[0] == 1 && ring[period * 2 - 1] == period * 2);
        REQUIRE(!snd2_stg_reserve(s, 1, &off));
        REQUIRE(snd2_seg_observe(s, 1, period, 30) == 0 && ring[period] == period + 1);
        REQUIRE(snd2_stg_reserve(s, cap, &off) == period);
        REQUIRE(snd2_seg_observe(s, 1, 4095, 85) == 0);
        REQUIRE(snd2_seg_observe(s, 1, 0, 86) == 0);
    }
    snd2_stg_free(s);
    REQUIRE(!s->stg && !s->stg_frames && frees == 1);
    refuse_alloc = 1;
    REQUIRE(snd2_stg_alloc(s) == OS32_ERR_NOSPC && !s->stg && !s->stg_frames);
    snd2_stg_free(s);
    REQUIRE(frees == 1);
    return 0;
}

void _start(void)
{
    for (u32 i = 0; i < SND2_STG_FRAMES; ++i) samples[i] = i + 1;
    int rc = append_and_pause() || empty_period_then_refill() || margins_and_loss() ||
             drain_and_late_observations() || revolution_boundary() || capacity_and_wrap();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
