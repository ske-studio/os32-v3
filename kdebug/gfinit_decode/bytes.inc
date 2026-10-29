static int span(u32 size, u32 at, u32 len)
{ return at <= size && len <= size - at ? 0 : -1; }
static u32 le(const u8 *r, u32 at, u32 n)
{
    u32 v = 0;
    for (u32 i = 0; i < n; i++) v |= (u32)r[at + i] << (8 * i);
    return v;
}
static int shift_ok(u32 s)
{ return (s < GF_SHIFT_LEFT ? s : GF_SHIFT_WRAP - s) < GF_SHIFT_BITS; }
