#include "machcfg_contract.h"

_Static_assert(MACHCFG_FILE_MAX == TEST_SYSCONFIG_MAX - 1, "machine.cfg limit");

static int run(void)
{
    const struct machcfg_seed seeds[] = {
        {{0, 0}, 1}, {{0, 0}, 0}, {{0, 0}, 0}
    };
    struct machcfg values = {2, 1};
    unsigned int i;
    for (i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        machcfg_begin(seeds[i]);
        if (machcfg_get()->perf_request != 0 || machcfg_get()->nap7450 != 0)
            return 1;
        machcfg_finish(&values);
        if (machcfg_get()->perf_request != 2 ||
            machcfg_get()->nap7450 != (i == 0 ? 1 : 0))
            return 2;
        machcfg_finish(0);
        if (machcfg_get()->perf_request != 2 ||
            machcfg_get()->nap7450 != (i == 0 ? 1 : 0))
            return 3;
        machcfg_begin(seeds[i]);
        machcfg_finish(0);
        machcfg_finish(&values);
        if (machcfg_get()->perf_request != 0 || machcfg_get()->nap7450 != 0)
            return 4;
    }
    {
        const struct machcfg_seed seed = {{1, 1}, 0};
        const struct machcfg candidate = {2, 0};
        machcfg_begin(seed);
        if (machcfg_get()->perf_request != 1 || machcfg_get()->nap7450 != 1)
            return 5;
        machcfg_finish(0);
        if (machcfg_get()->perf_request != 1 || machcfg_get()->nap7450 != 1)
            return 6;
        machcfg_begin(seed);
        machcfg_finish(&candidate);
        if (machcfg_get()->perf_request != 2 || machcfg_get()->nap7450 != 1)
            return 7;
    }
    return 0;
}

void _start(void)
{
    int status = run();
    __asm__ volatile ("int $0x80" : : "a" (1), "b" (status));
    __builtin_unreachable();
}
