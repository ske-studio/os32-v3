#include "../../userland/lib/gfx/gfx_direct_internal.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    static const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), \
                     "d"(sizeof(msg) - 1) : "memory"); \
    finish(1); \
} } while (0)

static GFX_Cmd seen[256];
static u8 copied[16000];
static u32 count, bytes, calls, gets;
static int fail_submit, fail_get;
KernelAPI *gfx_api;
int gfx_lost;
int gfx_direct = 1;
GFX_Framebuffer gfx_fb = {.width = 800, .height = 480};
u16 gfx_gen = 123;

static int submit(const GFX_Cmd *cmd, u32 n, u32 gen, u32 *done)
{
    CHECK(gen == 123 && n > 0 && n <= 64);
    calls++;
    for (u32 i = 0; i < n; i++) {
        seen[count++] = cmd[i];
        if (cmd[i].op == GFX_CMD_PUT || cmd[i].op == GFX_CMD_MONO) {
            const u8 *data = (const u8 *)cmd[i].data;
            for (u32 b = 0; b < (u32)cmd[i].h * cmd[i].pitch; b++)
                copied[bytes++] = data[b];
        }
    }
    *done = fail_submit ? n - 1 : n;
    return fail_submit;
}

static int get(int x, int y, int w, int h, u8 *dst, u32 pitch, u32 gen)
{
    CHECK(x == 2 && y == 3 && w == 1 && h == 1 && pitch == 1 && gen == 123);
    CHECK(count > 0 && seen[count - 1].op == GFX_CMD_COPY);
    gets++;
    *dst = 42;
    return fail_get;
}

static void reset(void)
{
    gfx_cmd_drop();
    gfx_lost = 0;
    count = bytes = calls = gets = 0;
    fail_submit = fail_get = 0;
}

void _start(void)
{
    static KernelAPI api;
    static u8 data[6500];
    u8 dst = 0;
    api.gfx_hw_submit = submit;
    api.gfx_hw_get = get;
    gfx_api = &api;
    CHECK(!gfx_flush() && calls == 0);
    gfx_direct = 0;
    CHECK(gfx_cmd_enqueue_fill(0, 0, 1, 1, 0, 0) == OS32_ERR_NOSYS);
    gfx_direct = 1;
    for (int i = 0; i < 65; i++)
        CHECK(!gfx_cmd_enqueue_fill(i, 0, 1, 1, i, i & 1));
    CHECK(calls == 1 && count == 64);
    CHECK(!gfx_flush() && count == 65 && seen[64].color == 64);
    CHECK(seen[1].rop == 1 && seen[1].x == 1);
    reset();
    for (u32 i = 0; i < sizeof data; i++) data[i] = i;
    CHECK(!gfx_cmd_enqueue_put(0, 0, 640, 6, data, 640, -1));
    CHECK(!gfx_cmd_enqueue_put(0, 0, 256, 1, data, 256, -1));
    CHECK(!gfx_cmd_enqueue_put(0, 0, 1, 1, data, 1, -1));
    CHECK(calls == 1 && count == 2);
    reset();
    CHECK(!gfx_cmd_enqueue_put(0, 5, 640, 10, data, 650, 17));
    CHECK(calls == 1 && count == 1 && seen[0].h == 6);
    for (u32 i = 0; i < sizeof data; i++) data[i] = 0;
    CHECK(!gfx_flush() && count == 2 && bytes == 6400);
    CHECK(seen[1].y == 11 && seen[1].h == 4 && seen[1].pitch == 640 && seen[1].key == 17);
    for (u32 r = 0; r < 10; r++)
        for (u32 b = 0; b < 640; b++) CHECK(copied[r * 640 + b] == (u8)(r * 650 + b));
    reset();
    gfx_fb.width = 640;
    for (u32 i = 0; i < 648; i++) data[i] = i;
    CHECK(!gfx_cmd_enqueue_put(-8, 0, 648, 1, data, 648, -1));
    CHECK(!gfx_flush() && count == 1 && seen[0].w == 640 && copied[0] == 8);
    CHECK(!gfx_cmd_enqueue_put(-8, -2, 2, 1, data, 2, -1));
    gfx_fb.width = 800;
    reset();
    data[0] = 0x12; data[1] = 0x34;
    CHECK(!gfx_cmd_enqueue_put(1, 2, 2, 1, data, 2, -1));
    CHECK(!gfx_cmd_enqueue_fill(2, 3, 1, 1, 5, 1));
    data[0] = 0x80;
    CHECK(!gfx_cmd_enqueue_mono(3, 4, 1, 1, data, 1, 6, 2, 3, 4, 1, 1));
    data[0] = 0;
    CHECK(!gfx_flush() && count == 3 && bytes == 3);
    CHECK(seen[0].op == GFX_CMD_PUT && seen[1].op == GFX_CMD_FILL && seen[2].op == GFX_CMD_MONO);
    CHECK(copied[0] == 0x12 && copied[1] == 0x34 && copied[2] == 0x80);
    CHECK(seen[2].key == 2);
    reset();
    for (u32 i = 0; i < sizeof data; i++) data[i] = 0xff;
    CHECK(!gfx_cmd_enqueue_mono(0, 10, 640, 60, data, 80, 1, 0, 0, 10, 640, 60));
    CHECK(calls == 1 && count == 1 && seen[0].h == 51);
    CHECK(!gfx_flush() && seen[1].y == 61 && seen[1].h == 9 && bytes == 4800);
    for (u32 i = 0; i < bytes; i++) CHECK(copied[i] == 0xff);
    reset();
    data[0] = 0xaa; data[1] = 0xcc; data[3] = 0x5a; data[4] = 0x81;
    CHECK(!gfx_cmd_enqueue_mono(10, 20, 16, 2, data, 3, 7, -1, 13, 21, 10, 1));
    data[3] = data[4] = 0;
    CHECK(!gfx_flush() && count == 1 && bytes == 2);
    CHECK(seen[0].x == 13 && seen[0].y == 21 && seen[0].w == 10 && seen[0].h == 1);
    CHECK(seen[0].color == 7 && seen[0].key == -1 && seen[0].pitch == 2);
    CHECK(copied[0] == 0xd4 && copied[1] == 0);
    reset();
    CHECK(!gfx_cmd_enqueue_copy(8, 9, 4, 5, 6, 7));
    CHECK(!gfx_cmd_get(2, 3, 1, 1, &dst, 1) && gets == 1 && dst == 42);
    CHECK(seen[0].x == 8 && seen[0].y == 9 && seen[0].sx == 4 && seen[0].sy == 5);
    reset();
    CHECK(!gfx_cmd_enqueue_fill(0, 0, 1, 1, 1, 0));
    gfx_cmd_drop();
    CHECK(!gfx_flush() && calls == 0);
    CHECK(gfx_cmd_enqueue_fill(32768, 0, 1, 1, 0, 0) == OS32_ERR_INVAL);
    CHECK(gfx_cmd_enqueue_copy(0, 0, -32769, 0, 1, 1) == OS32_ERR_INVAL);
    CHECK(gfx_cmd_enqueue_put(0, 0, 641, 1, data, 641, -1) == OS32_ERR_INVAL);
    CHECK(gfx_cmd_enqueue_put(0, 0, 2, 1, data, 1, -1) == OS32_ERR_INVAL);
    CHECK(gfx_cmd_enqueue_put(0, 0, 1, 3, data, 0xffffffffU, -1) == OS32_ERR_INVAL);
    CHECK(gfx_cmd_enqueue_put(0, 0, 1, 1, data, 1, 256) == OS32_ERR_INVAL);
    CHECK(gfx_cmd_enqueue_mono(0, 0, 8, 1, data, 1, 0, -2, 0, 0, 8, 1) == OS32_ERR_INVAL);
    CHECK(!gfx_cmd_enqueue_mono(0, 0, 8, 1, data, 1, 0, -1, 9, 0, 1, 1));
    CHECK(!gfx_flush() && calls == 0);
    CHECK(!gfx_cmd_enqueue_copy(8, 9, 4, 5, 6, 7));
    fail_submit = OS32_ERR_STALE;
    CHECK(gfx_cmd_get(2, 3, 1, 1, &dst, 1) == OS32_ERR_STALE && gets == 0 && gfx_lost);
    CHECK(!gfx_flush() && calls == 1);
    CHECK(gfx_cmd_enqueue_fill(0, 0, 1, 1, 0, 0) == OS32_ERR_STALE);
    reset();
    CHECK(!gfx_cmd_enqueue_copy(8, 9, 4, 5, 6, 7));
    fail_get = OS32_ERR_IO;
    CHECK(gfx_cmd_get(2, 3, 1, 1, &dst, 1) == OS32_ERR_IO && gfx_lost);
    reset();
    fail_submit = OS32_ERR_NOSYS;
    CHECK(gfx_cmd_enqueue_put(0, 0, 640, 10, data, 640, -1) == OS32_ERR_NOSYS);
    CHECK(gfx_lost && calls == 1 && count == 1);
    CHECK(!gfx_flush() && calls == 1);
    finish(0);
}
