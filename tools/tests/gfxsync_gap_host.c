#include "sync_int.h"

void _start(void)
{
    static const struct { u32 lpt, ns, expected; } cases[] = {
        {1000000u, 1250u, 125u}, {1000000u, 0u, 0u},
        {1000000u, 80u, 8u}, {0u, 0xffffffffu, 0u},
        {1u, 9999999u, 0u}, {1u, 10000000u, 1u},
        {0xffffffffu, 10000000u, 0xffffffffu},
        {0xffffffffu, 10000001u, 0xffffffffu},
        {0xffffffffu, 1u, 429u},
    };
    int rc = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        if (gfxsync_gap_loops(cases[i].lpt, cases[i].ns) != cases[i].expected)
            rc = 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
