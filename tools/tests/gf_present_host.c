/* Freestanding test of the actual transfer with fake memory and safe point. */
#include "gfx_hal.h"
#include "hwdev/gfinit_mode.h"
GfxCounters gfx_counters;
#include "../../drivers/video/geforce/gf_present.c"

#define SOURCE_PITCH (GF_MODE_WIDTH + 7U)
#define DISPLAY_PITCH (GF_MODE_WIDTH + 11U)
#define SENTINEL 0xa5U
static u8 source[SOURCE_PITCH * GF_MODE_HEIGHT];
static u8 display[DISPLAY_PITCH * GF_MODE_HEIGHT];
static u32 registers[(GF_PRESENT_PFB_AMOUNT / sizeof(u32)) + 1];
static int calls, fail_at, errors;
#define CHECK(c) do { if (!(c)) errors++; } while (0)
static int point(void *opaque)
{
    int *marker = opaque;
    (*marker)++;
    calls++;
    return calls == fail_at ? -1 : 0;
}
static void reset(void)
{
    for (u32 i = 0; i < sizeof(source); i++) source[i] = (u8)(i * 13U);
    for (u32 i = 0; i < sizeof(display); i++) display[i] = SENTINEL;
    gfx_counters = (GfxCounters){0};
    calls = 0; fail_at = 0;
}
static void verify(int left, int top, int width, int height)
{
    for (u32 y = 0; y < GF_MODE_HEIGHT; y++)
        for (u32 x = 0; x < DISPLAY_PITCH; x++) {
            u8 expected = SENTINEL;
            if ((int)x >= left && (int)x < left + width &&
                (int)y >= top && (int)y < top + height)
                expected = source[y * SOURCE_PITCH + x];
            CHECK(display[y * DISPLAY_PITCH + x] == expected);
        }
}
static int present(int x, int y, int w, int h, int *marker)
{
    return gf_present_rect(source, SOURCE_PITCH, display, DISPLAY_PITCH,
                           (volatile u8 *)registers, x, y, w, h, point, marker);
}
void _start(void)
{
    int marker = 0;
    reset(); CHECK(!present(-3, -2, 8, 5, &marker));
    verify(0, 0, 5, 3); CHECK(calls == 3 && marker == 3);
    CHECK(gfx_counters.present_bytes == 15 && gfx_counters.commits == 1 && !gfx_counters.hw_ops);
    reset(); marker = 0; CHECK(!present(638, 478, 20, 20, &marker));
    verify(638, 478, 2, 2); CHECK(calls == 2 && gfx_counters.present_bytes == 4);
    reset(); marker = 0; CHECK(!present(640, 0, 1, 1, &marker));
    CHECK(!present(-2147483647 - 1, 0, 2147483647, 1, &marker));
    CHECK(!calls && !gfx_counters.commits && !gfx_counters.present_bytes);
    reset(); marker = 0; fail_at = 2; CHECK(present(4, 6, 7, 5, &marker) == -1);
    verify(4, 6, 7, 1); CHECK(calls == 2 && marker == 2);
    CHECK(gfx_counters.present_bytes == 7 && !gfx_counters.commits);
    reset(); marker = 0; fail_at = 1; CHECK(present(4, 6, 7, 1, &marker) == -1);
    verify(0, 0, 0, 0); CHECK(calls == 1 && !gfx_counters.present_bytes && !gfx_counters.commits);
    reset(); marker = 0; CHECK(!present(0, 0, 640, 480, &marker));
    verify(0, 0, 640, 480); CHECK(calls == 480 && gfx_counters.present_bytes == 307200 && gfx_counters.commits == 1);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(errors ? 1 : 0) : "memory");
    for (;;) {}
}
