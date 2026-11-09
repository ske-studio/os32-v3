#include "video_wc.h"

/* CPU/cache records are unavailable on this provider. */
int arch_video_cache_inspect(u32 phase, u32 base, u32 bytes,
                             void (*report)(const char *))
{
    (void)phase; (void)base; (void)bytes; (void)report;
    return 0;
}
