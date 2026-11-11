#include "hwdev/gf/accel.h"
static struct gf3_ctx ctx;
static struct gf2_chan ch;
static struct gf3_pattern p;
static struct { u8 subc; u16 method; u32 n, v[32]; } calls[7];
static u32 count, fail_at;
int gf3_format_get(enum gf3_fmt fmt, struct gf3_format *f)
{
    if (fmt == GF3_INDEX8) { f->cpp = 1; f->pat = 3; }
    else if (fmt == GF3_RGB565) { f->cpp = 2; f->pat = 1; }
    else if (fmt == GF3_XRGB8888) { f->cpp = 4; f->pat = 3; }
    else if (fmt == GF3_Y8) { f->cpp = 1; f->pat = 3; }
    else return GF3_EFORMAT;
    return 0;
}
int gf2_emit(struct gf2_chan *c, u8 subc, u16 method, bool noninc,
             const u32 *v, u16 n, u32 dl)
{
    if (c != &ch || noninc || dl != 42 || count >= 7 || n > 32) return -99;
    u32 i = count++;
    calls[i].subc = subc; calls[i].method = method; calls[i].n = n;
    for (u32 j = 0; j < n; j++) calls[i].v[j] = v[j];
    return count == fail_at ? -77 : 0;
}
static void die(int n) { __asm__ volatile("int $0x80" : : "a"(1), "b"(n)); __builtin_unreachable(); }
#define CHECK(x) do { if (!(x)) die(__LINE__); } while (0)
static void common(u32 fmt, u32 select, u32 method, u32 n)
{
    const u16 methods[] = {0, 0x300, 0x30c, method, 0, 0x300, 0x2fc};
    CHECK(count == 7);
    for (u32 i = 0; i < 7; i++) {
        CHECK(calls[i].method == methods[i]);
        CHECK(calls[i].subc == (i == 6 ? GF3_SUBC_BLIT : GF3_SUBC_MISC));
    }
    CHECK(calls[0].v[0] == 11 && calls[1].v[0] == fmt && calls[2].v[0] == select);
    CHECK(calls[3].n == n && calls[4].v[0] == 22 && calls[5].v[0] == 0x5a && calls[6].v[0] == 1);
}
void _start(void)
{
    ctx.ch = &ch; ctx.obj[GF3_OBJ_PATTERN].handle = 11; ctx.obj[GF3_OBJ_ROP].handle = 22;
    p.kind = 1; p.bg = 0x1234; p.fg = 0x5678; p.mono[0] = 0x80; p.mono[1] = 0x01000000;
    CHECK(!gf3_rop3_set(&ctx, GF3_SUBC_BLIT, 0x5a, &p, GF3_RGB565, 42));
    common(1, 1, 0x310, 4);
    CHECK(calls[3].v[0] == 0xffff1234 && calls[3].v[1] == 0xffff5678 && calls[3].v[2] == 0x80 && calls[3].v[3] == 0x01000000);
    p.kind = 2; for (u32 i = 0; i < 64; i++) p.color[i] = i | 0x12340000;
    count = 0; CHECK(!gf3_rop3_set(&ctx, GF3_SUBC_BLIT, 0x5a, &p, GF3_INDEX8, 42));
    common(3, 2, 0x400, 16); CHECK(calls[3].v[0] == 0x03020100 && calls[3].v[15] == 0x3f3e3d3c);
    count = 0; CHECK(!gf3_rop3_set(&ctx, GF3_SUBC_BLIT, 0x5a, &p, GF3_RGB565, 42));
    common(1, 2, 0x500, 32); CHECK(calls[3].v[0] == 0x00010000 && calls[3].v[31] == 0x003f003e);
    count = 0; CHECK(!gf3_rop3_set(&ctx, GF3_SUBC_BLIT, 0x5a, 0, GF3_INDEX8, 42));
    common(3, 1, 0x310, 4); for (u32 i = 0; i < 4; i++) CHECK(calls[3].v[i] == ~0U);
    p.kind = 0; count = 0; CHECK(!gf3_rop3_set(&ctx, GF3_SUBC_BLIT, 0x5a, &p, GF3_INDEX8, 42));
    CHECK(calls[3].v[0] == ~0U);
    count = 0; CHECK(gf3_rop3_set(&ctx, 0, 0, &p, GF3_XRGB8888, 42) == GF3_EUNSUPPORTED && !count);
    CHECK(gf3_rop3_set(&ctx, 0, 0, &p, GF3_Y8, 42) == GF3_EFORMAT && !count);
    CHECK(gf3_rop3_set(&ctx, 0, 0, &p, (enum gf3_fmt)99, 42) == GF3_EFORMAT && !count);
    p.kind = 3; CHECK(gf3_rop3_set(&ctx, 0, 0, &p, GF3_INDEX8, 42) == GF3_EINVAL && !count);
    CHECK(gf3_rop3_set(0, 0, 0, 0, GF3_INDEX8, 42) == GF3_EINVAL);
    p.kind = 1;
    for (u32 i = 1; i <= 7; i++) { count = 0; fail_at = i; ctx.last_rc = 0; CHECK(gf3_rop3_set(&ctx, 0, 0, &p, GF3_INDEX8, 42) == -77 && count == i && ctx.last_rc == -77); }
    die(0);
}
