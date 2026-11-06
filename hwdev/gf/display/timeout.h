/* Failure-only diagnostics: use the clock sample that rejected the wait.
 * reg=0 denotes a timed delay/deadline, val is the requested delay in us.
 * No polling or clock callback is added to a successful operation. */
#ifndef GF4_TIMEOUT_H
#define GF4_TIMEOUT_H
#include "../../../include/kprintf.h"
static inline i32 gf4_timeout(const char *stage,u32 head,u32 dl,u32 now,
                              u32 reg,u32 value)
{
    kprintf(KLOG_ATTR,"GF4 ERROR stage=%s rc=-409 head=%u remain_us=%d reg=%08x val=%08x\n",
            stage,head,(i32)(dl-now),reg,value);
    return GF4_ETIME;
}
#endif
