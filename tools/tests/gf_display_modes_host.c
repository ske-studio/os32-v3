/* Freestanding tests of the GF mode information callbacks. */
#include "hwdev/gf/internal.h"

int gf_display_query(void *, struct video_info *);
int gf_display_mode_info(void *, u32, struct video_mode *);
int gf_display_framebuffer(void *, struct video_fb *);
int gf_display_start(void *, u32);

static unsigned failures, io_calls;
#define CHECK(c) do { if (!(c)) failures++; } while (0)
static int fake_read(void *p, u32 bar, u32 off, u32 width, u32 *value)
{ (void)p; (void)bar; (void)off; (void)width; (void)value; io_calls++; return -1; }
static int fake_write(void *p, u32 bar, u32 off, u32 width, u32 value)
{ (void)p; (void)bar; (void)off; (void)width; (void)value; io_calls++; return -1; }

void _start(void)
{
    struct gf_display_context ctx = {0};
    struct video_info info;
    struct video_mode mode;
    struct video_fb fb;
    ctx.io.read = fake_read; ctx.io.write = fake_write;
    ctx.bars[GF_MODE_VRAM_BAR].base = 0x90000000U;
    CHECK(gf_display_query(&ctx, &info) == 0);
    CHECK(info.mode_count == 1 && info.current_mode == VIDEO_MODE_NONE && !info.active && !info.caps);
    CHECK(gf_display_mode_info(&ctx, 0, &mode) == 0);
    CHECK(mode.id != 0 && mode.width == 640 && mode.height == 480 && mode.pitch == 640);
    CHECK(mode.format == GFX_FMT_PACKED8 && mode.dac_bits == 8 && mode.clock_khz == 25175);
    ctx.active = true;
    CHECK(gf_display_query(&ctx, &info) == 0);
    CHECK(info.active && info.current_mode == mode.id && info.caps == (VIDEO_CAP_PALETTE | VIDEO_CAP_BLANK));
    CHECK(gf_display_framebuffer(&ctx, &fb) == 0);
    CHECK(fb.phys == 0x90000000U && fb.bytes == 307200 && fb.pitch == 640 && fb.format == GFX_FMT_PACKED8);
    CHECK(gf_display_start(&ctx, 0) == 0);
    CHECK(gf_display_start(&ctx, 1) == OS32_ERR_NOSYS);
    CHECK(gf_display_start(&ctx, U32_MAX) == OS32_ERR_NOSYS);
    CHECK(gf_display_start(0, 0) == OS32_ERR_INVAL);
    CHECK(gf_display_query(0, &info) == OS32_ERR_INVAL);
    CHECK(!info.caps && !info.mode_count && !info.current_mode && !info.active);
    CHECK(gf_display_query(&ctx, 0) == OS32_ERR_INVAL);
    CHECK(gf_display_mode_info(&ctx, 1, &mode) == OS32_ERR_INVAL);
    CHECK(!mode.id && !mode.width && !mode.height && !mode.pitch && !mode.format && !mode.dac_bits && !mode.clock_khz);
    CHECK(gf_display_mode_info(0, 0, &mode) == OS32_ERR_INVAL);
    CHECK(gf_display_mode_info(&ctx, 0, 0) == OS32_ERR_INVAL);
    CHECK(gf_display_framebuffer(0, &fb) == OS32_ERR_INVAL);
    CHECK(!fb.phys && !fb.bytes && !fb.pitch && !fb.format);
    CHECK(gf_display_framebuffer(&ctx, 0) == OS32_ERR_INVAL);
    CHECK(io_calls == 0);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failures ? 1 : 0));
    __builtin_unreachable();
}
