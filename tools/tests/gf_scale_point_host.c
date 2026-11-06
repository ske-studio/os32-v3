/* CENTER point-sample regression. Hand-selected pixels are independent of
 * the tested coordinate calculation. NV077 IMAGE_IN_FORMAT: cl0077.h:120-125;
 * observed half-pixel tie: gf-accept4/e_all.txt, fmt=4 trial=1, (3,3).
 * This fixture tests the oracle, not SIFM hardware or conversion precision. */
#include "gf_command_runtime_host.h"
#include "../../kdebug/gf_accel_test.c"

void _start(void)
{
    /* Fake VRAM snapshot: row padding is poison; image has a 64-byte pitch. */
    for (u32 i = 0; i < sizeof(before); ++i) before[i] = 0xa7;
    for (u32 y = 0; y < 16; ++y)
    for (u32 x = 0; x < 16; ++x) {
        before[y * 64 + x * 4] = x;
        before[y * 64 + x * 4 + 1] = y;
        before[y * 64 + x * 4 + 2] = 0;
        before[y * 64 + x * 4 + 3] = 255;
    }
    /* 12 -> 24, sr=(2,2), dr=(1,2), clipped output (3,3):
     * source (3,2.5) selects (3,3), packed R/G/B/A = 0/3/3/255. */
    assert(filtered(GF3_XRGB8888, 3U << 20, (2U << 20) + (1U << 19), false)
           == 0xff030300U);
    assert(filtered(GF3_XRGB8888, (3U << 20) - (1U << 19) - 1U,
                    2U << 20, false) == 0xff020200U);
    assert(filtered(GF3_XRGB8888, (3U << 20) - (1U << 19),
                    2U << 20, false) == 0xff030200U);
    /* Round before clamping at the full input extent, including >15. */
    assert(filtered(GF3_XRGB8888, (15U << 20) + (1U << 19),
                    16U << 20, false) == 0xff0f0f00U);
    /* Bilinear keeps fractional coordinates (existing floor ideal). */
    assert(filtered(GF3_XRGB8888, 3U << 20, (2U << 20) + (1U << 19), true)
           == 0xff030200U);
    gf2_host_exit(0);
}
