#include "../../userland/lib/gfx/gfx_direct_internal.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
#define CHECK(x) do { if (!(x)) { static const char m[] = "FAIL " #x "\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(m), "d"(sizeof(m)-1) : "memory"); finish(1); } } while (0)
KernelAPI *gfx_api;
GFX_Framebuffer gfx_fb;
int gfx_ready = 1, gfx_direct, gfx_packed = 1, gfx_lost;
u16 gfx_gen = 1;
static u8 pixels[640 * 400];
static GFX_Cmd seen[64];
static int n;
static int submit(const GFX_Cmd *cmd, u32 count, u32 gen, u32 *done) {
    CHECK(gen == 1); for (u32 i = 0; i < count; i++) seen[n++] = cmd[i]; *done = count; return 0;
}
static void dirty(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void asm_gfx_hline(u8 **p, int b, int x, int x2, u8 c) { (void)p; (void)b; (void)x; (void)x2; (void)c; }
void gfx_pixel(int x, int y, u8 c) { (void)x; (void)y; (void)c; }
static void command(int i, int x, int y, int w, u32 color) {
    CHECK(seen[i].op == GFX_CMD_FILL && seen[i].x == x && seen[i].y == y && seen[i].w == w && seen[i].h == 1 && seen[i].color == color && seen[i].rop == 0 && seen[i].data == 0);
}
void _start(void) {
    static KernelAPI api;
    GFX_Edge edges[2] = {{-2, 1, -2, 3}, {3, 1, 3, 3}};
    int intersections[2];
    GFX_EdgeTable et = {0};
    api.gfx_hw_submit = submit; api.gfx_add_dirty_rect = dirty; gfx_api = &api;
    gfx_fb.width = 640; gfx_fb.height = 400; gfx_fb.pitch = 640; gfx_fb.planes[0] = pixels;
    et.edges = edges; et.num_edges = 2; et.max_edges = 2; et.intersect_buf = intersections; et.max_intersect = 2;
    gfx_scanline_fill(&et, 7, 1, 2);
    CHECK(pixels[640] == 7 && pixels[643] == 7 && pixels[644] == 0 && pixels[1280] == 7 && n == 0);
    gfx_fill_circle(10, 10, 1, 9);
    CHECK(pixels[9 * 640 + 10] == 9 && pixels[10 * 640 + 9] == 9 && pixels[10 * 640 + 11] == 9 && n == 0);
    gfx_direct = 1; pixels[640] = 0;
    gfx_scanline_fill(&et, 7, 1, 2); gfx_fill_circle(10, 10, 1, 9); CHECK(!gfx_flush());
    CHECK(n == 5 && pixels[640] == 0); command(0, 0, 1, 4, 7); command(1, 0, 2, 4, 7);
    command(2, 10, 11, 1, 9); command(3, 10, 9, 1, 9); command(4, 9, 10, 3, 9);
    gfx_fb.planes[0] = 0; gfx_scanline_fill(&et, 7, 1, 2); gfx_fill_circle(10, 10, 1, 9); CHECK(!gfx_flush());
    finish(0);
}
