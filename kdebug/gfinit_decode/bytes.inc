#include "../../lib/le_internal.h"

static int span(u32 size, u32 at, u32 len)
{ return at <= size && len <= size - at ? 0 : -1; }
static u32 le(const u8 *r, u32 at, u32 n)
{
    return le_read_n(r + at, n);
}
static int shift_ok(u32 s)
{ return (s < GF_SHIFT_LEFT ? s : GF_SHIFT_WRAP - s) < GF_SHIFT_BITS; }
