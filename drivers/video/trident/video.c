/* Source notices retained from trident_mode.hwb:3–48.
 * Copyright 1991-1999 by The XFree86 Project, Inc.
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * vgaHW.c license: xorg-server-21.1.13 COPYING:54-76
 * Copyright (C) 1994-2003 The XFree86 Project, Inc.  All Rights Reserved.
 * Copyright (C) Colin Harrison 2005-2008
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall not
 * be used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from the XFree86 Project.
 * Copyright 1992-2003 by Alan Hourihane, North Wales, UK.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Alan Hourihane not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Alan Hourihane makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * ALAN HOURIHANE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ALAN HOURIHANE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */
/* MIT/X11 reference: X/trident_video.c (V in TRIDENT_FULL_DESIGN §1).
 * Only the PROVIDIA9682 branches are used. Register numbers below follow
 * V:225-300,609-774,1186-1343; no Cyber zoom/offset flags apply to 9682.
 */
#include "trident.h"
#include "sys.h"
#define TRY(call) do { int rc = (call); if (rc) return rc; } while (0)
#define VIDEO_MAX 1024u             /* V:915-916 */
#define VIDEO_ALIGN 16u             /* V:592,831: pixels / packed bytes */
#define VIDEO_OFF_DELAY 800u        /* V:42, milliseconds */
#define VIDEO_FREE_DELAY 60000u     /* V:43, milliseconds */
#define VIDEO_FIX_PERIOD 100        /* V:1199 */
#define VIDEO_US_PER_MS 1000u       /* sys_time_now returns microseconds */
#define VIDEO_FP 65536             /* xf86XVClipVideoHelper's 16.16 */
#define VIDEO_VSYNC 8u             /* VGA Input Status 1, bit 3 */
#define VIDEO_STOP_CONTROL 0x84u    /* V:402,1165: stop overlay */
#define VIDEO_KEY_BASE 0x50u        /* V:378-379, 9682 */

static int video_out(struct trident_device *d, u8 index, u8 value)
{
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_EXT_DAC_INDEX, index));
    return trident_write8(d, TRI_SPACE_REGS, TRI_EXT_DAC_DATA, value);
}

/* V:1332-1343: each start/stop waits for two complete low->high edges. */
static int video_vblank(struct trident_device *d)
{
    u8 status;
    for (u32 frame = 0; frame < 2; ++frame) {
        do { TRY(trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6u, &status)); }
        while (status & VIDEO_VSYNC);
        do { TRY(trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6u, &status)); }
        while (!(status & VIDEO_VSYNC));
    }
    return 0;
}

static u32 video_depth(const struct trident_device *d)
{
    return d->mode.valid ? d->mode.selected.depth : 8u;
}

int trident_video_reset(struct trident_device *d)
{
    static u32 key_depth; /* Single Trident device; depth of the packed key. */
    u32 depth;
    if (!d) return TRI_E_INVAL;
    depth = video_depth(d);
    /* V:356-362 adaptor setup, D:2090-2093 default packed color key.
     * Surface resets preserve the key until the framebuffer depth changes. */
    if (key_depth != depth ||
        (!d->video.fix_frame && !d->video.hsync && !d->video.vsync)) {
        if (depth == 8) d->video.color_key = 0xFF;
        else if (depth == 15) d->video.color_key = (1u << 10) | (1u << 5) | ((1u << 5) - 2);
        else if (depth == 16) d->video.color_key = (1u << 11) | (1u << 5) | ((1u << 5) - 2);
        else d->video.color_key = (1u << 16) | (1u << 8) | ((1u << 8) - 2);
        d->video.fix_frame = VIDEO_FIX_PERIOD;
        key_depth = depth;
    }
    TRY(video_vblank(d));
    TRY(trident_indexed_write(d, d->cur_vga_base, 0x8E, 0x84));
    TRY(video_out(d, 0x57, 0xC0));
    TRY(trident_indexed_write(d, d->cur_vga_base, 0xBE, 0x26));
    /* Preserve the S3 reset contract before a selected mode is published. */
    if (!d->mode.valid) {
        d->video.color_key = 0xFF;
        TRY(video_out(d, 0x50, 0xFF));
        TRY(video_out(d, 0x51, 0));
        TRY(video_out(d, 0x52, 0));
        TRY(video_out(d, 0x54, 0xFF));
        TRY(video_out(d, 0x55, 0));
        return video_out(d, 0x56, 0);
    }
    TRY(video_out(d, VIDEO_KEY_BASE, (u8)d->video.color_key));
    TRY(video_out(d, VIDEO_KEY_BASE + 1, depth == 8 ? 0 : (u8)(d->video.color_key >> 8)));
    TRY(video_out(d, VIDEO_KEY_BASE + 2, depth == 24 ? (u8)(d->video.color_key >> 16) : 0));
    TRY(video_out(d, VIDEO_KEY_BASE + 4, 0xFF));
    TRY(video_out(d, VIDEO_KEY_BASE + 5, depth == 8 ? 0 : 0xFF));
    return video_out(d, VIDEO_KEY_BASE + 6, depth == 24 ? 0xFF : 0);
}

static int video_disable(struct trident_device *d)
{
    TRY(video_vblank(d));
    TRY(trident_indexed_write(d, d->cur_vga_base, 0x8E, VIDEO_STOP_CONTROL));
    return trident_indexed_write(d, d->cur_vga_base, 0x91, 0);
}

static int video_clock(struct trident_device *d, u32 *now_ms);

/* V:390-415. Refresh the clock for this stop, never reuse an init deadline. */
int tri_video_stop(struct trident_device *d, bool shutdown)
{
    if (!d) return TRI_E_INVAL;
    d->video.clip_valid = false;
    if (shutdown) {
        if (d->video.status & TRI_VIDEO_ON) TRY(video_disable(d));
        if (d->video.allocation.allocated)
            TRY(tri_vram_free(d, &d->video.allocation));
        d->video.status = 0;
        d->video.polling = false;
    } else if (d->video.status & TRI_VIDEO_ON) {
        TRY(video_clock(d, &d->video.now_ms));
        d->video.status |= TRI_VIDEO_OFF_TIMER;
        d->video.off_time = d->video.now_ms + VIDEO_OFF_DELAY;
        d->video.polling = true;
    }
    return 0;
}

/* V:453-525. key is already packed in the selected framebuffer depth. */
int tri_video_set_key(struct trident_device *d, u32 key)
{
    u32 depth;
    if (!d) return TRI_E_INVAL;
    depth = video_depth(d);
    TRY(video_out(d, VIDEO_KEY_BASE, (u8)key));
    TRY(video_out(d, VIDEO_KEY_BASE + 1, depth == 8 ? 0 : (u8)(key >> 8)));
    TRY(video_out(d, VIDEO_KEY_BASE + 2, depth == 24 ? (u8)(key >> 16) : 0));
    d->video.color_key = key;
    d->video.clip_valid = false;
    return 0;
}

/* V:568-605. F5 implements reuse/resize/free/reallocate over its VRAM list.
 * X's purge has no OS32 cache counterpart. Failure disables only this use
 * of video; it is never returned through ScreenInit/Enter/mode setup. */
static int video_allocate(struct trident_device *d, struct tri_vram_block *b,
                          u32 bytes)
{
    return tri_vram_resize(d, b, TRI_VRAM_VIDEO, bytes,
                            VIDEO_ALIGN * d->mode.display.cpp);
}

static int video_pair(struct trident_device *d, u8 reg, u32 value)
{
    TRY(trident_indexed_write(d, d->cur_vga_base, reg, (u8)value));
    return trident_indexed_write(d, d->cur_vga_base, reg + 1, (u8)(value >> 8));
}

/* V:688-744, integer form of the reference's truncated float fractions. */
static int video_zoom(struct trident_device *d, u8 reg, u32 src, u32 dst)
{
    u32 q = 0, hi = 0;
    if (dst > src) {
        q = dst * 1024u / src - 1024u;
        hi = ((q >> 10) & 15u) << 2 | ((q >> 8) & 3u);
    } else if (dst < src) {
        q = dst * 1024u / src;
        hi = 0x80u | (((src / dst - 1) & 7u) << 2) | ((q >> 8) & 3u);
    }
    TRY(trident_indexed_write(d, d->cur_vga_base, reg, (u8)q));
    return trident_indexed_write(d, d->cur_vga_base, reg + 1, (u8)hi);
}

static int video_display(struct trident_device *d, u32 format, u32 off,
                         u32 width, i32 x1, const struct tri_rect *box,
                         u32 sw, u32 sh, u32 dw, u32 dh)
{
    bool yuv = format != TRI_VIDEO_RV16;
    TRY(trident_indexed_write(d, d->cur_vga_base, 0x8F, yuv ? 0x10 : 0x11));
    TRY(video_pair(d, 0x86, box->x1 + d->video.hsync));
    TRY(video_pair(d, 0x88, box->y1 + d->video.vsync - 2));
    TRY(video_pair(d, 0x8A, box->x2 + d->video.hsync));
    TRY(video_pair(d, 0x8C, box->y2 + d->video.vsync + 2));
    off += (x1 >> 15) & ~1;
    off = off >> 3; /* V:668-675: 9682 uses eight-byte address units. */
    TRY(video_pair(d, 0x90, width * 2));
    TRY(video_pair(d, 0x92, off));
    TRY(trident_indexed_write(d, d->cur_vga_base, 0x94, (off >> 16) & 7));
    TRY(video_zoom(d, 0x80, sw, dw));
    TRY(video_zoom(d, 0x82, sh, dh));
    TRY(trident_indexed_write(d, d->cur_vga_base, 0x95,
                              yuv ? width >> 2 : width >> 6));
    TRY(trident_indexed_write(d, d->cur_vga_base, 0x96,
                              yuv ? (width + 2) >> 2 : (width + 2) >> 6));
    TRY(trident_indexed_write(d, d->cur_vga_base, 0x8E, 0x94));
    return video_out(d, 0x00, 0x95);
}

static int video_fix_frame(struct trident_device *d);

/* The X region is the selected viewport at (0,0) in this driver. H supplies
 * integer pixel rectangles; convert to 16.16 before clipping either axis.
 * Bound input coordinates to X's short range before arithmetic. */
static int video_axis(i32 *s1, i32 *s2, i32 *d1, i32 *d2,
                       u32 image, u32 viewport)
{
    i32 scale, diff;
    if (*s1 < -32768 || *s2 > 32767 || *s2 <= *s1 ||
        *d1 < -32768 || *d2 > 32767 || *d2 <= *d1) return TRI_E_INVAL;
    /* X's source dimensions are at most the advertised image dimensions. */
    if (*s2 - *s1 > (i32)VIDEO_MAX) return TRI_E_INVAL;
    if (*s2 <= 0 || *s1 >= (i32)image || *d2 <= 0 || *d1 >= (i32)viewport)
        return 0;
    scale = ((*s2 - *s1) * VIDEO_FP) / (*d2 - *d1);
    if (!scale) return TRI_E_INVAL;
    *s1 *= VIDEO_FP;
    *s2 *= VIDEO_FP;
    if (*s1 < 0) {
        diff = (-*s1 + scale - 1) / scale;
        if (diff >= *d2 - *d1) return 0;
        *d1 += diff;
        *s1 += diff * scale;
    }
    if (*s2 > (i32)image * VIDEO_FP) {
        diff = (*s2 - (i32)image * VIDEO_FP + scale - 1) / scale;
        if (diff >= *d2 - *d1) return 0;
        *d2 -= diff;
        *s2 -= diff * scale;
    }
    if (*d1 < 0) {
        diff = -*d1;
        if (diff >= *d2 - *d1) return 0;
        *d1 = 0;
        *s1 += diff * scale;
    }
    if (*d2 > (i32)viewport) {
        diff = *d2 - viewport;
        if (diff >= *d2 - *d1) return 0;
        *d2 = viewport;
        *s2 -= diff * scale;
    }
    return *d1 < *d2 && *s1 < *s2;
}

static int video_clip(struct trident_device *d, u32 w, u32 h,
                       const struct tri_rect *src, const struct tri_rect *dst,
                       struct tri_rect *s, struct tri_rect *box)
{
    int rc;
    if (!d || !src || !dst || !d->mode.valid || !w || !h)
        return TRI_E_INVAL;
    *s = *src;
    *box = *dst;
    rc = video_axis(&s->x1, &s->x2, &box->x1, &box->x2,
                    w, d->mode.selected.hd);
    if (rc <= 0) return rc;
    return video_axis(&s->y1, &s->y2, &box->y1, &box->y2,
                       h, d->mode.selected.vd);
}

/* xf86XVFillKeyHelper equivalent for the single rectangular OS32 clip.
 * CPU stores also cover packed24/NoAccel; synchronize before touching LFB.
 * RAM fallback publishes the same rectangle using its byte present path. */
static int video_fill_key(struct trident_device *d, const struct tri_rect *box,
                           bool always)
{
    const struct tri_surface *fb = &d->mode.display;
    const struct tri_rect *old = &d->video.clip;
    u32 key = d->video.color_key;
    if (!always && d->video.clip_valid && old->x1 == box->x1 &&
        old->y1 == box->y1 && old->x2 == box->x2 && old->y2 == box->y2) return 0;
    TRY(tri_sync(d));
    for (i32 y = box->y1; y < box->y2; ++y) {
        for (i32 x = box->x1; x < box->x2; ++x) {
            u32 pixel = y * fb->pitch + x * fb->cpp;
            for (u32 b = 0; b < fb->cpp; ++b) {
                if (d->res->ram_fallback)
                    d->res->client[y * d->mode.client.pitch + x * fb->cpp + b] = key >> (b * 8);
                else
                    TRY(trident_write8(d, TRI_SPACE_LFB, fb->off + pixel + b, key >> (b * 8)));
            }
        }
    }
    if (d->res->ram_fallback)
        TRY(trident_present(d, box->x1, box->y1, box->x2 - box->x1, box->y2 - box->y1));
    TRY(tri_sync(d));
    d->video.clip = *box;
    d->video.clip_valid = true;
    return 0;
}

static void video_doublescan(struct trident_device *d, struct tri_rect *box, u32 *dh)
{
    if (d->mode.selected.flags & TRI_MODE_DOUBLESCAN) {
        box->y1 *= 2;
        box->y2 *= 2;
        *dh *= 2;
    }
}

/* V:779-899: clip -> allocate -> even copy -> key -> top -> FixFrame -> show. */
int tri_video_put(struct trident_device *d, const struct tri_video_image *image,
                  const struct tri_rect *src, const struct tri_rect *dst, u32 key)
{
    struct tri_video_image layout;
    struct tri_rect s, box;
    u32 pitch, top, left, pixels, lines, off, dh, dst_start;
    int rc;
    if (!d || !image || !image->data) return TRI_E_INVAL;
    layout = *image;
    TRY(tri_video_query(&layout));
    if (layout.width != image->width || layout.height != image->height ||
        image->bytes < layout.bytes) return TRI_E_INVAL;
    for (u32 p = 0; p < 3; ++p)
        if (image->pitch[p] != layout.pitch[p] || image->offset[p] != layout.offset[p])
            return TRI_E_INVAL;
    rc = video_clip(d, image->width, image->height, src, dst, &s, &box);
    if (rc <= 0) return rc;
    pitch = (image->width * 2 + VIDEO_ALIGN - 1) & ~(VIDEO_ALIGN - 1);
    TRY(video_allocate(d, &d->video.allocation, pitch * image->height));
    top = s.y1 >> 16;
    left = (s.x1 >> 16) & ~1u;
    pixels = ((((s.x2 + VIDEO_FP - 1) >> 16) + 1) & ~1u) - left;
    /* V:845-856: destination precedes YV12's source top even alignment. */
    dst_start = d->video.allocation.off + top * pitch + left * 2;
    if (image->format == TRI_VIDEO_YV12) {
        top &= ~1u;
        lines = ((((s.y2 + VIDEO_FP - 1) >> 16) + 1) & ~1u) - top;
    } else {
        lines = ((s.y2 + VIDEO_FP - 1) >> 16) - top;
    }
    TRY(tri_sync(d));
    for (u32 row = 0; row < lines; ++row) {
        u32 target = dst_start + row * pitch;
        const u8 *y = image->data + image->offset[0] + (top + row) * image->pitch[0];
        if (image->format == TRI_VIDEO_YV12) {
            const u8 *v = image->data + image->offset[1] + ((top + row) / 2) * image->pitch[1];
            const u8 *u = image->data + image->offset[2] + ((top + row) / 2) * image->pitch[2];
            for (u32 x = left; x < left + pixels; x += 2) {
                u32 packed = y[x] | ((u32)u[x / 2] << 8) |
                             ((u32)y[x + 1] << 16) | ((u32)v[x / 2] << 24);
                TRY(trident_write32(d, TRI_SPACE_LFB, target, packed));
                target += 4;
            }
        } else {
            for (u32 x = left * 2; x < (left + pixels) * 2; x += 4) {
                u32 packed = y[x] | ((u32)y[x + 1] << 8) |
                             ((u32)y[x + 2] << 16) | ((u32)y[x + 3] << 24);
                TRY(trident_write32(d, TRI_SPACE_LFB, target, packed));
                target += 4;
            }
        }
    }
    if (d->video.color_key != key) TRY(tri_video_set_key(d, key));
    TRY(video_fill_key(d, &box, false));
    off = d->video.allocation.off + top * pitch;
    dh = dst->y2 - dst->y1;
    video_doublescan(d, &box, &dh);
    TRY(video_fix_frame(d));
    TRY(video_display(d, image->format, off, image->width, s.x1, &box,
                      src->x2 - src->x1, src->y2 - src->y1, dst->x2 - dst->x1, dh));
    d->video.status = TRI_VIDEO_ON;
    d->video.polling = true;
    return 0;
}

/* V:907-940. Unused planes are zero so callers can reuse descriptors. */
int tri_video_query(struct tri_video_image *image)
{
    u32 uv;
    if (!image || !image->width || !image->height) return TRI_E_INVAL;
    if (image->format != TRI_VIDEO_RV16 && image->format != TRI_VIDEO_YV12 &&
        image->format != TRI_VIDEO_YUY2) return TRI_E_NOSYS;
    if (image->width > VIDEO_MAX) image->width = VIDEO_MAX;
    if (image->height > VIDEO_MAX) image->height = VIDEO_MAX;
    image->width = (image->width + 1) & ~1u;
    for (u32 p = 0; p < 3; ++p) image->pitch[p] = image->offset[p] = 0;
    if (image->format == TRI_VIDEO_YV12) {
        image->height = (image->height + 1) & ~1u;
        image->pitch[0] = (image->width + 3) & ~3u;
        image->pitch[1] = image->pitch[2] = ((image->width / 2) + 3) & ~3u;
        image->offset[1] = image->pitch[0] * image->height;
        uv = image->pitch[1] * (image->height / 2);
        image->offset[2] = image->offset[1] + uv;
        image->bytes = image->offset[2] + uv;
    } else {
        image->pitch[0] = image->width * 2;
        image->bytes = image->pitch[0] * image->height;
    }
    return 0;
}

/* V:953-1023. X advertises only Images[0] (RV16) for offscreen surfaces. */
int tri_video_surface_alloc(struct trident_device *d, struct tri_video_surface *s,
                            u32 format, u32 width, u32 height)
{
    u32 pitch;
    if (!d || !s || !d->mode.valid || !width || !height ||
        width > VIDEO_MAX || height > VIDEO_MAX) return TRI_E_INVAL;
    if (format != TRI_VIDEO_RV16) return TRI_E_NOSYS;
    if (s->allocation.allocated) return TRI_E_STATE;
    width = (width + 1) & ~1u;
    pitch = (width * 2 + VIDEO_ALIGN - 1) & ~(VIDEO_ALIGN - 1);
    TRY(video_allocate(d, &s->allocation, pitch * height));
    s->format = format;
    s->width = width;
    s->height = height;
    s->pitch = pitch;
    s->on = false;
    return 0;
}

int tri_video_surface_stop(struct trident_device *d, struct tri_video_surface *s)
{
    if (!d || !s) return TRI_E_INVAL;
    if (s->on) {
        TRY(video_disable(d));
        s->on = false;
    }
    return 0;
}

int tri_video_surface_free(struct trident_device *d, struct tri_video_surface *s)
{
    if (!d || !s) return TRI_E_INVAL;
    TRY(tri_video_surface_stop(d, s));
    if (s->allocation.allocated) TRY(tri_vram_free(d, &s->allocation));
    return 0;
}

/* V:1071-1129; §7 requires key -> Sync -> overlay also for surfaces. */
int tri_video_surface_display(struct trident_device *d, struct tri_video_surface *surface,
                              const struct tri_rect *src,
                              const struct tri_rect *dst, u32 key)
{
    struct tri_rect s, box;
    u32 dh, now_ms = 0;
    int rc;
    if (!d || !surface || !surface->allocation.allocated) return TRI_E_INVAL;
    rc = video_clip(d, surface->width, surface->height, src, dst, &s, &box);
    if (rc <= 0) return rc;
    /* Obtain this operation's clock before preempting the stream: a clock
     * failure must not leave its old OFF timer targeting the new surface. */
    if (d->video.status & TRI_VIDEO_ON) TRY(video_clock(d, &now_ms));
    if (d->video.color_key != key) TRY(tri_video_set_key(d, key));
    TRY(trident_video_reset(d));
    TRY(video_fill_key(d, &box, true));
    dh = dst->y2 - dst->y1;
    video_doublescan(d, &box, &dh);
    TRY(video_fix_frame(d));
    TRY(video_display(d, surface->format, surface->allocation.off, surface->width,
                      s.x1, &box, src->x2 - src->x1, src->y2 - src->y1,
                      dst->x2 - dst->x1, dh));
    surface->on = true;
    if (d->video.status & TRI_VIDEO_ON) {
        d->video.now_ms = now_ms;
        d->video.clip_valid = false;
        d->video.status = TRI_VIDEO_FREE_TIMER;
        d->video.free_time = d->video.now_ms + VIDEO_FREE_DELAY;
        d->video.polling = true;
    }
    return 0;
}

/* V:1153-1178: strict deadline < now, OFF then FREE on later polls. */
void tri_video_tick(struct trident_device *d, u32 now_ms)
{
    int rc;
    if (!d) return;
    d->video.now_ms = now_ms;
    if (!d->video.polling) return;
    if (d->video.status & TRI_VIDEO_OFF_TIMER) {
        if (d->video.off_time < now_ms) {
            rc = video_disable(d);
            if (rc) { d->video.last_rc = rc; return; }
            d->video.status = TRI_VIDEO_FREE_TIMER;
            d->video.free_time = now_ms + VIDEO_FREE_DELAY;
        }
    } else if (d->video.status & TRI_VIDEO_FREE_TIMER) {
        if (d->video.free_time < now_ms) {
            if (d->video.allocation.allocated) {
                rc = tri_vram_free(d, &d->video.allocation);
                if (rc) { d->video.last_rc = rc; return; }
            }
            d->video.status = 0;
            d->video.polling = false;
        }
    } else {
        d->video.polling = false;
    }
}

/* Divide the 64-bit monotonic microseconds by 1000 without 64-bit division
 * or libgcc. Keep the low 32 quotient bits (the timer's millisecond clock). */
static int video_clock(struct trident_device *d, u32 *now_ms)
{
    u32 lo, hi, remainder = 0, ms = 0;
    int rc;
    if (!d) return TRI_E_INVAL;
    rc = sys_time_now(&lo, &hi);
    if (rc) { d->video.last_rc = rc; return rc; }
    for (int bit = 63; bit >= 0; --bit) {
        u32 value = bit >= 32 ? (hi >> (bit - 32)) & 1u : (lo >> bit) & 1u;
        remainder = remainder * 2 + value;
        ms <<= 1;
        if (remainder >= VIDEO_US_PER_MS) {
            remainder -= VIDEO_US_PER_MS;
            ms |= 1u;
        }
    }
    *now_ms = ms;
    return 0;
}

int tri_video_poll(struct trident_device *d)
{
    u32 now_ms;
    if (!d) return TRI_E_INVAL;
    d->video.last_rc = 0;
    TRY(video_clock(d, &now_ms));
    tri_video_tick(d, now_ms);
    return d->video.last_rc;
}

/* V:1186-1276. Read the raw timing bytes, including shadow switching from
 * R:445-456. 9682 has no Cyber LCD centering/lcdMode branch. */
static int video_fix_frame(struct trident_device *d)
{
    u8 control, shadow = 0, cr[6], hcenter, vcenter;
    static const u8 index[] = {0x00, 0x04, 0x05, 0x06, 0x07, 0x10};
    i32 ht, hs, vt, vs;
    bool is_shadow;
    int rc = 0, restore_rc = 0;
    if (!d->video.hsync && !d->video.vsync && !d->video.fix_frame)
        d->video.fix_frame = VIDEO_FIX_PERIOD;
    if (d->video.fix_frame++ < VIDEO_FIX_PERIOD) return 0;
    TRY(trident_indexed_read(d, TRI_GR_INDEX, 0x30, &control));
    is_shadow = (control & 0x81) == 0x81;
    if (is_shadow) {
        TRY(trident_indexed_read(d, TRI_GR_INDEX, 0x30, &shadow));
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_GR_INDEX + 1, shadow | 0x40));
    }
    for (u32 i = 0; i < sizeof(index); ++i) {
        rc = trident_indexed_read(d, d->cur_vga_base, index[i], &cr[i]);
        if (rc) break;
    }
    if (!rc) rc = trident_indexed_read(d, TRI_GR_INDEX, 0x53, &hcenter);
    if (!rc) rc = trident_indexed_read(d, TRI_GR_INDEX, 0x52, &vcenter);
    if (is_shadow) {
        restore_rc = trident_write8(d, TRI_SPACE_REGS, TRI_GR_INDEX, 0x30);
        if (!restore_rc)
            restore_rc = trident_write8(d, TRI_SPACE_REGS, TRI_GR_INDEX + 1, shadow);
    }
    if (rc || restore_rc) {
        d->video.fix_frame = VIDEO_FIX_PERIOD;
        if (restore_rc) d->video.last_rc = restore_rc;
        return rc ? rc : restore_rc;
    }
    ht = cr[0] << 3;
    hs = (cr[1] + ((cr[2] >> 5) & 3)) << 3;
    vt = cr[3] | ((cr[4] & 1) << 8) | ((cr[4] & 0x20) << 4);
    vs = cr[5] | ((cr[4] & 4) << 6) | ((cr[4] & 0x80) << 2);
    d->video.hsync = ht - hs + 23 + 7 + d->settings.hsync_skew;
    d->video.vsync = vt - vs - 2 + d->settings.vsync_skew;
    d->video.fix_frame = 0;
    return 0;
}
