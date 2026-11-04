/* Actual frame wait, scripted clock and Python integer oracle. No libc. */
#include "types.h"
#include "splash_raster.h"
#include "splash_wait_vectors.h"

int platform_splash_wait_frame(u32 end_tick);
volatile u32 tick_count;
static struct SplashRaster state;
static unsigned long long times[2];
static u32 calls;

struct SplashRaster *pc98_splash_raster_state(void) { return &state; }
int sys_time_now(u32 *lo, u32 *hi)
{
    if (calls == 2) return -1;
    unsigned long long now = times[calls++];
    *lo = (u32)now;
    *hi = (u32)(now >> BITS_PER_U32);
    return 0;
}

static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}

void _start(void)
{
    for (u32 i = 0; i < sizeof(waits) / sizeof(waits[0]); i++) {
        state.total_lines = waits[i].lines;
        state.line_den = waits[i].den;
        times[0] = waits[i].start;
        times[1] = waits[i].now;
        calls = tick_count = 0;
        if (platform_splash_wait_frame(1) != waits[i].rc) finish(2);
    }
    finish(0);
}
