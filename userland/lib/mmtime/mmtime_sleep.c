#include "mmtime.h"
#include "os32api.h"

extern KernelAPI *kapi;

void mmt_sleep_ms(u32 ms)
{
    u32 lo, hi;
    unsigned long long end, now;
    mmt_now_us(&lo, &hi);
    now = ((unsigned long long)hi << 32) | lo;
    end = now + (unsigned long long)ms * 1000u;
    do {
        mmt_deliver();
        /* Sleep(0) still gives another cooperative task one turn. */
        if (!ms) {
            kapi->sys_yield();
            return;
        }
        mmt_now_us(&lo, &hi);
        now = ((unsigned long long)hi << 32) | lo;
        if (now >= end) return;
        kapi->sys_yield();
    } while (1);
}
