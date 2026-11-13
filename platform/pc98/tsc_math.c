#include "tsc_clock.h"

/* 64 x 32 -> 96 partial product, followed by a 1..32 bit shift. */
unsigned long long tsc_scale(unsigned long long delta, u32 mult, u32 shift,
                             u32 fraction, u32 *remainder)
{
    unsigned long long low = (unsigned long long)(u32)delta * mult + fraction;
    unsigned long long high = (delta >> 32) * mult + (low >> 32);
    u32 mask = shift == 32 ? ~0u : (1u << shift) - 1;
    *remainder = (u32)low & mask;
    return (high << (32 - shift)) | ((u32)low >> (shift - 1) >> 1);
}

int tsc_coefficient(unsigned long long cycles, u32 us, u32 *mult, u32 *shift)
{
    if (!cycles || !us) return 0;
    for (u32 s = 32; s; --s) {
        unsigned long long numerator = (unsigned long long)us << s;
        unsigned long long m = numerator / cycles;
        if (m && m <= 0xffffffffULL) {
            /* The discarded fraction must contribute no more than 10 ppm. */
            if (numerator - m * cycles > numerator / 100000u) return 0;
            *mult = (u32)m; *shift = s;
            return 1;
        }
    }
    return 0;
}
