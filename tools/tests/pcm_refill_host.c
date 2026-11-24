#include "sound/hal/snd2_int.h"
#include "kstring.h"

#define REQUIRE(x) do { if (!(x)) return 1; } while (0)

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

static int verify(u32 rate, u32 p)
{
    static u32 dma[4096], staging[8192];
    Snd2Stream s = {
        .fmt = {rate, 16, 2, SND2_ENC_S16LE, 0}, .state = SND2_ST_RUN,
        .period = 2048, .lead = 1, .nseg = 2, .margin = 512,
        .ring = (u8 *)dma, .stg = (u8 *)staging, .stg_frames = 8192
    };
    for (u32 i = 0; i < 8192; ++i) staging[i] = i + 1;
    snd2_seg_reset(&s, 0);
    s.written = p + 2048;
    snd2_seg_prime(&s);
    REQUIRE(!snd2_seg_observe(&s, 1, p, 1));
    /* Last safe frame and first protected frame, in either half. */
    u32 next = (p + 2048) % 4096;
    s.written += 17;
    REQUIRE(!snd2_seg_observe(&s, 1, p + 1535, 2));
    REQUIRE(dma[next + 16] == p + 2065 && !dma[next + 17]);
    s.written += 23;
    REQUIRE(!snd2_seg_observe(&s, 1, p + 1536, 3));
    for (u32 i = 0; i < 2048; ++i) {
        REQUIRE(dma[p + i] == p + i + 1);
        REQUIRE(dma[next + i] == (i < 17 ? p + 2049 + i : 0));
    }
    /* A backwards observation is ambiguous. Keep the published data until
     * stop proof lets the caller prime from the last retired frame. */
    REQUIRE(snd2_seg_observe(&s, 1, p + 1535, 4) < 0);
    REQUIRE(s.played == p && s.copied == p + 2065 && !dma[next + 17]);
    snd2_seg_resync(&s, 5);
    REQUIRE(s.epoch == 1 && s.played == p && s.copied == s.written);
    for (u32 i = 0; i < 4096; ++i)
        REQUIRE(dma[i] == (i < 2088 ? p + i + 1 : 0));
    return 0;
}

void _start(void)
{
    int rc = verify(44100, 0) || verify(44100, 2048) ||
             verify(22050, 0) || verify(22050, 2048);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
