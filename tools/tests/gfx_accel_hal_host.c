/* Real internal headers under ILP32; no libc or hardware access. */
#include "gfx_hal.h"

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

void _start(void)
{
    GFX_Cmd cmd = {0};
    GfxBackend backend = {0};
    u32 done = 19;
    u8 pixel = 7;
    CHECK(KAPI_VERSION == 83);
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
    CHECK(sizeof(GfxBackend) == 148 && sizeof(GfxDirectOps) == 24);
    CHECK(backend.policy == GFX_POLICY_SHADOW && !backend.direct_ops && !backend.direct_ok);
    CHECK(gfx_hw_submit(&cmd, 1, 0, &done) == OS32_ERR_NOSYS);
    CHECK(done == 19);
    CHECK(gfx_hw_submit(0, 0xffffffffU, 0, 0) == OS32_ERR_NOSYS);
    CHECK(gfx_hw_get(0, 0, 1, 1, &pixel, 1, 0) == OS32_ERR_NOSYS);
    CHECK(pixel == 7);
    finish(0);
}
