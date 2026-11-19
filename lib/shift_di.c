#include "types.h"

static unsigned long long join(u32 hi, u32 lo)
{
    return ((unsigned long long)hi << 32) | lo;
}

unsigned long long __ashldi3(unsigned long long value, int count)
{
    u32 hi = value >> 32, lo = value;
    if (!count) return value;
    if (count >= 32) return join(lo << (count - 32), 0);
    return join((hi << count) | (lo >> (32 - count)), lo << count);
}

unsigned long long __lshrdi3(unsigned long long value, int count)
{
    u32 hi = value >> 32, lo = value;
    if (!count) return value;
    if (count >= 32) return join(0, hi >> (count - 32));
    return join(hi >> count, (lo >> count) | (hi << (32 - count)));
}
