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
/* HAL boundary; ScreenInit/EnterVT/LeaveVT/CloseScreen live in lifecycle.c.
 * MIT reference notices and source correspondence are in trident.h.
 * Loader calls prepare only after validating and pinning module text/data.
 */
#include "trident_module.h"
#include "gfx/gfx_internal.h"
#include "lib/serial_diag.h"

/* D:3637-3719, A:141,226-277; C:1046-1214. Register sequences remain
 * in F1/F2/F3. This file adapts X's framebuffer/cursor framework to the HAL.
 * GXcopy is logical X11 raster-op 3 (A:SetupForFillRectSolid/ScreenToScreenCopy).
 */
#define TRI_HAL_COPY_ROP 3u
#define TRI_HAL_DAC_BITS 6u
static struct trident_device device;
static int display_rc; /* mandatory HAL failures, separate from optional diagnostics */
static const struct TridentDiagnosticOps diagnostics;
/* Before PreInit only the legacy default is known. After SPR identification,
 * rebuild this resident table with F3's capacity/PLL validation. hw_mode is
 * the stable driver mode ID; format always contains a HAL format ID. */
static GfxMode modes[TRI_MODE_COUNT] = {
    {TRI_WIDTH, TRI_HEIGHT, GFX_FMT_PACKED8, TRI_HAL_DAC_BITS,
     TRI_MODE_DEFAULT_ID, 0, TRI_PITCH}
};
static u16 mode_count = 1;
static i32 cursor_hot_x, cursor_hot_y;
int tri_hal_fill(int, int, int, int, u8);
int tri_hal_blit(int, int, int, int, int, int);
int tri_hal_cursor_image(u8, const struct GfxCursorImage *);
int tri_hal_cursor_move(u8, i32, i32);
int tri_hal_cursor_show(u8, int);

/* Match the other display HAL adapters: preserve the driver's rc in the
 * diagnostic state, expose public OS32 errors only at the HAL boundary. */
static int hal_rc(int rc)
{
    switch (rc) {
    case TRI_OK: return 0;
    case TRI_E_INVAL: case TRI_E_SURFACE: return OS32_ERR_INVAL;
    case TRI_E_NOSYS: return OS32_ERR_NOSYS;
    case TRI_E_BUSY: return OS32_ERR_BUSY;
    default: return OS32_ERR_IO;
    }
}

static int hal_format(const struct tri_mode *m)
{
    if (m->depth == 8 && m->bpp == 8) return GFX_FMT_PACKED8;
    if (m->depth == 16 && m->bpp == 16) return GFX_FMT_RGB565;
    if (m->depth == 24 && m->bpp == 32) return GFX_FMT_XRGB8888;
    /* PM: RGB555 and packed24 await TASK_VIDEO_HAL's public format IDs. */
    return -1;
}

static void build_modes(void)
{
    u16 count = 0;
    for (u32 i = 0; i < TRI_MODE_COUNT; ++i) {
        struct tri_mode m;
        int fmt = hal_format(&tri_modes[i]);
        if (fmt < 0 || tri_mode_prepare(&device, &tri_modes[i], &m)) continue;
        modes[count++] = (GfxMode){m.hd, m.vd, (u8)fmt, TRI_HAL_DAC_BITS,
            (u8)m.id, 0, (u32)m.display_width * (m.bpp / BITS_PER_BYTE)};
    }
    mode_count = count;
}

static bool drawable(void)
{
    return device.active && device.screen_ready && device.mode.valid &&
           !device.returning && device.res &&
           device.mode.generation == device.res->generation;
}

static void publish_mode(void)
{
    GfxBackend *b = trident_context->backend;
    b->modes = modes;
    b->mode_count = mode_count;
    b->default_mode = 0;
    b->selected_mode = NULL;
    for (u16 i = 0; i < mode_count; ++i) {
        if (modes[i].hw_mode == TRI_MODE_DEFAULT_ID) b->default_mode = i;
        if (device.mode.valid && modes[i].hw_mode == device.mode.selected.id)
            b->selected_mode = &modes[i];
    }
    if (device.mode.valid) {
        b->bb_base = device.res->client;
        b->bb_size = device.res->client_bytes;
        b->bb_pitch = device.mode.client.pitch;
        if (b->selected_mode) *trident_context->height = b->selected_mode->height;
    }
    bool ge = drawable() && device.accel.initialized && !device.mode.no_accel &&
              !device.res->ram_fallback;
    bool indexed = device.mode.valid && device.mode.client.format == TRI_FMT_INDEX8;
    b->fill_rect = ge && indexed && (device.mode.features & TRI_CAP_FILL) ? tri_hal_fill : NULL;
    b->blit = ge && indexed && (device.mode.features & TRI_CAP_COPY) ? tri_hal_blit : NULL;
    bool cursor = drawable() && device.cursor.available &&
                  (device.mode.features & TRI_CAP_CURSOR);
    b->cursor_image = cursor ? tri_hal_cursor_image : NULL;
    b->cursor_move = cursor ? tri_hal_cursor_move : NULL;
    b->cursor_show = cursor ? tri_hal_cursor_show : NULL;
}

static void accel_counters(u32 ops, u32 accesses)
{
    trident_context->counters->hw_ops += device.accel.hw_ops - ops;
    trident_context->counters->io_accesses += device.accel.io_accesses - accesses;
}
static void report(u32 stage, int rc)
{
    if (!rc || serial_gate_active()) return;
    serial_puts_polled("[trident] stage="); serial_diag_u32(stage);
    serial_puts_polled(" rc="); serial_diag_i32(rc);
    serial_puts_polled(" restore_rc="); serial_diag_i32(device.restore_rc);
    serial_puts_polled(" disable_rc="); serial_diag_i32(device.disable_rc);
    serial_puts_polled("\n");
}

static int remember(u32 stage, int rc)
{
    if (rc && !device.last_rc) {
        device.last_rc = rc;
        device.failed_step = stage;
    }
    return rc;
}

int trident_backend_prepare(void)
{
    device.failed_step = 0;
    device.last_rc = 0;
    display_rc = 0;
    const TridentModuleContext *c = trident_context;
    if (c && c->size == TRIDENT_MODULE_CONTEXT_SIZE && c->diagnostic_out)
        *c->diagnostic_out = NULL;
    if (!c || !c->backend || !c->counters || !c->resources || !c->arm ||
        !c->height || !c->flip || !c->page || !c->display_owner ||
        c->size != TRIDENT_MODULE_CONTEXT_SIZE || !c->diagnostic_out)
        return display_rc = remember(TRI_STEP_RESOURCES, TRI_E_INVAL);
    const struct trident_resources *r = c->resources;
    if (!r->ready || !r->regs || !r->lfb || !r->configure ||
        !r->mapped_bytes || !r->decode_bytes || r->regs_bytes < TRI_REG_MAPPED_BYTES)
        return display_rc = remember(TRI_STEP_RESOURCES, TRI_E_RESOURCE);
    int rc = trident_pci_validate(&r->pci);
    if (rc) return display_rc = remember(TRI_STEP_RESOURCES, rc);
    device.settings.ram_fallback = r->ram_fallback;
    if (!device.active) {
        struct tri_mode provisional = tri_modes[TRI_MODE_DEFAULT_ID - 1u];
        if (r->ram_fallback || device.settings.no_accel)
            provisional.display_width = provisional.hd;
        else {
            provisional.display_width = TRI_PITCH;
            if (device.preinit_done)
                for (u16 i = 0; i < mode_count; ++i)
                    if (modes[i].hw_mode == TRI_MODE_DEFAULT_ID)
                        provisional.display_width = modes[i].pitch / (provisional.bpp / BITS_PER_BYTE);
        }
        u32 bytes = (provisional.display_width * provisional.vd + TRI_PAGE_BYTES - 1u) & ~(TRI_PAGE_BYTES - 1u);
        if (device.settings.vram_bytes && bytes > device.settings.vram_bytes)
            return display_rc = remember(TRI_STEP_RESOURCES, TRI_E_CAP_TOO_SMALL);
        rc = r->configure(&provisional, bytes, false);
        if (rc) return display_rc = remember(TRI_STEP_RESOURCES, rc);
        if (device.mode.valid) device.mode.selected = provisional;
    }
    if (!r->client || r->client_bytes < r->client_surface.pitch * r->client_surface.height)
        return display_rc = remember(TRI_STEP_RESOURCES, TRI_E_RESOURCE);
    device.res = r;
    rc = trident_regs_bind(&device); /* descriptor-only, no hardware access */
    if (rc) return display_rc = remember(TRI_STEP_RESOURCES, rc);
    device.pinned = true; /* loader's prepare boundary guarantees the pin */
    if (!device.armed) device.step = TRI_STEP_PIN;
    c->backend->bb_base = r->client;
    c->backend->bb_size = r->client_bytes;
    if (r->client_surface.pitch) {
        c->backend->bb_pitch = r->client_surface.pitch;
        if (!device.preinit_done) modes[0].pitch = r->client_surface.pitch;
    }
    *c->diagnostic_out = &diagnostics;
    return 0;
}
static void prepare(void) { int rc = trident_backend_prepare(); report(device.failed_step, rc); }
static int probe(void) { return !display_rc && device.pinned && device.res && device.res->ready; }

static void finish(u32 stage, int rc, bool recover)
{
    if (!rc) return;
    display_rc = rc;
    remember(stage, rc);
    if (recover) {
        int recovery = trident_restore98(&device);
        /* Restore/Disable outcomes are retained separately by lifecycle. */
        if (recovery && !device.last_rc) remember(TRI_STEP_RESTORE, recovery);
    }
    report(device.failed_step, rc);
}
static void init(void)
{
    if (device.active) {
        int close_rc = trident_close(&device);
        if (close_rc) { finish(TRI_STEP_RESTORE, close_rc, false); return; }
    }
    device.last_rc = device.failed_step = 0;
    device.screen_ready = false;
    int rc = trident_backend_prepare();
    if (rc) { finish(device.failed_step, rc, false); return; }
    if (!device.armed) {
        device.display_owner = (u32)trident_context->display_owner();
        rc = trident_context->arm(trident_restore98, &device, device.display_owner);
        if (rc) { finish(TRI_STEP_ARM, rc, false); return; }
        device.armed = true;
        device.step = TRI_STEP_ARM;
    }
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    rc = trident_screen_init(&device);
    accel_counters(ops, accesses);
    if (rc) { finish(device.failed_step, rc, true); return; }
    build_modes();
    publish_mode();
    /* Optional component errors remain visible without blocking display. */
    report(device.failed_step, device.last_rc);
    *trident_context->flip = *trident_context->page = 0;
}
static int query(GFX_ScreenInfo *info)
{
    if (!info) return TRI_E_INVAL;
    if (display_rc) return display_rc;
    if (!probe()) return device.last_rc ? device.last_rc : TRI_E_STATE;
    const GfxMode *m = gfx_backend_mode(trident_context->backend);
    if (!m) return TRI_E_STATE;
    *info = (GFX_ScreenInfo){0};
    info->width = m->width; info->height = m->height;
    info->format = m->format;
    info->bpp = m->format == GFX_FMT_PACKED8 ? 8 :
                (m->format == GFX_FMT_RGB565 ? 16 : 32);
    if (trident_context->backend->fill_rect) info->flags |= GFX_CAP_HW_FILL;
    if (trident_context->backend->blit) info->flags |= GFX_CAP_HW_BLT;
    if (m->format == GFX_FMT_PACKED8) {
        info->lease_first = GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP);
        info->lease_count = TRI_DAC_COLORS - info->lease_first;
    }
    return 0;
}
static void enter(void)
{
    display_rc = 0;
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    int rc = trident_enter(&device);
    accel_counters(ops, accesses);
    finish(device.failed_step ? device.failed_step : TRI_STEP_MODE, rc, true);
    publish_mode();
}
/* No serial/console output inside the emergency callback trident_restore98. */
static void leave(void)
{
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    int rc = trident_leave(&device);
    accel_counters(ops, accesses);
    finish(device.failed_step, rc, false);
    publish_mode();
}
static void shutdown(void)
{
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    int rc = trident_close(&device);
    accel_counters(ops, accesses);
    finish(device.failed_step, rc, false);
    publish_mode();
}
static void present(int x, int y, int w, int h)
{
    if (!drawable()) return;
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    int rc = trident_present(&device, x, y, w, h);
    accel_counters(ops, accesses);
    if (rc) { finish(TRI_STEP_ACTIVE, rc, true); publish_mode(); return; }
    if (gfx_clip_screen(&x, &y, &w, &h, device.mode.client.width, device.mode.client.height)) {
        /* F7 direct CLIENT only synchronizes: no transferred bytes. */
        if (device.res->ram_fallback)
            trident_context->counters->present_bytes += (u32)w * (u32)h * device.mode.client.cpp;
        ++trident_context->counters->commits;
    }
}
static void set_palette(int first, int count, const u8 *rgb)
{
    if (!drawable()) return;
    int rc = trident_palette(&device, first, count, rgb);
    if (rc) { finish(TRI_STEP_CLIENT, rc, true); publish_mode(); return; }
    if (device.mode.client.format != TRI_FMT_INDEX8) return;
    for (int i = 0; i < count && first + i < (int)GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP); ++i)
        palette_shadow_set(first + i, rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
}
/* Empty rectangles succeed. Paired clipping keeps source/destination aligned
 * without adding arbitrary signed coordinates before they are bounded. */
static int clip_pair(int *dst, int *src, int *length, int limit)
{
    if (*length <= 0 || *dst >= limit || *src >= limit) return 0;
    if (*dst < 0) {
        if (*dst <= -*length) return 0;
        int skip = -*dst;
        if (*src > limit - 1 - skip) return 0;
        *src += skip; *length -= skip; *dst = 0;
    }
    if (*src < 0) {
        if (*src <= -*length) return 0;
        int skip = -*src;
        if (*dst > limit - 1 - skip) return 0;
        *dst += skip; *length -= skip; *src = 0;
    }
    if (*length > limit - *dst) *length = limit - *dst;
    if (*length > limit - *src) *length = limit - *src;
    return *length > 0;
}

static int primitive_result(int rc, u32 ops, u32 accesses)
{
    accel_counters(ops, accesses);
    if (rc) {
        finish(TRI_STEP_ACTIVE, rc, true);
        publish_mode();
    }
    return hal_rc(rc);
}
int tri_hal_fill(int x, int y, int w, int h, u8 color)
{
    if (!drawable()) return OS32_ERR_IO;
    if (!trident_context->backend->fill_rect) return OS32_ERR_NOSYS;
    const struct tri_surface *s = &device.mode.client;
    if (!gfx_clip_screen(&x, &y, &w, &h, s->width, s->height)) return 0;
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    /* F1's final Sync is the CPU return boundary. No additional wait. */
    int rc = tri_fill(&device, s, x, y, w, h, color, TRI_HAL_COPY_ROP);
    return primitive_result(rc, ops, accesses);
}
int tri_hal_blit(int dx, int dy, int sx, int sy, int w, int h)
{
    if (!drawable()) return OS32_ERR_IO;
    if (!trident_context->backend->blit) return OS32_ERR_NOSYS;
    const struct tri_surface *s = &device.mode.client;
    if (!clip_pair(&dx, &sx, &w, s->width) ||
        !clip_pair(&dy, &sy, &h, s->height)) return 0;
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    int rc = tri_copy_direction(&device, dx > sx ? -1 : 1, dy > sy ? -1 : 1);
    /* A:280-327: overlap direction precedes copy; F1 ends in ClearSync. */
    if (!rc) rc = tri_copy(&device, s, s, sx, sy, dx, dy, w, h,
                           TRI_HAL_COPY_ROP, false, 0);
    return primitive_result(rc, ops, accesses);
}

int tri_hal_dpms(u8 head, u32 state)
{
    if (head || state > TRI_DPMS_OFF) return OS32_ERR_INVAL;
    if (!probe()) return OS32_ERR_NOSYS;
    int rc = trident_dpms_set(&device, (enum trident_dpms)state);
    if (rc) { remember(TRI_STEP_ACTIVE, rc); report(TRI_STEP_ACTIVE, rc); }
    return hal_rc(rc);
}

static int cursor_result(int rc, u32 ops, u32 accesses)
{
    accel_counters(ops, accesses);
    if (rc && rc != TRI_E_NOSYS && rc != TRI_E_INVAL) {
        remember(TRI_STEP_ACTIVE, rc);
        report(TRI_STEP_ACTIVE, rc);
        device.cursor.available = false;
        device.mode.features &= ~TRI_CAP_CURSOR;
        publish_mode();
    }
    return hal_rc(rc);
}
int tri_hal_cursor_image(u8 head, const struct GfxCursorImage *im)
{
    if (head || !im) return OS32_ERR_INVAL;
    if (!drawable() || !trident_context->backend->cursor_image) return OS32_ERR_NOSYS;
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    int rc = tri_cursor_convert(&device, im);
    if (!rc) rc = tri_cursor_colors(&device, device.cursor.bg, device.cursor.fg);
    if (!rc) rc = tri_cursor_image(&device, im);
    if (!rc) { cursor_hot_x = im->hot_x; cursor_hot_y = im->hot_y; }
    return cursor_result(rc, ops, accesses);
}
/* Saturate only arithmetic overflow; the cursor implementation retains the
 * reference's register masking for ordinary offscreen positions. */
static i32 cursor_origin(i32 pos, i32 hot)
{
    if (hot > 0 && pos < (-S32_MAX - 1) + hot) return (-S32_MAX - 1);
    if (hot < 0 && pos > S32_MAX + hot) return S32_MAX;
    return pos - hot;
}
int tri_hal_cursor_move(u8 head, i32 x, i32 y)
{
    if (head) return OS32_ERR_INVAL;
    if (!drawable() || !trident_context->backend->cursor_move) return OS32_ERR_NOSYS;
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    int rc = tri_cursor_move(&device, cursor_origin(x, cursor_hot_x),
                             cursor_origin(y, cursor_hot_y));
    return cursor_result(rc, ops, accesses);
}
int tri_hal_cursor_show(u8 head, int show)
{
    if (head || (show != 0 && show != 1)) return OS32_ERR_INVAL;
    if (!drawable() || !trident_context->backend->cursor_show) return OS32_ERR_NOSYS;
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    int rc = tri_cursor_show(&device, show != 0);
    return cursor_result(rc, ops, accesses);
}

/* Optional setup errors are diagnostic only. Like D:3692-3703, keep the
 * framebuffer usable and remove the capability whose component is absent. */
static void optional_components(void)
{
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    int rc = tri_accel_init(&device, &device.mode.selected);
    if (rc) {
        remember(TRI_STEP_CLIENT, rc);
        report(TRI_STEP_CLIENT, rc);
        device.accel.initialized = false;
        device.mode.no_accel = true;
    }
    if (!device.accel.initialized) device.mode.features &= ~TRI_CAP_GE;
    if (!device.accel.pattern.allocated) device.mode.features &= ~TRI_CAP_MONO_PATTERN;
    rc = tri_cursor_init(&device);
    if (rc) {
        remember(TRI_STEP_CLIENT, rc);
        report(TRI_STEP_CLIENT, rc);
        device.cursor.available = false;
    }
    if (!device.cursor.available) device.mode.features &= ~TRI_CAP_CURSOR;
    cursor_hot_x = cursor_hot_y = 0;
    accel_counters(ops, accesses);
}

static int mode_failure(u32 stage, int rc)
{
    device.screen_ready = false;
    finish(stage, rc, true);
    publish_mode();
    return hal_rc(rc);
}
int tri_hal_mode(u32 id)
{
    if (!drawable()) return OS32_ERR_IO;
    const struct tri_mode *candidate = NULL;
    for (u16 i = 0; i < mode_count; ++i)
        if (modes[i].hw_mode == id) {
            candidate = &tri_modes[id - 1u];
            break;
        }
    if (!candidate) return OS32_ERR_INVAL;
    if (!device.res->configure) return OS32_ERR_NOSYS;
    struct tri_mode prepared;
    int rc = tri_mode_prepare(&device, candidate, &prepared);
    if (rc) return hal_rc(rc);
    /* Caller-owned diagnostic offscreen surfaces must be released first;
     * F5 refuses a reset with live consumers. No new allocation owner here. */
    for (struct tri_vram_block *b = device.vram.blocks; b; b = b->next)
        if (b != &device.accel.pattern && b != &device.cursor.allocation &&
            b != &device.video.allocation) return OS32_ERR_BUSY;
    device.last_rc = device.failed_step = 0;
    u32 ops = device.accel.hw_ops, accesses = device.accel.io_accesses;
    rc = tri_sync(&device);
    accel_counters(ops, accesses);
    if (rc) return mode_failure(TRI_STEP_ACTIVE, rc);
    rc = tri_video_stop(&device, true);
    /* A failed overlay disable must retain its live allocation; do not reuse
     * it for a new framebuffer. The existing screen remains available. */
    if (rc) {
        device.video.last_rc = rc;
        remember(TRI_STEP_CLIENT, rc);
        report(TRI_STEP_CLIENT, rc);
        return hal_rc(rc);
    }
    rc = tri_vram_free(&device, &device.accel.pattern);
    if (rc) return mode_failure(TRI_STEP_CLIENT, rc);
    device.accel.initialized = false;
    rc = tri_vram_free(&device, &device.cursor.allocation);
    if (rc) return mode_failure(TRI_STEP_CLIENT, rc);
    device.cursor.available = false;
    rc = tri_vram_init(&device, &prepared);
    if (rc) return mode_failure(TRI_STEP_CLIENT, rc);
    /* F6: revoke old leases and replace both surfaces before programming the
     * new scanout. configure performs no hardware access. */
    rc = device.res->configure(&prepared, device.vram.first_off, true);
    if (rc) return mode_failure(TRI_STEP_CLIENT, rc);
    u32 video_feature = device.mode.features & TRI_CAP_VIDEO;
    device.screen_ready = false;
    rc = tri_mode_set(&device, &prepared);
    if (rc) return mode_failure(TRI_STEP_MODE, rc);
    device.mode.features &= ~TRI_CAP_VIDEO | video_feature;
    rc = trident_route_enable(&device);
    if (rc) return mode_failure(TRI_STEP_ENABLE, rc);
    rc = trident_adjust_frame(&device);
    if (rc) return mode_failure(TRI_STEP_MODE, rc);
    device.mode.display = device.res->display;
    device.mode.client = device.res->client_surface;
    device.mode.generation = device.res->generation;
    optional_components();
    rc = tri_palette_reload(&device);
    if (rc) return mode_failure(TRI_STEP_CLIENT, rc);
    if (device.res->ram_fallback) {
        rc = trident_present(&device, 0, 0, device.mode.client.width, device.mode.client.height);
        if (rc) return mode_failure(TRI_STEP_CLIENT, rc);
    }
    /* No automatic DPMS On, no EnterVT video reset, no present-driven tick. */
    device.screen_ready = true;
    device.step = TRI_STEP_ACTIVE;
    publish_mode();
    *trident_context->flip = *trident_context->page = 0;
    return 0;
}

/* F16: same production context; poll owns the fresh clock read and records
 * video.last_rc. Optional video failures never feed mandatory present. */
static const struct TridentDiagnosticOps diagnostics = {
    .version = TRIDENT_DIAGNOSTIC_OPS_VERSION,
    .size = TRIDENT_DIAGNOSTIC_OPS_SIZE, .context = &device,
    .set_mode = tri_hal_mode, .dpms = tri_hal_dpms,
    .cursor_image = tri_hal_cursor_image, .cursor_move = tri_hal_cursor_move,
    .cursor_show = tri_hal_cursor_show,
    .sync = tri_sync, .clear_sync = tri_clear_sync,
    .copy_direction = tri_copy_direction, .line_setup = tri_line_setup,
    .fill = tri_fill, .copy = tri_copy, .pattern = tri_pattern,
    .line = tri_line, .pattern_upload = tri_pattern_upload,
    .video_query = tri_video_query, .video_put = tri_video_put,
    .video_set_key = tri_video_set_key, .video_stop = tri_video_stop,
    .poll = tri_video_poll, .video_surface_alloc = tri_video_surface_alloc,
    .video_surface_display = tri_video_surface_display,
    .video_surface_stop = tri_video_surface_stop,
    .video_surface_free = tri_video_surface_free,
    .present = trident_present
};

GfxBackend trident_backend_ops = {
    .name = "trident", .probe = probe, .prepare = prepare, .init = init,
    .query = query, .shutdown = shutdown, .present_rect = present,
    .set_palette = set_palette, .enter = enter, .leave = leave,
    .bb_pitch = TRI_PITCH, .bb_format = GFX_BB_PACKED8,
    .modes = modes, .mode_count = 1,
    .set_mode = tri_hal_mode, .dpms = tri_hal_dpms
};
