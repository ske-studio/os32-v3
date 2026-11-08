#include "../../userland/lib/gfx/gfx_direct_internal.h"
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
#define CHECK(x) do { if (!(x)) { static const char m[] = "FAIL " #x "\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(m), "d"(sizeof(m)-1) : "memory"); finish(1); } } while (0)
KernelAPI *gfx_api;
int gfx_direct, gfx_lost, gfx_ready = 1, gfx_dirty_suppress, gfx_packed;
u16 gfx_gen = 9;
GFX_Framebuffer gfx_fb = { .width = 8, .height = 6 };
static u8 pixels[6][8], expected[6][8];
void *memmove(void *dst, const void *src, unsigned int n) { u8 *d = dst; const u8 *s = src; if (d > s) while (n) { n--; d[n] = s[n]; } else for (unsigned int i = 0; i < n; i++) d[i] = s[i]; return dst; }
static GFX_Cmd seen[2];
static int count, dirty;
u8 gfx_get_pixel(int x, int y) { return pixels[y][x]; }
void gfx_pixel_nodirty(int x, int y, u8 c) { pixels[y][x] = c; }
static void mark(int x, int y, int w, int h) { if (!gfx_direct) CHECK(x == 1 && y == 1 && w == 7 && h == 5); else CHECK((x == 3 && y == 4 && w == 5 && h == 2) || (x == 0 && y == 1 && w == 4 && h == 2)); dirty++; }
static int submit(const GFX_Cmd *c, u32 n, u32 gen, u32 *done) { CHECK(n == 2 && gen == 9); seen[0] = c[0]; seen[1] = c[1]; count++; *done = n; return 0; }
void _start(void)
{
    static KernelAPI api;
    api.gfx_add_dirty_rect = mark; api.gfx_hw_submit = submit; gfx_api = &api;
    for (int y = 0; y < 6; y++) for (int x = 0; x < 8; x++) pixels[y][x] = expected[y][x] = y * 8 + x;
    for (int y = 1; y < 6; y++) for (int x = 1; x < 8; x++) expected[y][x] = (y - 1) * 8 + x - 1;
    CHECK(!gfx_copy_rect(1, 1, 0, 0, 8, 6));
    CHECK(dirty == 1 && count == 0);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 8; x++) CHECK(pixels[y][x] == expected[y][x]);
    gfx_packed = 1; gfx_fb.pitch = 8; gfx_fb.planes[0] = &pixels[0][0];
    for (int y = 0; y < 6; y++) for (int x = 0; x < 8; x++) pixels[y][x] = expected[y][x] = y * 8 + x;
    for (int y = 1; y < 6; y++) for (int x = 1; x < 8; x++) expected[y][x] = (y - 1) * 8 + x - 1;
    CHECK(!gfx_copy_rect(1, 1, 0, 0, 8, 6));
    for (int y = 0; y < 6; y++) for (int x = 0; x < 8; x++) CHECK(pixels[y][x] == expected[y][x]);
    CHECK(dirty == 2); dirty = 1;
    gfx_direct = 1; gfx_fb.planes[0] = 0;
    CHECK(!gfx_copy_rect(0, 0, 0, 0, 0, 1));
    CHECK(!gfx_copy_rect(0, 0, 0, 0, 1, -1));
    CHECK(!gfx_copy_rect(3, 4, 1, 2, 5, 2));
    CHECK(!gfx_copy_rect(0, 1, 2, 3, 4, 2));
    CHECK(!gfx_flush() && count == 1 && dirty == 3);
    CHECK(seen[0].op == GFX_CMD_COPY && seen[0].x == 3 && seen[0].y == 4 && seen[0].sx == 1 && seen[0].sy == 2 && seen[0].w == 5 && seen[0].h == 2 && seen[0].data == 0);
    CHECK(seen[1].op == GFX_CMD_COPY && seen[1].x == 0 && seen[1].y == 1 && seen[1].sx == 2 && seen[1].sy == 3 && seen[1].w == 4 && seen[1].h == 2 && seen[1].data == 0);
    finish(0);
}
