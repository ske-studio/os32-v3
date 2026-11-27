#include "gfx_hal.h"
#include "gfx_policy.h"
#include "sys/hal/gfx/v2/gfx2_int.h"
#include "sys/dev/video/geforce/accel.h"
#include "sys/dev/video/geforce/feature5.h"

extern int geforce_mono(int, int, int, int, const u8 *, u32, u8, int, int, int, int, int);
static GfxBackend backend;
GfxBackend *g_backend = &backend;
GfxCounters gfx_counters;
u16 gfx_policy_gen = 7;
static struct gf_foundation foundation;
static struct gf2_chan channel = {.f = &foundation};
static struct gf3_ctx ctx = {.ch = &channel};
static struct gf3_surface scan = {.fmt = GF3_INDEX8, .width = 640, .height = 24};
static u8 input[24*84], screen[26][642], reference[26][642];
static int calls, fail_at;
static const u8 *borrow;
static u32 original_pitch;
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc));
    __builtin_unreachable();
}
#define CHECK(expr, msg) do { if (!(expr)) { static const char s[] = msg "\n"; \
    __asm__ volatile("int $0x80" :: "a"(4),"b"(1),"c"(s),"d"(sizeof(s)-1)); finish(1); } } while (0)
int ring3_call_from_user(void) { return 0; }
int ring3_user_range_ok(u32 p, u32 n) { return 1; }
int ring3_user_range_writable(u32 p, u32 n) { return 1; }
int v1_bridge_begin(u32 role, u32 access) { return 0; }
void v1_bridge_end(u32 role, u32 access, u32 fence) {}
i32 gf5_service_poll(struct gf5 *g) { return 0; }
static int clock_us(void *arg, u32 *now) { *now = 10; return 0; }
static int query(GFX_ScreenInfo *info)
{
    info->width = scan.width;
    info->height = scan.height;
    return 0;
}
static int put(int x, int y, int w, int h, const u8 *src, u32 pitch, int key)
{
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++)
            if (src[i*pitch+j] != key) screen[y+i+1][x+j+1] = src[i*pitch+j];
    return 0;
}
int gf3_mono(struct gf3_ctx *c, const struct gf3_surface *s, struct gf3_rect r,
             const struct gf3_ram *ram, u32 fg, u32 bg, struct gf3_rect clip, u32 dl)
{
    calls++;
    if (calls == fail_at) return GF2_EIO;
    CHECK(r.x == clip.x && r.y == clip.y && r.w == clip.w && r.h == clip.h &&
          r.x+r.w <= s->width && r.y+r.h <= s->height, "visible rectangle");
    CHECK(ram->bytes == (r.h-1)*ram->pitch + (r.w+7)/8, "source extent");
    if (borrow) CHECK(ram->ptr == borrow && ram->pitch == original_pitch, "borrowed pitch");
    for (u32 i = 0; i < r.h; i++) {
        const u8 *line = (const u8 *)ram->ptr + i*ram->pitch;
        for (u32 j = 0; j < r.w; j++)
            screen[r.y+i+1][r.x+j+1] = line[j/8] & (128 >> (j%8)) ? fg : bg;
        if (!borrow && r.w%8)
            CHECK(!(line[r.w/8] & (255 >> (r.w%8))), "packed padding");
    }
    return 0;
}
static void compare(int x, int y, int w, int h, int bg, int extra)
{
    GFX_Cmd cmd = {.op=GFX_CMD_MONO, .x=x, .y=y, .w=w, .h=h, .key=bg,
        .color=0x1234, .pitch=(w+7)/8+extra, .data=(u32)input};
    u32 done;
    for (int pass = 0; pass < 2; pass++) {
        for (unsigned i=0; i<sizeof(screen); i++) ((u8 *)screen)[i] = i*19+37;
        calls = 0;
        backend.mono = pass ? geforce_mono : 0;
        borrow = x >= 0 ? input + (y < 0 ? -y : 0)*cmd.pitch : 0;
        original_pitch = cmd.pitch;
        int rc = gfx_hw_submit(&cmd, 1, 7, &done);
        CHECK(rc == 0 && done == 1, "transparent dispatch or submit");
        if (!pass) __builtin_memcpy(reference, screen, sizeof(screen));
        else {
            if (bg < 0 || x+w <= 0 || y+h <= 0) CHECK(!calls, "transparent or empty dispatch");
            else if (x >= 0) CHECK(calls == 1, "borrowed call count");
            for (unsigned i=0; i<sizeof(screen); i++)
                CHECK(((u8 *)reference)[i] == ((u8 *)screen)[i], "clipped pixels");
        }
    }
}
void _start(void)
{
    foundation.io.timing.clock_us = clock_us;
    gf_hook_ctx_bind(&ctx);
    gf_hook_scan_bind(true, &scan);
    backend.policy = GFX_POLICY_DIRECT;
    backend.query = query;
    backend.put = put;
    for (unsigned i=0; i<sizeof(input); i++) input[i] = i*71+(i>>2);
    int widths[] = {1,7,8,9,31,32,33,640};
    for (unsigned k=0; k<sizeof(widths)/sizeof(widths[0]); k++)
        for (int dx=0; dx<8; dx++)
            for (int key=-1; key<=255; key += key < 0 ? 1 : 255) {
                compare(-dx,-2,widths[k],24,key,0);
                compare(637,19,widths[k],5,key,3);
            }
    GFX_Cmd batch[2] = {{.op=GFX_CMD_FILL}, {.op=GFX_CMD_MONO,.x=-1,.w=640,
        .h=24,.key=255,.pitch=80,.data=(u32)input,.color=255}};
    u32 done;
    borrow = 0;
    calls = 0;
    fail_at = 2;
    for (unsigned i=0; i<sizeof(screen); i++) ((u8 *)screen)[i] = 37;
    CHECK(gfx_hw_submit(batch,2,7,&done) == OS32_ERR_IO && done == 1 && calls == 2,
          "second band failure");
    for (int i=0; i<24; i++)
        for (int j=0; j<640; j++)
            CHECK(screen[i+1][j+1] == (i<3 && j<639 ? 255 : 37), "partial band pixels");
    finish(0);
}
