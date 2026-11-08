#include "libos32filer.h"
#include "tilemap_internal.h"
#include "gfx_direct_internal.h"
#include "ls.h"
#include <stddef.h>

KernelAPI *gfx_api;
GFX_Framebuffer gfx_fb = {.width = 640, .height = 400};
int gfx_direct, gfx_lost, gfx_ready = 1;
u16 gfx_gen = 7;
TilemapState _tilemap;
static int submits, dirty, present_count, expected_direct;
static void fail(void) { __asm__ volatile("int $0x80" : : "a"(1), "b"(1)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(); } while (0)
void *memset(void *s, int c, size_t n) { unsigned char *p = s; while (n--) *p++ = c; return s; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
char *strncpy(char *d, const char *s, size_t n) { char *p = d; while (n && *s) { *d++ = *s++; n--; } while (n--) *d++ = 0; return p; }
int sprintf(char *s, const char *fmt, ...) { (void)fmt; *s = 0; return 0; }
int os32_ls(const char *p, DirCallback cb, void *ctx) { (void)p; (void)cb; (void)ctx; return 0; }
void gfx_fill_rect(int x, int y, int w, int h, u8 c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
void gfx_rect(int x, int y, int w, int h, u8 c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
int kcg_draw_utf8(int x, int y, const char *s, u8 fg, u8 bg) { (void)x;(void)y;(void)s;(void)fg;(void)bg; return 0; }
void gfx_present(void) { present_count++; }
static int key(void) { return 0x1b; }
static int submit(const GFX_Cmd *c, u32 n, u32 gen, u32 *done) {
    CHECK(n == 2 && gen == 7);
    CHECK(c[0].op == GFX_CMD_FILL && c[0].x == 3 && c[0].y == 4 && c[0].w == 5 && c[0].h == 6 && c[0].color == 9);
    CHECK(c[1].op == GFX_CMD_PUT && c[1].x == 10 && c[1].y == 11 && c[1].w == 2 && c[1].h == 1 && c[1].pitch == 2);
    CHECK(((u8 *)c[1].data)[0] == 42 && ((u8 *)c[1].data)[1] == 43);
    submits++; *done = n; return 0;
}
static void present_dirty(void) { CHECK(submits == expected_direct); dirty++; }
static void queue(void) { static const u8 data[] = {42, 43}; CHECK(!gfx_cmd_enqueue_fill(3,4,5,6,9,0)); CHECK(!gfx_cmd_enqueue_put(10,11,2,1,data,2,-1)); }
void _start(void) {
    static KernelAPI api;
    api.gfx_hw_submit = submit; api.gfx_present_dirty = present_dirty; api.kbd_getkey = key;
    gfx_api = &api; _tilemap.kapi = &api; filer_init(&api);
    for (int mode = 0; mode < 2; mode++) {
        gfx_direct = mode; expected_direct = mode; submits = dirty = present_count = 0;
        gfx_direct = 1; queue(); gfx_direct = mode;
        tilemap_present(); CHECK(dirty == 1 && submits == mode && present_count == 0);
        if (!mode) gfx_cmd_drop();
        submits = dirty = 0;
        gfx_direct = 1; queue(); gfx_direct = mode;
        CHECK(filer_open("/", 0) == 0); CHECK(dirty == 1 && submits == mode && present_count == 1);
        if (!mode) gfx_cmd_drop();
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); for (;;) {}
}
