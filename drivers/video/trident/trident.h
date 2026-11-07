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
/* Internal contract: TRIDENT_FULL_DESIGN §§2–8, F0 (9682 / Ra266 only).
 * The S3 register/save/resource prefix remains compatible.
 * Register sequences: MIT X.Org trident_driver-1.4.0.c:616–775,
 * 940–1136,1201–1247,2919–2975,3343–3348,3471–3481 and
 * userland/tests/hw/trident_mode.hwb. Manufacturer sync reference:
 * NEC LCD172VXM/192VXM manual p.20; 9682 register manual unavailable.
 * No public KAPI/SDK ABI. All int operations return 0 or a negative rc.
 */
#ifndef TRIDENT_H
#define TRIDENT_H
#include <stdbool.h>
#include "types.h"
#include "memmap.h"
#include "gfx/palette.h"

#define TRI_PCI_VENDOR 0x1023u
#define TRI_PCI_DEVICE 0x9660u
#define TRI_CHIP_9682 0x10u
#define TRI_PCI_BUS 0u
#define TRI_PCI_SLOT 8u
#define TRI_PCI_FUNCTION 0u
#define TRI_PCI_COMMAND 0x0002u
/* Device windows are identity mapped; memmap.h owns their addresses. */
#define TRI_BAR0_PHYS MEM_TRIDENT_LFB_VA
#define TRI_BAR1_PHYS MEM_TRIDENT_REGS_VA
#define TRI_BAR0_DECODE_BYTES 0x00400000u
#define TRI_BAR1_DECODE_BYTES 0x00010000u
#define TRI_REG_MAPPED_BYTES 0x00010000u /* D:1609-1635 MMIO aperture */
#define TRI_WIDTH 640u
#define TRI_HEIGHT 480u
#define TRI_PITCH 640u
#define TRI_DISPLAY_BYTES 307200u
#define TRI_DISPLAY_PAGES 75u
#define TRI_PAGE_BYTES 4096u
#define TRI_DAC_COLORS 256u
#define TRI_DAC_BYTES 768u
#define TRI_SAVED_BYTES 852u
#define TRI_SAVE_MCLK 0u /* independent of Init's 80MHz writes */
#define TRI_SR_INDEX 0x3C4u
#define TRI_GR_INDEX 0x3CEu
#define TRI_CR_COLOR 0x3D4u
#define TRI_CR_MONO 0x3B4u
#define TRI_AR_INDEX 0x3C0u
#define TRI_AR_DATA 0x3C1u
#define TRI_MISC_READ 0x3CCu
#define TRI_MISC_WRITE 0x3C2u
#define TRI_DAC_MASK 0x3C6u
#define TRI_DAC_READ_INDEX 0x3C7u
#define TRI_DAC_WRITE_INDEX 0x3C8u
#define TRI_DAC_DATA 0x3C9u
#define TRI_MCLK_LO 0x43C6u
#define TRI_MCLK_HI 0x43C7u
#define TRI_PLL_LO 0x43C8u
#define TRI_PLL_HI 0x43C9u
#define TRI_EXT_DAC_DATA 0x83C6u
#define TRI_EXT_DAC_INDEX 0x83C8u
#define TRI_PORT_MODE 0x68u
#define TRI_PORT_FF 0x6Au
#define TRI_PORT_ROUTE 0xFACu

/* Flat save slots follow the script, including the unused MCLK reservation. */
enum trident_save_slot {
    TRI_SAVE_DAC = 0, TRI_SAVE_MISC = 768, TRI_SAVE_CR = 769,
    TRI_SAVE_AR = 794, TRI_SAVE_GR = 815, TRI_SAVE_SR = 824,
    TRI_SAVE_SR0E = 828, TRI_SAVE_CR13 = 829, TRI_SAVE_CR21 = 830,
    TRI_SAVE_CR1E = 831, TRI_SAVE_CR27 = 832, TRI_SAVE_CR2B = 833,
    TRI_SAVE_CR2F = 834, TRI_SAVE_CR2A = 835, TRI_SAVE_CR3A = 836,
    TRI_SAVE_CR29 = 837, TRI_SAVE_CR38 = 838, TRI_SAVE_CR36 = 839,
    TRI_SAVE_CR39 = 840, TRI_SAVE_CR55 = 841, TRI_SAVE_CR50 = 842,
    TRI_SAVE_GR0F = 843, TRI_SAVE_GR2F = 844, TRI_SAVE_HIDDEN_DAC = 845,
    TRI_SAVE_CLOCK_MISC = 846, TRI_SAVE_PLL_LO = 847, TRI_SAVE_PLL_HI = 848,
    TRI_SAVE_MCLK_LO = 849, TRI_SAVE_MCLK_HI = 850, TRI_SAVE_SR0D = 851
};
enum trident_rc {
    TRI_OK = 0, TRI_E_INVAL = -1, TRI_E_DEVICE = -2,
    TRI_E_RESOURCE = -3, TRI_E_MAP = -4, TRI_E_SURFACE = -5,
    TRI_E_ACCESS = -6, TRI_E_RESTORE = -7, TRI_E_CAP_TOO_SMALL = -8,
    TRI_E_BUSY = -9, TRI_E_STATE = -10, TRI_E_MODULE = -11,
    TRI_E_NOSYS = -12 /* unsupported image/operation: HAL maps to NOSYS */
};
/* step = last completed stage, failed_step/last_rc = first failed operation.
 * Lifecycle owns these fields; primitive functions return rc without clearing
 * an earlier error. restore_rc/disable_rc retain both recovery outcomes. */
enum trident_step {
    TRI_STEP_NONE = 0, TRI_STEP_RESOURCES = 1, TRI_STEP_PIN = 2,
    TRI_STEP_ARM = 3, TRI_STEP_IDENTIFY = 4, TRI_STEP_SAVE = 5,
    TRI_STEP_PC98_INIT = 6, TRI_STEP_CAPACITY_PLAN = 7, TRI_STEP_MODE = 8,
    TRI_STEP_ENABLE = 9, TRI_STEP_CLIENT = 11,
    TRI_STEP_ACTIVE = 12, TRI_STEP_RESTORE = 13, TRI_STEP_LOCK = 14,
    TRI_STEP_DISABLE = 15, TRI_STEP_CLOSED = 16
};
enum trident_upper { TRI_UPPER_UNKNOWN = 0, TRI_UPPER_MIRROR = 1,
                     TRI_UPPER_INDEPENDENT = 2 };

/* Driver-private format IDs; F10 translates to HAL IDs (including H's new
 * RGB555/packed24 IDs). Never publish these numbers through KAPI. */
enum tri_format {
    TRI_FMT_INDEX8, TRI_FMT_RGB555, TRI_FMT_RGB565,
    TRI_FMT_PACKED24, TRI_FMT_XRGB8888, TRI_FORMAT_COUNT
};
struct tri_surface { u32 off, bytes, pitch; u16 width, height; u8 cpp, format; };
/* Dimensions are pixels, pitch/offset/bytes are bytes. off is BAR0-relative.
 * F5 validates format/pitch/capacity at creation; F10 clips before dispatch.
 * GE surfaces share cpp/pitch, occupy complete rows, and end at y<=2047. */
struct tri_mode {
    u32 id, dot_khz;
    u16 hd, hs, he, ht, vd, vs, ve, vt;
    u16 display_width;
    u8 depth, bpp;
    u32 flags;
};
enum tri_mode_flag {
    TRI_MODE_NHSYNC = 1u << 0, TRI_MODE_NVSYNC = 1u << 1,
    TRI_MODE_DOUBLESCAN = 1u << 2, TRI_MODE_NOACCEL = 1u << 3
};
/* F3 owns candidates, F10 filters them by capacity and builds the HAL table.
 * ID = 1 + timing_index * TRI_FORMAT_COUNT + format; timings in design §5
 * order: 480, 400, 350 lines. id=0 is never a driver mode. */
#define TRI_TIMING_COUNT 3u
#define TRI_MODE_COUNT (TRI_TIMING_COUNT * TRI_FORMAT_COUNT)
#define TRI_MODE_DEFAULT_ID 1u
extern const struct tri_mode tri_modes[TRI_MODE_COUNT];

/* A:97-181; C:1143-1214; V:42-49,182-204,588-602,907-940.
 * These are maximum capabilities, intersected with settings, MUX and actual
 * offscreen reservations. NO_PLANEMASK; no HW clip/mono expand/color pattern.
 * F5 defines tri_format_caps using this frozen initializer. */
enum tri_feature {
    TRI_CAP_FILL = 1u << 0, TRI_CAP_COPY = 1u << 1,
    TRI_CAP_LINE = 1u << 2, TRI_CAP_DASH = 1u << 3,
    TRI_CAP_MONO_PATTERN = 1u << 4, TRI_CAP_CURSOR = 1u << 5,
    TRI_CAP_VIDEO = 1u << 6
};
#define TRI_CAP_GE (TRI_CAP_FILL | TRI_CAP_COPY | TRI_CAP_LINE | \
                    TRI_CAP_DASH | TRI_CAP_MONO_PATTERN)
struct tri_format_cap { u8 format, depth, bpp, cpp; u32 features; };
#define TRI_FORMAT_CAPS_INIT { \
    {TRI_FMT_INDEX8, 8, 8, 1, TRI_CAP_GE | TRI_CAP_CURSOR | TRI_CAP_VIDEO}, \
    {TRI_FMT_RGB555, 15, 16, 2, TRI_CAP_GE | TRI_CAP_CURSOR | TRI_CAP_VIDEO}, \
    {TRI_FMT_RGB565, 16, 16, 2, TRI_CAP_GE | TRI_CAP_CURSOR | TRI_CAP_VIDEO}, \
    {TRI_FMT_PACKED24, 24, 24, 3, TRI_CAP_CURSOR | TRI_CAP_VIDEO}, \
    {TRI_FMT_XRGB8888, 24, 32, 4, TRI_CAP_GE | TRI_CAP_CURSOR | TRI_CAP_VIDEO} \
}
extern const struct tri_format_cap tri_format_caps[TRI_FORMAT_COUNT];
#define TRI_GE_MAX_Y 2047u             /* D:3637-3661 */
#define TRI_PATTERN_ALIGN 64u          /* R:419-420 */
#define TRI_PATTERN_BYTES 8u           /* A:173-180, mono 8 rows */
#define TRI_CURSOR_WIDTH 64u           /* C:1143-1214 */
#define TRI_CURSOR_HEIGHT 64u
#define TRI_CURSOR_ALIGN 1024u
#define TRI_CURSOR_IMAGE_BYTES 1024u   /* C:1107-1122, not reservation size */
#define TRI_CURSOR_RESERVE_BYTES (TRI_CURSOR_WIDTH * 4u * TRI_CURSOR_HEIGHT)
#define TRI_CURSOR_OFFSET_LIMIT (TRI_BAR0_DECODE_BYTES - TRI_PAGE_BYTES)
#define TRI_VIDEO_ALIGN_PIXELS 16u     /* V:588-602, allocation units */
#define TRI_VIDEO_MAX_WIDTH 1024u      /* V:907-940 */
#define TRI_VIDEO_MAX_HEIGHT 1024u
#define TRI_VIDEO_OFF_DELAY_MS 800u    /* V:42-43 */
#define TRI_VIDEO_FREE_DELAY_MS 60000u
#define TRI_MUX_THRESHOLD_KHZ 90000u   /* D:2179 */

/* F5 owns an intrusive list of caller-owned allocation records, not a new
 * fixed-size heap. Records live until free; no physical address input.
 * 2D/cursor allocations are complete rows, linear video alignment is in
 * pixels converted with the selected cpp. Free never frees the record. */
enum tri_vram_kind { TRI_VRAM_2D, TRI_VRAM_PATTERN, TRI_VRAM_CURSOR, TRI_VRAM_VIDEO };
struct tri_vram_block {
    struct tri_vram_block *next;
    u32 off, bytes;
    enum tri_vram_kind kind;
    bool allocated;
};
struct tri_vram_state {
    struct tri_vram_block *blocks;
    u32 usable_bytes, pitch, first_off, area2d_end;
};
/* F9 initializes settings before ModeInit; zero means reference defaults.
 * ram_fallback is a BOOT-WIDE configuration, never a per-mode choice.
 * mux_threshold_khz=0 selects 90000; skews are D:2179-2195 Xv adjustments. */
struct tri_settings {
    bool no_accel, sw_cursor, ram_fallback;
    u32 mux_threshold_khz, vram_bytes; /* 0: no additional capacity cap */
    i32 hsync_skew, vsync_skew;
};
struct tri_mode_state {
    struct tri_mode selected;
    struct tri_surface display, client;
    u32 generation, features;
    bool valid, mux, no_accel;
};
/* F1 owns the reference EngineOperation/DrawFlag/BltScanDirection state.
 * F10 supplies directions (+1/-1) and line setup before the matching op.
 * Sync writes engine_operation; ClearSync only polls, preserving DST_ENABLE.
 * hw_ops counts commands; io_accesses counts actual MMIO (F1/F10 share it). */
struct tri_accel_state {
    u16 engine_operation;
    u32 draw_flag, blt_scan_direction, pattern_location;
    u32 line_fg;
    i32 line_bg;
    u8 line_rop, dash_length;
    bool clipping, initialized;
    u32 hw_ops, io_accesses;
    struct tri_vram_block pattern;
};
/* F2 owns conversion scratch and reservation; image bytes are source/mask
 * swapped, MSB first, interleaved in 32-bit units (C:1199-1201).
 * HAL coordinates have hotspot subtracted before tri_cursor_move. */
struct tri_cursor_state {
    struct tri_vram_block allocation;
    u8 image[TRI_CURSOR_IMAGE_BYTES];
    u32 bg, fg;
    bool available;
};

/* Rectangles are half-open integer pixel bounds. F4 converts src to 16.16
 * during clipping; callers do not pass fixed-point values here. */
struct tri_rect { i32 x1, y1, x2, y2; };
#define TRI_VIDEO_RV16 0x36315652u /* V:186, FOURCC RV16 (RGB565) */
#define TRI_VIDEO_YV12 0x32315659u /* V:203, Y/V/U planar */
#define TRI_VIDEO_YUY2 0x32595559u /* V:204, packed YUYV */
struct tri_video_image {
    u32 format, width, height, pitch[3], offset[3], bytes;
    const u8 *data;
};
enum tri_video_status {
    TRI_VIDEO_OFF_TIMER = 0x01, TRI_VIDEO_FREE_TIMER = 0x02,
    TRI_VIDEO_ON = 0x04 /* V:45-49 */
};
/* F4 owns streaming allocation/clip/key/timers/FixFrame, corresponding to
 * V:207-219 and X/trident.h:178-183. Tick's void ABI retains I/O failure in
 * last_rc; poll returns it, never silently discards it. No IRQ timer. */
struct tri_video_state {
    struct tri_vram_block allocation;
    struct tri_rect clip;
    u32 color_key, status, off_time, free_time, now_ms;
    i32 fix_frame, hsync, vsync;
    int last_rc;
    bool clip_valid, polling;
};
/* V:953-1150 offscreen surface: caller-owned descriptor, one allocation per
 * surface; no arbitrary slot limit. format is a TRI_VIDEO_* FOURCC. */
struct tri_video_surface {
    struct tri_vram_block allocation;
    u32 format, width, height, pitch;
    bool on;
};
struct GfxCursorImage;
/* Byte/word/dword callbacks are distinct so OUTW never becomes two OUTBs.
 * Offsets are relative to the resource, except PORT8 uses a real I/O port.
 * Failed reads leave *value unchanged. No retries, readback or waiting.
 * regs.c owns the production implementation, fake uses exactly this seam.
 * All product register/LFB/port accesses, including present, use this seam. */
enum trident_space { TRI_SPACE_REGS = 0, TRI_SPACE_LFB = 1 };
struct trident_io {
    int (*read8)(void *, enum trident_space, u32, u8 *);
    int (*read16)(void *, enum trident_space, u32, u16 *);
    int (*read32)(void *, enum trident_space, u32, u32 *);
    int (*write8)(void *, enum trident_space, u32, u8);
    int (*write16)(void *, enum trident_space, u32, u16);
    int (*write32)(void *, enum trident_space, u32, u32);
    int (*port8)(void *, u16, u8);
    /* One LFB row span: aligned offset/source, count in dwords; validate the
     * whole range before writing. No readback. Scalar write8 handles edges. */
    int (*write_span32)(void *, u32 off, const u32 *src, u32 count);
};
struct trident_pci_info {
    u16 vendor, device, command;
    u8 bus, slot, function, revision;
    u32 bar0, bar1; /* raw config values, preserve type bits for validation */
};
struct trident_saved { u8 value[TRI_SAVED_BYTES]; u16 vga_base; bool valid; };
struct trident_resources {
    /* Kernel owns reservations, maps and surfaces; module borrows forever
     * after arm. owner is DEVICE reservation owner, not exec display owner.
     * F6 updates client/surface descriptors on mode changes after lease
     * revocation; BAR mappings/owner remain resident. */
    volatile u8 *regs, *lfb;
    u8 *client;
    u32 owner, bar0_rid, bar1_rid, decode_bytes, mapped_bytes;
    u32 display_sid, client_sid, client_bytes;
    u32 bar0_phys, bar1_phys, regs_bytes;
    struct trident_pci_info pci;
    bool ready;
    /* F6 publishes actual byte pitches and same-range DISPLAY/CLIENT alias.
     * Old prefix and legacy S3 constants are retained for existing callers. */
    struct tri_surface display, client_surface;
    u32 generation;
    bool ram_fallback;
    /* Core F6 implementation, called by F9/F10 through this borrowed callback
     * (no new module import). Revokes leases, replaces both ledger surfaces,
     * updates descriptors/generation; never programs hardware. F13 validates
     * the resulting bb/mode consistency, not this kernel text as module text.
     * mode is prepared/validated by F3/F5 before this call. */
    int (*configure)(const struct tri_mode *mode);
};
struct trident_device {
    const struct trident_resources *res;
    struct trident_saved saved;
    u32 declared_bytes, step;
    int last_rc, restore_rc, disable_rc;
    bool init_done, preinit_done, active, returning, armed, leave_done, closed;
    /* PreInit caches identification/SPR. ScreenInit always saves and programs
     * hardware. active tracks D's vtSema, set during ModeInit. */
    const struct trident_io *io;
    void *io_opaque;
    u32 failed_step, display_owner; /* arm-time diagnostic only, never a filter */
    u8 chip, spr, reference; /* identify owns chip; capacity_plan owns spr;
                             * mode_640 caches IsClearTV on first ModeInit after PC98 Init:
                             * 0=unset, 1=NTSC, 0x81=PAL; persists across reentry. */
    bool pinned, declared_unknown;
    bool screen_ready;
    /* vgaHW IOBase (index port): Save selects saved MISC; VGA mode restore
     * selects color; state restore selects saved MISC again. PC98 Init
     * deliberately retains Save's selection even after changing MISC. */
    u16 cur_vga_base;
    /* F0 additions: existing fields keep their offsets. Each named component
     * owns its state; lifecycle orchestrates without duplicating ownership. */
    struct tri_settings settings;
    struct tri_mode_state mode;
    struct tri_vram_state vram;
    struct tri_accel_state accel;
    struct tri_cursor_state cursor;
    struct tri_video_state video;
    /* PM 決定 (2026-11-07、F2 の設計の欠け): 今の 256 色の DAC の値 (6 bit、
     * R,G,B)。持ち主は F8 (palette.c)。DAC へ書く経路 (trident_palette と
     * lifecycle の既定の palette の load) は全部 tri_palette_record を通して
     * ここを更新する。F2 は INDEX8 の近似色の選択でここを読むだけ (I/O なし)。 */
    u8 dac[256][3];
    /* PM 決定 (2026-11-07、F8 の overscan): 今の overscan の index。持ち主は F8。
     * xf86cmap.c の pColPriv->overscan (未設定 = -1) に当たり、overscan_valid=false
     * が未設定 (ゼロ初期化で未設定になる)。 */
    u8 overscan;
    bool overscan_valid;
};

/* F3: prepare constructs the register/PLL inputs without programming ModeInit;
 * set programs the supplied mode, commits d->mode, and excludes route Enable
 * (as the 640 entry does). F9/F10 perform Enable, surfaces/palette/HAL update.
 * prepare selects display_width (1024 accel / 640 NoAccel for 640 pixels),
 * validates clock/timing and capacity through F5, and reports selected pitch
 * via the prepared mode's display_width*bpp/8. No float in kernel code. */
int tri_mode_prepare(struct trident_device *, const struct tri_mode *candidate,
                     struct tri_mode *prepared);
int tri_mode_set(struct trident_device *, const struct tri_mode *);
/* F5: validate is pure, init resets allocation bookkeeping only after all
 * old consumers have released their records. Usable capacity is the minimum
 * of SPR, configured capacity and mapped range, never an inferred VRAM probe.
 * alloc leaves the record unchanged on failure; resize may free/reallocate
 * per V:568-605. F2/F4 own records; F1 owns the pattern reservation below. */
int tri_surface_validate(struct trident_device *, const struct tri_surface *);
int tri_vram_init(struct trident_device *, const struct tri_mode *);
int tri_vram_alloc(struct trident_device *, struct tri_vram_block *,
                   enum tri_vram_kind, u32 bytes, u32 align_bytes);
int tri_vram_resize(struct trident_device *, struct tri_vram_block *,
                    enum tri_vram_kind, u32 bytes, u32 align_bytes);
int tri_vram_free(struct trident_device *, struct tri_vram_block *);

/* F1 implementations; F9 calls init at ScreenInit and EnterVT. F10/H and
 * diagnostics invoke operations with clipped, validated surfaces. ROP inputs
 * are GX logical indices 0..15, not already encoded ROP3 bytes (A:280-561).
 * Sync/ClearSync are no-ops in a NoAccel mode with no initialized engine.
 * F10 sets copy direction from overlap before tri_copy; no implicit MMIO in
 * direction/line_setup. line_setup supplies the fields absent from §3's
 * tri_line signature: dash_length=0 solid, otherwise 2/4/8/16; bg=-1
 * transparent. tri_line dash contains length low bits, phase is a bit index.
 * Octant uses XAA bits: bit0 Y-major, bit1 X-decreasing, bit2 Y-decreasing.
 * Each operation performs its own final Sync/ClearSync before returning. */
int tri_accel_init(struct trident_device *, const struct tri_mode *);
int tri_sync(struct trident_device *);
int tri_clear_sync(struct trident_device *);
int tri_copy_direction(struct trident_device *, int xdir, int ydir);
int tri_line_setup(struct trident_device *, u32 fg, i32 bg, u8 rop,
                   u8 dash_length);
int tri_fill(struct trident_device *, const struct tri_surface *,
             int x, int y, int w, int h, u32 color, u8 rop);
int tri_copy(struct trident_device *, const struct tri_surface *src,
             const struct tri_surface *dst, int sx, int sy, int dx, int dy,
             int w, int h, u8 rop, int transparent, u32 key);
int tri_pattern(struct trident_device *, const struct tri_surface *,
                u32 pattern_off, int x, int y, int w, int h,
                u32 fg, i32 bg, u8 rop);
int tri_line(struct trident_device *, const struct tri_surface *, int x, int y,
             int dmaj, int dmin, int error, int len, u8 octant,
             u16 dash, u8 phase);
int tri_pattern_upload(struct trident_device *, const u8 rows[8], u32 *off);

/* F2: init reserves C:1143-1214's area without an invented initial Hide.
 * convert is the X cursor-framework equivalent, with no I/O; it fills image
 * scratch and depth-packed bg/fg using the current palette for INDEX8.
 * F10 calls convert -> colors -> image; image uploads prepared scratch after
 * Sync. The GfxCursorImage argument is retained exactly as in design §4.
 * Unsupported size/alpha/colors or MUX+8 returns TRI_E_NOSYS for SW fallback. */
int tri_cursor_init(struct trident_device *);
int tri_cursor_convert(struct trident_device *, const struct GfxCursorImage *);
int tri_cursor_image(struct trident_device *, const struct GfxCursorImage *);
int tri_cursor_colors(struct trident_device *, u32 bg, u32 fg);
int tri_cursor_move(struct trident_device *, i32 x, i32 y);
int tri_cursor_show(struct trident_device *, bool show);

/* F4 owns every video function. query rounds dimensions and returns pitch,
 * plane offsets and total bytes without touching data or hardware. put/surface
 * display perform key fill -> Sync -> overlay; F10/H supplies the integer
 * viewport clip. set_key invalidates clip, without a brightness/etc interface.
 * poll obtains sys_time_now through F14's generated import, converts us to ms,
 * then calls tick; F16 exports poll to F17/H's cooperative loop. Leave/Close
 * stop polling and call stop(shutdown=true). No present-driven timer. */
int tri_video_query(struct tri_video_image *);
int tri_video_put(struct trident_device *, const struct tri_video_image *,
                  const struct tri_rect *src, const struct tri_rect *dst, u32 key);
int tri_video_set_key(struct trident_device *, u32 key);
int tri_video_stop(struct trident_device *, bool shutdown);
void tri_video_tick(struct trident_device *, u32 now_ms);
int tri_video_poll(struct trident_device *);
int tri_video_surface_alloc(struct trident_device *, struct tri_video_surface *,
                            u32 format, u32 width, u32 height);
int tri_video_surface_display(struct trident_device *, struct tri_video_surface *,
                              const struct tri_rect *src,
                              const struct tri_rect *dst, u32 key);
int tri_video_surface_stop(struct trident_device *, struct tri_video_surface *);
int tri_video_surface_free(struct trident_device *, struct tri_video_surface *);
/* F8, C:1245-1282; F9/F10 call after selected mode/palette changes. */
int tri_overscan(struct trident_device *, u8 index);
/* C:1218-1242 DDC1 callback only, NOT an EDID acquisition gate. The design
 * owner is F19 (ddc1.c, PM decision 2026-11-07). Read result is bit0 (0/1). */
int tri_ddc1_read(struct trident_device *, u8 *bit);

/* F10 owns these adapters (existing HAL signatures), translating failures
 * to HAL rc, head0 only. F16 wraps device-taking primitives above in its
 * diagnostic ops; that table/context version is owned by trident_module.h. */
int tri_hal_fill(int x, int y, int w, int h, u8 color);
int tri_hal_blit(int dx, int dy, int sx, int sy, int w, int h);
int tri_hal_mode(u32 id);
int tri_hal_dpms(u8 head, u32 state);
int tri_hal_cursor_image(u8 head, const struct GfxCursorImage *);
int tri_hal_cursor_move(u8 head, i32 x, i32 y);
int tri_hal_cursor_show(u8 head, int show);

/* Host foundation: keep the existing trident_io seam, no host-only callbacks
 * in product state. tools/tests/trident_device_fake.h owns reset/bind/fail
 * (one-based operation), trace and independent register/reference images.
 * Component tests select real F1/F2/F3/F4/F5 or link-time sibling stubs;
 * stub signatures MUST be the declarations here. PLL float/reference fixtures
 * remain host-side. No new shared host API is required by this declaration. */

/* probe.c: config-only validation before arm; indexed identify after arm. */
int trident_pci_validate(const struct trident_pci_info *pci);
int trident_identify(struct trident_device *d);
/* resources.c (core): master CR3/live AS=0; no indexed access/BAR writes.
 * Decode reservations survive rollback. Before arm roll back surfaces/client
 * and successful maps; after arm rollback returns BUSY. ready only on commit. */
int trident_boot_reserve(void);
const struct trident_resources *trident_resources_get(void);
int trident_resources_rollback(bool armed);
/* regs.c: bind is write-free; these wrappers keep exact width and propagate rc.
 * AR/protect use vga_base (3B4/3D4); status port = vga_base+6.
 * indexed_write is one 16-bit OUTW, indexed_read selects with one OUTB.
 * vgaHW operations (protect/CRTC lock and D:636/643) use two 8bit writes,
 * never indexed_write. Base selection follows vgaHW.c:1786–1789,1021–1025. */
int trident_regs_bind(struct trident_device *d);
int trident_read8(struct trident_device *, enum trident_space, u32, u8 *);
int trident_read16(struct trident_device *, enum trident_space, u32, u16 *);
int trident_read32(struct trident_device *, enum trident_space, u32, u32 *);
int trident_write8(struct trident_device *, enum trident_space, u32, u8);
int trident_write16(struct trident_device *, enum trident_space, u32, u16);
int trident_write32(struct trident_device *, enum trident_space, u32, u32);
int trident_port8(struct trident_device *, u16, u8);
int trident_indexed_read(struct trident_device *, u16 index_port, u8 index, u8 *);
int trident_indexed_write(struct trident_device *, u16 index_port, u8 index, u8);
int trident_ar_read(struct trident_device *, u16 vga_base, u8 index, u8 *);
int trident_ar_write(struct trident_device *, u16 vga_base, u8 index, u8);
int trident_hidden_dac_read(struct trident_device *, u8 *);
int trident_hidden_dac_write(struct trident_device *, u8);
/* H:493: 83C8=4, then read-modify-write 83C6. */
int trident_ext_dac_update(struct trident_device *, u8 mask, u8 bits);
int trident_protect(struct trident_device *, u16 vga_base, bool on);
int trident_crtc_lock(struct trident_device *, u16 vga_base, bool lock);
/* pc98_init.c: full D:616–692, including protect and SR0C alternate branch.
 * Protect and unlock/lock use the Save-time base, as in the reference.
 * The script fixes these at 3D4 (same when boot MISC bit0=1).
 * Explicit AR and Trident CR accesses stay at 3D4. */
int trident_pc98_init(struct trident_device *d);
/* state_save.c/default_dac.c/state_restore.c: 852 slots, nonprimary,
 * no font/text planes, default MCLK not saved, SR0E restore XOR 02.
 * Save selects base from MISC at save time (vgaHW.c:1021–1025). Restore
 * entry protect/Trident uses retained IOBase; VGA Restore writes saved MISC
 * then uses its base for AR/CRTC and exit protect (vgaHW.c:845–865;
 * trident_dac.c:781–810, D:1111–1136). */
int trident_state_save(struct trident_device *d);
extern const u8 trident_default_dac[TRI_DAC_BYTES];
int trident_state_restore(struct trident_device *d);
/* PreInit SPR declaration, D:2920-2969; no VRAM write/read test. */
int trident_capacity_plan(struct trident_device *d);
/* D:3490-3491,1182-1190; X/trident_video.c:225-270,1332-1343. */
int trident_screen_blank(struct trident_device *, bool blank);
int trident_adjust_frame(struct trident_device *);
int trident_video_reset(struct trident_device *);
/* D:1299-1340; F10 registers the head0 HAL adapter above. */
enum trident_dpms { TRI_DPMS_ON, TRI_DPMS_STANDBY, TRI_DPMS_SUSPEND, TRI_DPMS_OFF };
int trident_dpms_set(struct trident_device *, enum trident_dpms);
/* mode_640.c excludes Enable; lifecycle calls Enable once after ModeInit.
 * Mode entry and route protect use cur_vga_base (vgaHW IOBase).
 * Mode VGA Restore switches it to color; state VGA Restore to saved MISC.
 * PC98 Init retains Save's selection even after Init MISC|=C3.
 * Route 0xFAC also belongs to pc98_route.c; their relationship is set in 3c. */
int trident_mode_640(struct trident_device *d);
int trident_route_enable(struct trident_device *d);
int trident_route_disable(struct trident_device *d);
/* Direct VRAM CLIENT (accel and NoAccel): synchronization only, no transfer.
 * Boot-wide RAM fallback only: clip and validate both source/destination ends,
 * use selected cpp and independent client/display byte pitches, then copy
 * with write8 only. write_span32 is NOT the fallback present path. No VRAM
 * readback. CPU direct stores and cursor/pattern/video uploads may use 32bit.
 * The S3 implementation is migrated by F7; API remains unchanged. */
int trident_present(struct trident_device *, int x, int y, int w, int h);
/* HAL RGB components use GFX_PALETTE_COMPONENT_BITS (currently 4, up to 8
 * per component for 32bit color), not a backend-fixed input width.
 * palette.c and lifecycle's default palette MUST both call this shared
 * conversion with GFX_PALETTE_COMPONENT_BITS. W is in [4,8]: mask to W bits;
 * W < 6 replicates the high input bits into the remaining low DAC bits
 * (W=4: x<<2 | x>>2), W=6 is unchanged, W>6 takes the high six (W=8: x>>2).
 * This pure contract helper performs no I/O; saved/default DAC register
 * images are already in DAC units and must not be converted again. */
STATIC_ASSERT(GFX_PALETTE_COMPONENT_BITS >= 4 && GFX_PALETTE_COMPONENT_BITS <= 8, trident_palette_component_bits_range);
static inline u8 trident_palette_dac6(u8 component, u32 component_bits)
{
    u32 bits = component_bits;
    u32 x = component & ((1u << bits) - 1u);
    if (bits < 6)
        return (u8)((x << (6 - bits)) | (x >> (2 * bits - 6)));
    return (u8)(x >> (bits - 6));
}
int trident_palette(struct trident_device *, int first, int count, const u8 *rgb);
/* F8: dac6 は DAC の単位 (変換済み)。d->dac[first..first+count) を写すだけ、
 * I/O なし。範囲外は TRI_E_INVAL。 */
int tri_palette_record(struct trident_device *, int first, int count, const u8 *dac6);
/* F8 (F8 追記 2026-11-07、xf86cmap.c:254-275,525-665,855-904): 選択 mode の
 * 深度の既定の DAC 表を作って書く。depth8 は system/cube/gray、15 は 0..31
 * に i*63/31、16 は 0..63 に G=i・R/B は i<=31 で i*63/31 (ほかは 0)、24/32 は
 * 0..255 に i*63/255。呼ぶ時点は F9 (ScreenInit の palette 段)。 */
int tri_palette_load_default(struct trident_device *);
/* F9 の Enter・F10 の mode 切替の後。depth8 は d->dac を書き直し、TrueColor は
 * tri_palette_load_default。 */
int tri_palette_reload(struct trident_device *);
/* F9 target: ScreenInit PreInit once, then Save/PC98 Init/Mode/Enable/blank/
 * viewport/surfaces/Accel/Cursor/palette/DPMS registration/video reset.
 * NoAccel/SWCursor retain D:2065-2073 settings, not mandatory defaults.
 * EnterVT Mode/Enable/InitializeAccelerator; Leave Sync/Restore/lock/Disable;
 * active Close Sync/Disable/Restore/lock. Stop video with shutdown and remove
 * cooperative poll on Leave/Close. No extra Save or video reset on EnterVT.
 * Maps and arm survive close for emergency restoration.
 * last_rc is diagnostic for the latest attempt, reset on the next init. */
int trident_screen_init(struct trident_device *d);
int trident_enter(struct trident_device *d);
int trident_leave(struct trident_device *d);
int trident_close(struct trident_device *d);
/* restore98 touches hardware only while active (vtSema, D:3333). */
int trident_restore98(void *opaque);
#endif
