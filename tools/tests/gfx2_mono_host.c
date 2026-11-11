#include "gfx2_sw.h"
static u8 screen[32], bits[4];
static int calls, fail;
int gfx2_fmt_info(u32 f, struct gfx2_fmt_info *o) { (void)f; o->cpp = 1; return 0; }
void gfx2_px_write(u8 *p, u32 f, u32 v) { (void)f; *p = (u8)v; }
int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, const u8 *p, u32 pitch)
{ (void)c; (void)w; (void)h; (void)pitch; calls++; if (fail) return -5; screen[y*8+x] = *p; return 0; }
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
#define CHECK(x) do { if (!(x)) finish(__LINE__); } while (0)
void _start(void)
{
    GFX2_Op op = {0}; struct gfx2_rect t;
    struct gfx2_exec ex = {0};
    op.fg.v = 1; op.bg.v = 2; op.bg.x = GFX2_CX_TRANSPARENT;
    ex.op = &op; ex.cpu = bits; ex.cpu_pitch = 1; ex.dv.base = screen; ex.dv.pitch = 8;
    ex.dv.fmt = GFX2_FMT_INDEX8; ex.clip = (struct gfx2_rect){0,0,3,1};
    bits[0] = 0xa0; screen[1] = 9;
    CHECK(!gfx2_sw_mono(&ex, &t));
    CHECK(screen[0] == 1 && screen[1] == 9 && screen[2] == 1);
    CHECK(t.x == 0 && t.y == 0 && t.w == 3 && t.h == 1);
    op.bg.x = 0; ex.clip = (struct gfx2_rect){1,0,2,1};
    CHECK(!gfx2_sw_mono(&ex, &t)); CHECK(screen[1] == 2 && screen[2] == 1 && t.x == 1 && t.w == 2);
    ex.dv.sync = (GfxSyncCtx *)1; calls = 0; op.bg.x = GFX2_CX_TRANSPARENT;
    CHECK(!gfx2_sw_mono(&ex, &t)); CHECK(calls == 1 && t.x == 2 && t.w == 1);
    fail = 1; CHECK(gfx2_sw_mono(&ex, &t) == -5 && t.w == 0);
    ex.clip.w = 0; calls = 0; CHECK(!gfx2_sw_mono(&ex, &t) && !calls && !t.w);
    finish(0);
}
