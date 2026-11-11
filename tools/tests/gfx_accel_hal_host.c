/* Real internal headers under ILP32; no libc or hardware access. */
#include "gfx_hal.h"
#include "gfx_policy.h"

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

#include "os32_kapi_slots.h"
#include "../../gfx/v2/gfx2_int.h"
static u32 bridge_access;
static int bridge_active, bridge_begins, bridge_ends;
int v1_bridge_begin(u32 role, u32 access)
{
    CHECK(role == GFX2_ROLE_DISPLAY && !bridge_active);
    bridge_active = 1; bridge_access = access; bridge_begins++;
    return bridge_begins & 1 ? OS32_ERR_BUSY : OS32_ERR_TIMEOUT;
}
void v1_bridge_end(u32 role, u32 access, u32 fence)
{
    CHECK(role == GFX2_ROLE_DISPLAY && bridge_active && access == bridge_access && !fence);
    bridge_active = 0; bridge_ends++;
}

static GfxBackend selected;
GfxBackend *g_backend = &selected;
u16 gfx_policy_gen = 7;
static int user, deny_read, deny_write, calls, mono_calls, cpu_calls, hook_rc;
static u32 read_addr, read_len, write_addr, write_len, deny_addr;
static int ax, ay, aw, ah, asx, asy, acolor, akey, arop;
static const u8 *adata;
static u32 apitch;
int ring3_call_from_user(void) { return user; }
int ring3_user_range_ok(u32 p, u32 len)
{
    read_addr = p; read_len = len;
    return p && !deny_read && p != deny_addr;
}
int ring3_user_range_writable(u32 p, u32 len)
{
    write_addr = p; write_len = len;
    return p && !deny_write;
}
static int query(GFX_ScreenInfo *info)
{
    info->width = 640; info->height = 480;
    return 0;
}
static int fill(int x, int y, int w, int h, u8 color, u8 rop)
{
    CHECK(bridge_active);
    calls++; ax = x; ay = y; aw = w; ah = h; acolor = color; arop = rop;
    return hook_rc;
}
static int blit(int x, int y, int sx, int sy, int w, int h)
{
    asx = sx; asy = sy;
    return fill(x, y, w, h, 0, 0);
}
static int put(int x, int y, int w, int h, const u8 *data, u32 pitch, int key)
{
    adata = data; apitch = pitch; akey = key;
    return fill(x, y, w, h, 0, 0);
}
static int get(int x, int y, int w, int h, u8 *data, u32 pitch)
{
    return put(x, y, w, h, data, pitch, -1);
}
static int mono(int x, int y, int w, int h, const u8 *data, u32 pitch,
                u8 fg, int bg, int cx, int cy, int cw, int ch)
{
    mono_calls++; asx = x; asy = y;
    CHECK(w == 9 && h == 3 && fg == 0x34);
    adata = data; apitch = pitch; akey = bg;
    return fill(cx, cy, cw, ch, fg, 0);
}
int gfx_hw_mono_cpu(int x, int y, int w, int h, const u8 *data, u32 pitch,
                    u8 fg, int bg, int cx, int cy, int cw, int ch)
{
    cpu_calls++;
    return mono(x, y, w, h, data, pitch, fg, bg, cx, cy, cw, ch);
}
static void commands(void)
{
    u8 data[64];
    GFX_Cmd c[3] = {{.op=GFX_CMD_FILL, .x=-2, .y=-3, .w=10, .h=8,
                     .color=0x1234, .rop=GFX_ROP_XOR}};
    u32 done = 19;
    selected.query = query; selected.fill_rop = fill; selected.blit = blit;
    selected.put = put; selected.get = get; selected.mono = mono;
    user = 1; deny_write = 1;
    CHECK(gfx_hw_submit(c, 1, 0, &done) == OS32_ERR_INVAL);
    CHECK(done == 19 && calls == 0);
    deny_write = 0;
    CHECK(gfx_hw_submit(c, 1, 0, 0) == OS32_ERR_INVAL);
    CHECK(gfx_hw_submit(c, 65, 0, &done) == OS32_ERR_NOSYS && done == 0);
    selected.policy = GFX_POLICY_DIRECT;
    CHECK(gfx_hw_submit(c, 65, 0, &done) == OS32_ERR_STALE);
    CHECK(gfx_hw_submit(c, 65, 7, &done) == OS32_ERR_INVAL);
    CHECK(gfx_hw_submit(0, 0, 7, &done) == 0 && done == 0);
    CHECK(gfx_hw_submit(0, 1, 7, &done) == OS32_ERR_INVAL);
    CHECK(gfx_hw_submit((const GFX_Cmd *)0xfffffff0U, 1, 7, &done) == OS32_ERR_INVAL);
    deny_read = 1;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL && calls == 0);
    deny_read = 0;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0 && done == 1);
    CHECK(read_addr == (u32)c && read_len == sizeof(GFX_Cmd));
    CHECK(write_addr == (u32)&done && write_len == 4);
    CHECK(ax == 0 && ay == 0 && aw == 8 && ah == 5 && acolor == 0x34 && arop == 1);
    c[1] = c[0]; c[2] = c[0]; c[2].op = 99;
    CHECK(gfx_hw_submit(c, 3, 7, &done) == OS32_ERR_INVAL && done == 2);
    hook_rc = OS32_ERR_BUSY;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_BUSY && done == 0);
    hook_rc = 0;
    c[0].rop = 2;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0] = (GFX_Cmd){.op=GFX_CMD_COPY, .x=-2, .y=478, .sx=4, .sy=7, .w=10, .h=8};
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0);
    CHECK(ax == 0 && ay == 478 && asx == 6 && asy == 7 && aw == 8 && ah == 2);
    c[0] = (GFX_Cmd){.op=GFX_CMD_COPY, .x=1, .y=3, .sx=-2, .sy=-1, .w=10, .h=8};
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0);
    CHECK(ax == 3 && ay == 4 && asx == 0 && asy == 0 && aw == 8 && ah == 7);
    c[0].sx = 638; c[0].sy = 479;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0 && aw == 2 && ah == 1);
    c[0].rop = 1;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0] = (GFX_Cmd){.op=GFX_CMD_PUT, .x=-2, .y=-1, .w=9, .h=3,
                     .data=(u32)data, .pitch=12, .key=255};
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0);
    CHECK(adata == data+14 && apitch == 12 && akey == 255 && aw == 7 && ah == 2);
    CHECK(read_addr == (u32)data && read_len == 33);
    deny_addr = (u32)data;
    int saved_calls = calls;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL && calls == saved_calls);
    deny_addr = 0;
    c[0].key = 256;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].key = -2;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].key = -1; c[0].pitch = 8;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].pitch = 0;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].pitch = 0xffffffffU;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].pitch = 12; c[0].data = 0xfffffff0U;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].data = 0;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].data = (u32)data; c[0].op = GFX_CMD_MONO; c[0].pitch = 4; c[0].color = 0x1234;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0);
    CHECK(cpu_calls == 1 && asx == -2 && asy == -1 && ax == 0 && ay == 0 && aw == 7 && ah == 2);
    CHECK(adata == data && apitch == 4 && read_len == 10);
    c[0].key = 255;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0 && cpu_calls == 1 && mono_calls == 2);
    selected.mono = 0;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0 && cpu_calls == 2);
    c[0].pitch = 1;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].pitch = 4; c[0].key = 256;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].key = -2;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == OS32_ERR_INVAL);
    c[0].key = -1; c[0].w = 0;
    int before = calls;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0 && done == 1 && calls == before);
    c[0].w = 9; c[0].x = 640;
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0 && calls == before);
    CHECK(gfx_hw_get(-2, -1, 9, 3, data, 12, 7) == 0);
    CHECK(ax == 0 && ay == 0 && aw == 7 && ah == 2 && adata == data+14);
    CHECK(write_addr == (u32)data && write_len == 33);
    deny_write = 1; before = calls;
    CHECK(gfx_hw_get(0, 0, 9, 3, data, 12, 7) == OS32_ERR_INVAL && calls == before);
    deny_write = 0;
    CHECK(gfx_hw_get(0, 0, 9, 3, data, 8, 7) == OS32_ERR_INVAL);
    CHECK(gfx_hw_get(0, 0, 9, 3, data, 0xffffffffU, 7) == OS32_ERR_INVAL);
    CHECK(gfx_hw_get(0, 0, 9, 3, (u8 *)0xfffffff0U, 12, 7) == OS32_ERR_INVAL);
    CHECK(gfx_hw_get(0, 0, 9, 3, 0, 12, 7) == OS32_ERR_INVAL);
    CHECK(gfx_hw_get(0, 0, 9, 3, data, 12, 8) == OS32_ERR_STALE);
    CHECK(gfx_hw_get(640, 0, 9, 3, data, 12, 7) == 0 && calls == before);
    CHECK(gfx_hw_get(0, 0, 0, 3, 0, 0, 7) == 0);
    CHECK(gfx_hw_get((-2147483647-1), 0, 9, 3, data, 12, 7) == 0);
    hook_rc = OS32_ERR_BUSY;
    CHECK(gfx_hw_get(0, 0, 9, 3, data, 12, 7) == OS32_ERR_BUSY);
    hook_rc = 0; user = 0; deny_read = deny_write = 1;
    c[0] = (GFX_Cmd){.op=GFX_CMD_PUT, .w=9, .h=3, .data=(u32)data, .pitch=12, .key=-1};
    CHECK(gfx_hw_submit(c, 1, 7, &done) == 0);
    CHECK(gfx_hw_get(0, 0, 9, 3, data, 12, 7) == 0);
    c[0].op = GFX_CMD_FILL; c[0].w = 1; c[0].h = 1;
    GFX_Cmd batch[GFX_CMD_MAX];
    for (u32 i = 0; i < GFX_CMD_MAX; i++) batch[i] = c[0];
    CHECK(gfx_hw_submit(batch, GFX_CMD_MAX, 7, &done) == 0 && done == GFX_CMD_MAX);
}

void _start(void)
{
    GFX_Cmd cmd = {0};
    GfxBackend backend = {0};
    u32 done = 19;
    u8 pixel = 7;
    CHECK(KAPI_VERSION >= 83);
    CHECK(KAPI_SLOT_GFX_HW_SUBMIT == 261 && KAPI_SLOT_GFX_HW_GET == 262);
    CHECK(sizeof(GFX_Cmd) == 32);
    CHECK(__builtin_offsetof(GFX_Cmd, color) == 16);
    CHECK(__builtin_offsetof(GFX_Cmd, key) == 20);
    CHECK(__builtin_offsetof(GFX_Cmd, data) == 24);
    CHECK(__builtin_offsetof(GFX_Cmd, pitch) == 28);
    cmd.x = -1; cmd.key = -1;
    CHECK(cmd.x < 0 && cmd.key < 0);
    CHECK(GFX_CMD_FILL == 1 && GFX_CMD_COPY == 2 && GFX_CMD_PUT == 3 && GFX_CMD_MONO == 4);
    CHECK(GFX_CMD_MAX == 64 && GFX_CAP_DIRECT == 0x0010);
    CHECK(GFX_ROP_COPY == 0 && GFX_ROP_XOR == 1);
    CHECK(GFX_POLICY_SHADOW == 0 && GFX_POLICY_DIRECT == 1);
    CHECK(sizeof(GFX_ScreenInfo) == 28);
    CHECK(__builtin_offsetof(GfxBackend, osri_show) == 108);
    CHECK(__builtin_offsetof(GfxBackend, policy) == 112);
    CHECK(__builtin_offsetof(GfxBackend, shadow_blit) == 144);
    CHECK(sizeof(GfxBackend) == 156 && sizeof(GfxDirectOps) == 24);
    CHECK(backend.policy == GFX_POLICY_SHADOW && !backend.direct_ops && !backend.direct_ok);
    CHECK(gfx_hw_submit(&cmd, 1, 0, &done) == OS32_ERR_NOSYS);
    CHECK(done == 0);
    CHECK(gfx_hw_submit(0, 0xffffffffU, 0, &done) == OS32_ERR_NOSYS);
    CHECK(gfx_hw_get(0, 0, 1, 1, &pixel, 1, 0) == OS32_ERR_NOSYS);
    CHECK(pixel == 7);
    commands();
    CHECK(bridge_begins && bridge_begins == bridge_ends && !bridge_active);
    finish(0);
}
