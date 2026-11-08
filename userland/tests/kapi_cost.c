/* CPL=3 cost of one side-effect-free KAPI call, measured in batches. */
#include "os32api.h"
#include <stdint.h>
#include <stdio.h>

#define KAPI_COST_CALLS 1000

int main(int argc, char **argv, KernelAPI *api)
{
    GFX_ScreenInfo si;
    u32 lo0, hi0, lo1, hi1;
    uint64_t start, end;
    int i;

    (void)argc;
    (void)argv;
    if (api->sys_time_now(&lo0, &hi0) != 0) {
        printf("kapi_cost: clock unavailable\n");
        return 1;
    }
    for (i = 0; i < KAPI_COST_CALLS; ++i)
        api->gfx_screen_info(&si);
    if (api->sys_time_now(&lo1, &hi1) != 0) {
        printf("kapi_cost: clock unavailable\n");
        return 1;
    }
    start = ((uint64_t)hi0 << 32) | lo0;
    end = ((uint64_t)hi1 << 32) | lo1;
    if (end < start) {
        printf("kapi_cost: clock went backwards\n");
        return 1;
    }
    /* 1000 calls and 1000 ns/us cancel exactly. */
    printf("gfx_screen_info: %llu ns/call\n", (unsigned long long)(end - start));
    return 0;
}
