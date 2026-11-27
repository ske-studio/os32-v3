#include "sys/hal/gfx/sync/sync_int.h"
#include "os32_gfx2.h"
static u8 vram[4096], data[128], out[128];
static int calls;
static int read32(void *p, u32 off, u32 *v) { u8 *s = p; *v = (u32)s[off] | (u32)s[off+1]<<8 | (u32)s[off+2]<<16 | (u32)s[off+3]<<24; return 0; }
static int hook(void *p) { (void)p; ++calls; return 0; }
static int test(void)
{
    GfxSyncCtx c = { .t = { .va = vram, .bytes = sizeof(vram), .pitch = 64, .fmt = GFX2_FMT_INDEX8, .read32 = read32, .opaque = vram }, .cfg = { .unit = 4, .read = GFXSYNC_R_DIRECT } };
    for (u32 i = 0; i < sizeof(data); ++i) data[i] = (u8)(i + 1);
    if (gfxsync_write_rows(&c, 0, 63, 16, 2, data, 16) != OS32_ERR_INVAL) return 1;
    for (u32 i = 0; i < sizeof(vram); ++i) if (vram[i]) return 2;
    if (gfxsync_write_rows(&c, 0xffffffffu, 0, 2, 1, data, 2) != OS32_ERR_INVAL) return 3;
    return 0;
}
void _start(void) { int rc = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
