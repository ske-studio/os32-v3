/* docs/design/hwdev/gf/display/modes.md: nouveau dispnv04/crtc.c:643 (mode setting;
 * OS32's mode enumeration and information services have no nouveau analogue). */
#include "../internal.h"

#define GF_DISPLAY_MODE_ID 1U
#define GF_DISPLAY_MODE_INDEX 0U
#define GF_DISPLAY_MODE_COUNT 1U

int gf_display_query(void *context, struct video_info *out)
{
    struct gf_display_context *ctx = context;
    if (out) *out = (struct video_info){0};
    if (!ctx || !out) return OS32_ERR_INVAL;
    *out = (struct video_info){
        .caps = ctx->active ? VIDEO_CAP_PALETTE | VIDEO_CAP_BLANK : 0,
        .mode_count = GF_DISPLAY_MODE_COUNT,
        .current_mode = ctx->active ? GF_DISPLAY_MODE_ID : VIDEO_MODE_NONE,
        .active = ctx->active
    };
    return 0;
}

int gf_display_mode_info(void *context, u32 index, struct video_mode *out)
{
    if (out) *out = (struct video_mode){0};
    if (!context || !out || index != GF_DISPLAY_MODE_INDEX) return OS32_ERR_INVAL;
    *out = (struct video_mode){GF_DISPLAY_MODE_ID, GF_MODE_WIDTH, GF_MODE_HEIGHT,
        GF_MODE_PITCH, GFX_FMT_PACKED8, VIDEO_RGB_BITS, GF_MODE_CLOCK_KHZ};
    return 0;
}

int gf_display_framebuffer(void *context, struct video_fb *out)
{
    struct gf_display_context *ctx = context;
    if (out) *out = (struct video_fb){0};
    if (!ctx || !out) return OS32_ERR_INVAL;
    *out = (struct video_fb){ctx->bars[GF_MODE_VRAM_BAR].base,
        GF_MODE_VRAM_BYTES, GF_MODE_PITCH, GFX_FMT_PACKED8};
    return 0;
}

int gf_display_start(void *context, u32 byte_offset)
{
    if (!context) return OS32_ERR_INVAL;
    return byte_offset == GF_MODE_VRAM_OFFSET ? 0 : OS32_ERR_NOSYS;
}
