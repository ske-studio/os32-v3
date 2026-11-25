/* Internal video services, docs/design/include/video_service.md. Not a KAPI/INT 10h ABI. */
#ifndef VIDEO_SERVICE_H
#define VIDEO_SERVICE_H
#include "types.h"
#include "os32_kapi_shared.h" /* GFX_FMT_*, OS32_ERR_INVAL/NOSYS */

#define VIDEO_MODE_NONE U32_MAX
#define VIDEO_PALETTE_ENTRIES 256U
#define VIDEO_RGB_COMPONENTS 3U
#define VIDEO_RGB_BITS 8U
#define VIDEO_PALETTE_BYTES (VIDEO_PALETTE_ENTRIES * VIDEO_RGB_COMPONENTS)
/* Service capabilities, separate from the HAL's GFX_CAP_* acceleration bits.
 * DISPLAY_START means nonzero offsets; zero alone needs no capability bit. */
#define VIDEO_CAP_PALETTE (1U << 0)
#define VIDEO_CAP_BLANK (1U << 1)
#define VIDEO_CAP_DISPLAY_START (1U << 2)

struct video_mode { u32 id, width, height, pitch, format, dac_bits, clock_khz; };
struct video_info { u32 caps, mode_count, current_mode, active; };
struct video_fb { u32 phys, bytes, pitch, format; };
struct video_ops {
    int (*query)(void *context, struct video_info *out);
    int (*mode_info)(void *context, u32 index, struct video_mode *out);
    int (*start)(void *context, u32 mode_id, const u8 *rgb256);
    int (*framebuffer)(void *context, struct video_fb *out);
    int (*palette)(void *context, u32 first, u32 count, const u8 *rgb);
    int (*display_start)(void *context, u32 byte_offset);
    int (*blank)(void *context, int blank);
    int (*stop)(void *context);
};
/* One per device; context and the complete table outlive all synchronous,
 * non-reentrant calls. Unsupported entries are NOSYS stubs, not NULL. */
struct video_device { void *context; const struct video_ops *ops; };

/* All calls: 0 success, OS32_ERR_INVAL invalid arguments/ranges,
 * OS32_ERR_NOSYS unsupported operation, -1 device failure. No new error class.
 * Validate before I/O. Retain the last stage/target/underlying rc privately.
 * Information calls do no I/O, allocation or initialization; zero a non-NULL
 * out on failure. current_mode is a mode ID (VIDEO_MODE_NONE before start),
 * not an array index. active/caps describe successfully established features.
 * format uses GFX_FMT_*; pitch/FB bytes/offsets are bytes, RGB components are
 * 8-bit inputs even when a device reports a narrower DAC in mode_info.
 * Inputs are borrowed only during the call; copy retained mode/palette data.
 */
int video_query(struct video_device *d, struct video_info *out);
int video_mode_info(struct video_device *d, u32 index, struct video_mode *out);
/* First start: devinit then modeset. After a successful stop, revalidate
 * identity/BAR/Command/legacy/PMC before modeset, never repeat devinit.
 * Init/bus/window/stop failure or identity/BAR change makes GF non-reusable. */
int video_start(struct video_device *d, u32 mode_id, const u8 *rgb256);
/* GF returns the DISPLAY reservation (307200 bytes), not BAR1's full length.
 * Verify pitch/height/offset fit that reservation without overflow. */
int video_framebuffer(struct video_device *d, struct video_fb *out);
/* first < 256, 0 < count <= 256-first, rgb non-NULL; shadow partial updates. */
int video_palette(struct video_device *d, u32 first, u32 count, const u8 *rgb);
/* GF: BAR1-relative byte offset 0 succeeds; every nonzero value is NOSYS. */
int video_display_start(struct video_device *d, u32 byte_offset);
/* blank is 0/1; same value succeeds without I/O. Trident may return NOSYS. */
int video_blank(struct video_device *d, int blank);
/* Stopped stop succeeds. Local cleanup then quiesce; the upper layer owns
 * PC-98 routing/restoration. No full timing/state-blob restore or public DPMS. */
int video_stop(struct video_device *d);
#endif
