/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/* GeForce HAL adapter. Reservation and device ownership belong to the caller. */
#include "gfx_hal.h"
#include "video_service.h"
#include "memmap.h"
#include "kprintf.h"
#include "../../../gfx/palette.h"
#include "../pc98/pc98_route.h"
#include "hwdev/gf/foundation.h"
#ifdef OS32_GEFORCE_MODULE
#include "geforce_module.h"
#include "gf_v2.h"
#include "hwdev/gf/display/api.h"
#include "hwdev/gf/feature5.h"
#include "hwdev/gf/production.h"
#include "leave_log.h"
#include "../../../gfx/gfx_internal.h"
#include "../../../gfx/gfx_policy.h"
#include "../../../gfx/gfx_osri_view.h"
#define gfx_backend_geforce (*geforce_context->backend)
#define gfx_current_height (*geforce_context->height)
#define gfx_flip_enabled (*geforce_context->flip)
#define gfx_display_page (*geforce_context->page)
#endif
extern void geforce_display_failed(void) __attribute__((weak));

#define GF_WIDTH 640
#define GF_HEIGHT 480
#define GF_PITCH GF_WIDTH
#define GF_MODE_INDEX 0U
/* dispnv04/crtc.c:869-883 uses depth 16 and depth 24/cpp 4. HAL bpp
 * describes storage, so XRGB8888 is 32 bits per pixel. */
#define GF_RGB565_BPP 16U
#define GF_XRGB8888_BPP 32U
#define GF_GXCOPY 3U /* xf86-video-nouveau nv04_exa.c:46-84 */
#define GF_LEASE_FIRST 16
#define GF_LEASE_COUNT (VIDEO_PALETTE_ENTRIES - GF_LEASE_FIRST)
#define GF_CUBE_STEP 51

extern int pc98_route_to_gf(void);
extern int pc98_route_to_98(void);
extern int pc98_route_cell_check(void);
extern int gf_present_rect(const u8 *, u32, volatile u8 *, u32, volatile u8 *,
                           int, int, int, int, int (*)(void *), void *);
#ifndef OS32_GEFORCE_MODULE
extern int gfx_current_height, gfx_flip_enabled, gfx_display_page;
extern GfxBackend gfx_backend_geforce;
#endif

static struct video_device *device;
static int (*identify_resources)(void);
static struct video_fb framebuffer;
#ifdef OS32_GEFORCE_MODULE
static struct gf3_surface selected_front;
static bool scanout_logged;
static GfxMode selected_scan_mode;
static GfxMode display_modes[GF4_MODES];
/* Borrow the same resident owner as the hooks. Foreground only; never
 * attach this to the IRQ/tick path (GF_FEAT5_DESIGN final C2 addendum).
 * Nouveau c03c1a1 therm/base.c:361-370 uses work scheduling instead. */
static struct gf5 *feature5;
static int geforce_heat_stop(void);
static int geforce_heat_clear(void);
static bool geforce_heat_backend_active(void);
void geforce_backend_feature5_bind(struct gf5 *g)
{
    feature5 = g;
    if (geforce_context->heat) {
        geforce_context->heat->stop=geforce_heat_stop;
        geforce_context->heat->clear=geforce_heat_clear;
        if (g) g->stop=&geforce_context->heat->stop_state;
        geforce_context->heat->backend_active=geforce_heat_backend_active;
    }
}
/* Module-local forwarding avoids an additional import or retained gf5 loan. */
void geforce_heat_stop_request(i32 reason,i32 milli_c,i32 source_rc)
{
    if (geforce_context->heat && geforce_context->heat->request)
        geforce_context->heat->request(reason,milli_c,source_rc);
}
static int feature5_poll(void)
{
    return feature5 ? gf5_service_poll(feature5) : 0;
}
#endif
#ifdef OS32_GEFORCE_MODULE
void geforce_backend_framebuffer_info(struct video_fb *out, u32 *width, u32 *height)
{
    *out = framebuffer;
    *width = selected_front.bytes ? selected_front.width : GF_WIDTH;
    *height = selected_front.bytes ? selected_front.height : GF_HEIGHT;
}
#endif
static u32 bar0_phys;
static int (*interrupt_point)(void *);
static void *interrupt_context;
static int failed, active, routed;
#ifdef OS32_GEFORCE_MODULE
static int geforce_direct_ok(void)
{
    return active && gfx_is_started() && gf_direct_ok();
}
#endif
static int (*stop_device)(void);
static int (*start_wc)(void);
void geforce_backend_wc_bind(int (*start)(void)) { start_wc = start; }
void geforce_backend_stop_bind(int (*stop)(void)) { stop_device = stop; }
static u8 initial_palette[VIDEO_PALETTE_BYTES];

/* Integration supplies a long-lived device and a read-only identity/resource
 * check. The core binds the CLIENT surface into gfx_backend_geforce.bb_base. */
void geforce_backend_bind(struct video_device *d, int (*identify)(void),
                          u32 bar0, int (*point)(void *), void *context)
{
    device = d;
    identify_resources = identify;
    bar0_phys = bar0;
    interrupt_point = point;
    interrupt_context = context;
}

#ifdef OS32_GEFORCE_MODULE
/* No cached dispatch: C2 closes the kernel slot during change/recovery. */
static const struct gf4_test_ops *display_ops(void)
{
    return geforce_context->display_out ? *geforce_context->display_out : 0;
}
static int display_result(struct gf4_ctx *c, int rc)
{
    if (c) c->last_rc = rc;
    switch (rc) {
    case 0: return 0;
    case GF4_EINVAL: case GF4_ERANGE: case GF4_EFORMAT:
    case GF3_EINVAL: case GF3_ERANGE: case GF3_EFORMAT: return OS32_ERR_INVAL;
    case GF4_EUNSUPPORTED: case GF4_ENODEV: return OS32_ERR_NOSYS;
    case GF4_ENOSPC: case GF2_ENOSPC: return OS32_ERR_NOSPC;
    case GF4_EBUSY: return OS32_ERR_BUSY;
    default: return OS32_ERR_IO;
    }
}
static int display_deadline(struct gf4_ctx *c, u32 *dl)
{
    u32 now;
    struct gf_io *io = &c->ch->f->io;
    if (!io->timing.clock_us) return GF2_EIO;
    int rc = io->timing.clock_us(io->opaque, &now);
    if (rc) return rc;
    *dl = now + GF3_TIMEOUT_US;
    return 0;
}
/* Read the live head-0 scan, independently of selected_front. Register
 * widths/fields: MIT nouveau c03c1a1 dispnv04/hw.h:119-140,353-372;
 * crtc.c:175-233,869-903; dac.c:385-415. This HAL presents head 0 only.
 * Keep the original I/O rc and restore CR44 even on a failed read. */
#define SCAN_CR 0x6013d4U
#define SCAN_SR 0x0c03c4U
#define SCAN_START 0x600800U
#define SCAN_CONFIG 0x600804U
#define SCAN_DACCLK 0x68052cU
#define SCAN_TEST 0x680608U
static int scanout_index(struct gf_io *io, u32 port, u32 index, u32 *value)
{
    int rc = io->write(io->opaque, 0, port, 1, index);
    return rc ? rc : io->read(io->opaque, 0, port + 1U, 1, value);
}
static void geforce_scanout_log(u32 bytes)
{
    if (scanout_logged) return;
    scanout_logged = true;
    const struct gf4_test_ops *ops = display_ops();
    struct gf4_ctx *c = ops ? ops->ctx : 0;
    if (!c || !c->ch || !c->ch->f) {
        kprintf(KLOG_ATTR, "[gf4] scanout fail stage=context rc=%d\n", GF4_ESTATE);
        return;
    }
    struct gf_io *io = &c->ch->f->io;
    u32 owner, start=0, config=0, lo=0, hi=0, ext=0, pixel=0;
    u32 sr=0, control=0, sync=0, dac=0, test=0;
    int rc = scanout_index(io, SCAN_CR, 0x44U, &owner);
    if (!rc) {
        rc = io->write(io->opaque, 0, SCAN_CR + 1U, 1, 0);
        if (!rc) rc = io->read(io->opaque, 0, SCAN_START, 4, &start);
        if (!rc) rc = io->read(io->opaque, 0, SCAN_CONFIG, 4, &config);
        if (!rc) rc = scanout_index(io, SCAN_CR, GF_MODE_CR_PITCH, &lo);
        if (!rc) rc = scanout_index(io, SCAN_CR, GF_MODE_CR_PITCH_HI, &hi);
        if (!rc) rc = scanout_index(io, SCAN_CR, GF_MODE_CR_PITCH11, &ext);
        if (!rc) rc = scanout_index(io, SCAN_CR, GF_MODE_CR_PIXEL, &pixel);
        if (!rc) rc = scanout_index(io, SCAN_SR, 1, &sr);
        if (!rc) rc = scanout_index(io, SCAN_CR, GF_MODE_CR_MODE, &control);
        if (!rc) rc = scanout_index(io, SCAN_CR, GF_MODE_CR_RPC1, &sync);
        if (!rc) rc = io->read(io->opaque, 0, SCAN_DACCLK, 4, &dac);
        if (!rc) rc = io->read(io->opaque, 0, SCAN_TEST, 4, &test);
        int restore = io->write(io->opaque, 0, SCAN_CR, 1, 0x44U);
        if (!restore) restore = io->write(io->opaque, 0, SCAN_CR + 1U, 1, owner);
        if (restore) kprintf(KLOG_ATTR, "[gf4] scanout fail stage=owner_restore rc=%d\n", restore);
        if (!rc) rc = restore;
    }
    if (rc) {
        kprintf(KLOG_ATTR, "[gf4] scanout fail stage=read rc=%d\n", rc);
        return;
    }
    u32 pitch = (lo | ((hi >> 5) & 7U) << 8 | ((ext >> 6) & 1U) << 11) * 8U;
    u32 depth = (pixel & 3U) == 3U ? 32U : (pixel & 3U) * 8U;
    const struct gf4_head_image *h = &c->state->head[0];
    kprintf(KLOG_ATTR, "[gf4] scanout head=0 start=%08x front=%08x pitch=%u depth=%u output=%u dpms=%u present=%s bytes=%u fb=%08x expected_pitch=%u config=%08x connector=%u or=%u dac=%08x test=%08x sr1=%02x cr17=%02x cr1a=%02x blank=%u\n",
        start, selected_front.off, pitch, depth, h->route.type, h->dpms,
        selected_front.off ? "upload" : "cpu", bytes,
        framebuffer.phys - geforce_context->platform->display_phys, selected_front.pitch,
        config, h->route.connector, h->route.or_mask, dac, test, sr, control, sync, h->blank);
}
static u8 display_format(enum gf3_fmt fmt)
{
    switch (fmt) {
    case GF3_INDEX8: return GFX_FMT_PACKED8;
    case GF3_RGB565: return GFX_FMT_RGB565;
    case GF3_XRGB8888: return GFX_FMT_XRGB8888;
    default: return 0;
    }
}
/* Confirmed head-0 scan only. C2 calls this after validation and before
 * publishing either dispatch. BO format/pitch/base: nouveau c03c1a1
 * dispnv04/crtc.c:838-903; flip completion :1074-1087,1136-1217.
 * OS32's immutable mode table and value loans replace DRM framebuffer BOs.
 * No wait, allocation, register write or dispatch lookup in publication. */
void gf_hook_scan_changed(u8 head, const struct gf3_surface *front, u32 mode_id)
{
    if (head || !front) return;
    if (gf_v2_owns(front)) return;
    selected_front = *front;
    selected_scan_mode = (GfxMode){front->width, front->height,
        display_format(front->fmt), VIDEO_RGB_BITS, 0, 0, front->pitch};
    framebuffer = (struct video_fb){geforce_context->platform->display_phys + front->off, front->bytes,
        front->pitch, selected_scan_mode.format};
    struct gf4_state *state = geforce_context->display_state;
    if (state) {
        for (u32 i = 0; i < state->nmodes; ++i) {
            const struct gf4_mode *m = &state->modes[i];
            if (m->id == mode_id) gfx_backend_geforce.default_mode = i;
            display_modes[i] = (GfxMode){m->t.hd, m->t.vd,
                display_format(m->fmt), VIDEO_RGB_BITS, 0, 0, m->pitch};
        }
        gfx_backend_geforce.modes = display_modes;
        gfx_backend_geforce.mode_count = state->nmodes;
    }
    gfx_backend_geforce.selected_mode = &selected_scan_mode;
    gfx_current_height = front->height;
    gf_hook_scan_bind(true, front);
    gfx_policy_scan_changed();
}
static int geforce_set_mode(u32 id)
{
    int poll = feature5_poll();
    if (poll) return poll;
    const struct gf4_test_ops *ops = display_ops();
    if (!ops || !ops->set_mode || !ops->scan_prepare || !ops->scan_discard)
        return OS32_ERR_NOSYS;
    struct gf4_ctx *c = ops->ctx;
    const struct gf4_route route = c->state->head[0].route;
    if (route.head || route.type != GF4_ANALOG)
        return display_result(c, GF4_EUNSUPPORTED);
    bool found = false;
    for (u32 i = 0; i < c->state->nmodes; ++i)
        if (c->state->modes[i].id == id &&
            c->state->mode_connector[i] == route.connector) found = true;
    if (!found) return display_result(c, GF4_EINVAL);
    u32 dl;
    int rc = display_deadline(c, &dl);
    struct gf3_surface scan[2];
    if (!rc) rc = ops->scan_prepare(c, 0, id, scan, dl);
    if (rc) return display_result(c, rc);
    /* New reservation only: never draw into a front/hold/snapshot. gf3_fill
     * synchronously drains its own render fence with this same deadline. */
    rc = gf3_fill(c->accel, &scan[0],
        (struct gf3_rect){0, 0, scan[0].width, scan[0].height},
        0, GF_GXCOPY, U32_MAX, dl); /* GXcopy, all planes; nv04_exa.c:46-84. */
    if (rc) {
        /* A failed fill can already be queued/published. Drain before unpin,
         * including packets whose fence emission failed. Reference protocol:
         * nouveau c03c1a1 nv10_fence.c:33-41,53-55; BO lifetime:
         * nouveau_bo.c:594-629. Keep the original error and absolute deadline. */
        struct gf2_fence fence;
        int drain = gf2_fence_emit(c->ch, &fence, dl);
        if (!drain) drain = gf2_fence_wait(c->ch, fence, dl);
        if (drain) {
            c->recovery_rc = drain;
            c->deadline = dl;
            c->reusable = false;
            c->accepting = false;
            /* The existing failure owner closes admission and releases only
             * after confirmed stop. Never discard these candidates here or
             * allow a later scan_prepare to recycle them while GPU is live. */
            (void)c->links->failed(c->links->owner, rc, dl);
            return display_result(c, rc);
        }
    }
    if (!rc) rc = ops->set_mode(c, &route, id, &scan[0], dl);
    if (rc) {
        int cleanup = ops->scan_discard(c, 0, dl);
        if (cleanup) c->recovery_rc = cleanup;
    }
    /* C2 scan_changed/admission commits the descriptor, even on rollback. */
    return display_result(c, rc);
}
static int geforce_dpms(u8 head, u32 state)
{
    int poll = feature5_poll();
    if (poll) return poll;
    const struct gf4_test_ops *ops = display_ops();
    if (!ops || !ops->dpms_set) return OS32_ERR_NOSYS;
    u32 dl;
    int rc = state > GF4_OFF ? GF4_EINVAL : display_deadline(ops->ctx, &dl);
    if (!rc) rc = ops->dpms_set(ops->ctx, head, (enum gf4_dpms)state, dl);
    return display_result(ops->ctx, rc);
}
static int geforce_cursor_image(u8 head, const struct GfxCursorImage *image)
{
    int poll = feature5_poll();
    if (poll) return poll;
    const struct gf4_test_ops *ops = display_ops();
    if (!ops || !ops->cursor_image) return OS32_ERR_NOSYS;
    if (!image) return display_result(ops->ctx, GF4_EINVAL);
    struct gf4_cursor cursor = {image->argb_pm, image->bytes, image->pitch,
        image->width, image->height, image->hot_x, image->hot_y};
    u32 dl;
    int rc = display_deadline(ops->ctx, &dl);
    if (!rc) rc = ops->cursor_image(ops->ctx, head, &cursor, dl);
    return display_result(ops->ctx, rc);
}
static int geforce_cursor_move(u8 head, i32 x, i32 y)
{
    int poll = feature5_poll();
    if (poll) return poll;
    const struct gf4_test_ops *ops = display_ops();
    return !ops || !ops->cursor_move ? OS32_ERR_NOSYS :
        display_result(ops->ctx, ops->cursor_move(ops->ctx, head, x, y));
}
static int geforce_cursor_show(u8 head, int show)
{
    int poll = feature5_poll();
    if (poll) return poll;
    const struct gf4_test_ops *ops = display_ops();
    if (!ops || !ops->cursor_show) return OS32_ERR_NOSYS;
    if (show != 0 && show != 1) return display_result(ops->ctx, GF4_EINVAL);
    return display_result(ops->ctx, ops->cursor_show(ops->ctx, head, show != 0));
}
static int geforce_vsync(u8 head, u32 after, u32 dl, u32 *seen)
{
    int poll = feature5_poll();
    if (poll) return poll;
    const struct gf4_test_ops *ops = display_ops();
    return !ops || !ops->vblank_wait ? OS32_ERR_NOSYS :
        display_result(ops->ctx, ops->vblank_wait(ops->ctx, head, after, dl, seen));
}
static int geforce_page_flip(u8 head, const struct GfxScanout *s, int sync, u32 dl)
{
    int poll = feature5_poll();
    if (poll) return poll;
    const struct gf4_test_ops *ops = display_ops();
    if (!ops || !ops->flip) return OS32_ERR_NOSYS;
    struct gf4_ctx *c = ops->ctx;
    if (!s || (sync != 0 && sync != 1)) return display_result(c, GF4_EINVAL);
    enum gf3_fmt fmt;
    switch (s->format) {
    case GFX_FMT_PACKED8: fmt = GF3_INDEX8; break;
    case GFX_FMT_RGB565: fmt = GF3_RGB565; break;
    case GFX_FMT_XRGB8888: fmt = GF3_XRGB8888; break;
    default: return display_result(c, GF4_EFORMAT);
    }
    /* HAL carries no generation/owner: obtain both from the live registry,
     * then match the contained image before passing it to B7. */
    u32 cursor = 0;
    struct gf_vram_reservation key;
    int rc;
    while ((rc = gf_vram_next(c->ch, &cursor, &key)) > 0)
        if (key.reservation_id == s->reservation_id) break;
    if (rc <= 0) return display_result(c, rc ? rc : GF3_ESTATE);
    rc = gf_vram_match(c->ch, key.owner, s->reservation_id, key.generation,
        s->offset, s->bytes, &key);
    struct gf3_surface front = {s->offset, s->bytes, s->pitch, s->width, s->height,
        fmt, s->reservation_id, key.generation, 0};
    if (!rc) rc = ops->flip(c, head, &front, sync != 0, dl);
    return display_result(c, rc);
}
static int geforce_gamma(u8 head, const u16 rgb[3][256])
{
    int poll = feature5_poll();
    if (poll) return poll;
    const struct gf4_test_ops *ops = display_ops();
    return !ops || !ops->gamma ? OS32_ERR_NOSYS :
        display_result(ops->ctx, ops->gamma(ops->ctx, head, rgb));
}
#endif

static void geforce_fail(void);

static int geforce_probe(void)
{
    int ok = !failed && device && identify_resources && identify_resources();
#ifdef OS32_GEFORCE_MODULE
    if (geforce_context->heat && geforce_context->heat->stop_state.state!=GF5_STOP_NORMAL) ok=0;
#endif
    /* Reinit may drop this backend immediately after a failed probe. */
    if (!ok && active) geforce_fail();
    return ok;
}

static void geforce_prepare(void)
{
    (void)geforce_probe();
}

static void geforce_palette_init(void)
{
    const PaletteEntry *sys = palette_get_all();
    u32 i = 0;
    for (; i < GF_LEASE_FIRST; ++i) {
        initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_R] = (sys[i].r & GFX_PALETTE_COMPONENT_MASK) * PEGC_PAL_KAPI_SCALE;
        initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_G] = (sys[i].g & GFX_PALETTE_COMPONENT_MASK) * PEGC_PAL_KAPI_SCALE;
        initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_B] = (sys[i].b & GFX_PALETTE_COMPONENT_MASK) * PEGC_PAL_KAPI_SCALE;
    }
    for (u32 r = 0; r < GFX_PALETTE_CUBE_LEVELS; ++r)
        for (u32 g = 0; g < GFX_PALETTE_CUBE_LEVELS; ++g)
            for (u32 b = 0; b < GFX_PALETTE_CUBE_LEVELS; ++b, ++i) {
                initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_R] = r * GF_CUBE_STEP;
                initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_G] = g * GF_CUBE_STEP;
                initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_B] = b * GF_CUBE_STEP;
            }
    for (u32 gray = 0; i < VIDEO_PALETTE_ENTRIES; ++i, ++gray) {
        u8 v = GFX_PALETTE_GRAY_FIRST + gray * GFX_PALETTE_GRAY_STEP;
        for (u32 c = 0; c < GFX_RGB_COMPONENTS; ++c)
            initial_palette[i * GFX_RGB_COMPONENTS + c] = v;
    }
}

static int geforce_stop_common(bool force)
{
    (void)force;
    bool was_routed = routed, forced_stop = false;
#ifdef OS32_GEFORCE_MODULE
    gf_hook_scan_bind(false, 0);
    struct gf_display_context *video = device ? device->context : 0;
    struct gf_io *leave_io = video ? &video->io : 0;
#endif
    active = routed = 0;
#ifdef OS32_GEFORCE_MODULE
    gfx_policy_reeval();
#endif
    int rc = stop_device ? stop_device() : device ? video_stop(device) : 0;
#ifdef OS32_GEFORCE_MODULE
    if (force && rc && feature5 && feature5->ch && feature5->ch->f) {
        forced_stop = true;
        /* Lower reset cannot finish the display/channel ownership cleanup.
         * Rebuild that lifetime on the next enter, even if reset succeeds. */
        failed = 1;
        rc=gf_foundation_stop(feature5->ch->f);
    }
#endif
    /* Sample stopped GF state before route_to_98 performs the final redraw. */
#ifdef OS32_GEFORCE_MODULE
    geforce_leave_log(leave_io, rc);
#endif
    int route_rc = (was_routed || forced_stop) ? pc98_route_to_98() : 0;
    if (route_rc) rc = -1;
    active = routed = 0;
    if (rc) failed = 1;
    return rc;
}

static int geforce_stop(void)
{
    return geforce_stop_common(false);
}

#ifdef OS32_GEFORCE_MODULE
static bool geforce_heat_backend_active(void)
{
    return active;
}
static int geforce_heat_stop(void)
{
    /* Same backend closure as fail, without making cooling unrecoverable. */
    return geforce_stop_common(true);
}
static int geforce_heat_clear(void)
{
    const struct geforce_platform *p=geforce_context->platform;
    struct gf_production *owner=p ? p->opaque : 0;
    if (failed || !feature5 || !owner || !feature5->ch ||
        !feature5->ch->f || feature5->ch->f->revoked ||
        gf_production_identity(owner) || gf_production_mapped(owner))
        return GF5_ESTATE;
    /* Permanent BAR windows are checked before sensor setup/read. No start,
     * admission, mode programming or routing to GF is allowed here. */
    struct gf5_thermal sample;
    int rc=gf5_therm_read(feature5,&sample);
    if (rc) return rc;
    i64 cool=((i64)feature5->fan.threshold[1]-feature5->fan.hyst[1])*1000;
    if (!sample.calibrated || sample.milli_c>cool) return GF5_EBUSY;
    rc=gf5_therm_monitor_restart(feature5);
    if (rc) return rc;
    feature5->heat.critical_paused=false;
    feature5->heat.cool_samples=0;
    return 0;
}
#endif

static void geforce_fail(void)
{
    failed = 1;
    geforce_stop();
    if (geforce_display_failed) geforce_display_failed();
}

/* The video context retains its F4 loan even if start closed admission.
 * Capture before geforce_fail can replace both layers' last_rc values. */
static void geforce_start_fail(const char *stage, int rc)
{
#ifdef OS32_GEFORCE_MODULE
    const struct gf4_test_ops *ops = display_ops();
    struct gf_display_context *video = device ? device->context : 0;
    struct gf4_ctx *ctx = video && video->display4 ? video->display4 : ops ? ops->ctx : 0;
    struct gf2_chan *ch = ctx ? ctx->ch : 0;
    if (ch) {
        int last = ctx->last_rc, ch_rc = ch->last_rc;
        unsigned state = ch->state;
        u32 epoch = ch->epoch, seq = ch->seq;
        kprintf(KLOG_ATTR, "[geforce] start rc=%d stage=%s last=%d gf2=%u/%u/%u ch_rc=%d\n",
                rc, stage, last, state, epoch, seq, ch_rc);
    } else if (ctx) {
        int last = ctx->last_rc;
        kprintf(KLOG_ATTR, "[geforce] start rc=%d stage=%s last=%d gf2=na ch_rc=na\n",
                rc, stage, last);
    } else
#endif
        kprintf(KLOG_ATTR, "[geforce] start rc=%d stage=%s last=na gf2=na ch_rc=na\n",
                rc, stage);
    geforce_fail();
}

static int geforce_transfer(int x, int y, int w, int h)
{
#ifdef OS32_GEFORCE_MODULE
    if (selected_front.bytes && (selected_front.fmt != GF3_INDEX8 ||
        selected_front.width != GF_WIDTH || selected_front.height != GF_HEIGHT)) {
        const struct gf4_test_ops *ops = display_ops();
        if (ops) ops->ctx->last_rc = GF4_EFORMAT;
        return OS32_ERR_INVAL;
    }
    if (selected_front.bytes && selected_front.off) {
        /* Only the boot display has the permanent CPU map. A newly reserved
         * VGA INDEX8 scan uses F3's existing RAM upload, not an unchecked
         * P2V cast beyond that map (nouveau_bo.c:669-689, OS32 V0 boundary).
         * The old offset-zero CPU path below remains unchanged. */
        const struct gf4_test_ops *ops = display_ops();
        if (!ops) return OS32_ERR_NOSYS;
        if (!gfx_clip_screen(&x, &y, &w, &h, GF_WIDTH, GF_HEIGHT)) return 0;
        u32 pitch = gfx_backend_geforce.bb_pitch;
        if (!pitch || (u32)y > U32_MAX / pitch) return OS32_ERR_INVAL;
        u32 start = (u32)y * pitch;
        if ((u32)x > U32_MAX - start) return OS32_ERR_INVAL;
        start += (u32)x;
        if (!gfx_backend_geforce.bb_base || start > gfx_backend_geforce.bb_size)
            return OS32_ERR_INVAL;
        struct gf3_ram ram = {gfx_backend_geforce.bb_base + start,
            gfx_backend_geforce.bb_size - start, pitch};
        u32 dl;
        int rc = display_deadline(ops->ctx, &dl);
        if (!rc) rc = gf3_upload(ops->ctx->accel, &selected_front,
            (struct gf3_rect){x, y, w, h}, &ram, false, dl);
        if (!rc) {
            geforce_context->counters->present_bytes += (u32)w * (u32)h;
            geforce_context->counters->commits++;
        }
        ops->ctx->last_rc = rc;
        return rc;
    }
#endif
    return gf_present_rect(gfx_backend_geforce.bb_base, gfx_backend_geforce.bb_pitch,
                           P2V_IO(framebuffer.phys), framebuffer.pitch, P2V_IO(bar0_phys),
                           x, y, w, h, interrupt_point, interrupt_context);
}

static void geforce_init(void)
{
    struct video_mode mode;
    if (!geforce_probe()) return;
    if (active) return; /* gfx_init -> gfx_init_200 preserves the current scan. */
#ifdef OS32_GEFORCE_MODULE
    scanout_logged = false;
#endif
    if (video_mode_info(device, GF_MODE_INDEX, &mode) ||
        video_framebuffer(device, &framebuffer) || !gfx_backend_geforce.bb_base) {
        geforce_fail();
        return;
    }
    geforce_palette_init();
    pc98_console_set_safe(0);
    int rc = video_start(device, mode.id, initial_palette);
    if (rc) { geforce_start_fail("video_start", rc); return; }
    rc = start_wc ? start_wc() : 0;
    if (rc < 0) { geforce_start_fail("wc", rc); return; }
    rc = geforce_transfer(0, 0, GF_WIDTH, GF_HEIGHT);
    if (rc) { geforce_start_fail("transfer", rc); return; }
    rc = pc98_route_cell_check();
    if (rc) { geforce_start_fail("cell", rc); return; }
    rc = pc98_route_to_gf();
    if (rc) { geforce_start_fail("route", rc); return; }
    active = routed = 1;
    gfx_flip_enabled = gfx_display_page = 0;
    gfx_current_height = GF_HEIGHT;
}

int geforce_foundation_display(bool start)
{
    if (!start) return geforce_stop() ? GF1_ESTOP : GF1_OK;
    geforce_init();
    return active && !failed ? GF1_OK : GF1_EIO;
}

static int geforce_query(GFX_ScreenInfo *info)
{
    const GfxMode *mode = gfx_backend_mode(&gfx_backend_geforce);
    if (!info || !mode) return OS32_ERR_INVAL;
    info->width = mode->width; info->height = mode->height;
    info->bpp = mode->format == GFX_FMT_RGB565 ? GF_RGB565_BPP :
        mode->format == GFX_FMT_XRGB8888 ? GF_XRGB8888_BPP : GFX_FMT_PACKED8_BPP;
    info->format = mode->format;
    info->flags = 0; info->lease_mask = 0;
    info->lease_first = GF_LEASE_FIRST; info->lease_count = GF_LEASE_COUNT;
    for (u32 i = 0; i < sizeof(info->reserved) / sizeof(info->reserved[0]); ++i)
        info->reserved[i] = 0;
#ifdef OS32_GEFORCE_MODULE
    if (gfx_backend_geforce.policy == GFX_POLICY_DIRECT) {
        info->flags |= GFX_CAP_DIRECT;
        info->reserved[0] = gfx_policy_gen_get();
    }
#endif
    return 0;
}

static void geforce_present(int x, int y, int w, int h)
{
    if (!active) {
#ifdef OS32_GEFORCE_MODULE
        if (!scanout_logged) {
            scanout_logged = true;
            kprintf(KLOG_ATTR, "[gf4] present fail stage=inactive rc=%d\n", GF4_ESTATE);
        }
#endif
        return;
    }
#ifdef OS32_GEFORCE_MODULE
    int poll = feature5_poll();
    if (poll) {
        kprintf(KLOG_ATTR, "[gf4] present fail stage=poll rc=%d source=%s source_rc=%d\n",
                poll, feature5 && feature5->poll_source ? feature5->poll_source : "other",
                feature5 ? feature5->source_rc : poll);
        scanout_logged = true;
        geforce_fail(); return;
    }
    if (gfx_backend_geforce.policy == GFX_POLICY_DIRECT) {
        /* DIRECT skips transfer, but must retain its device-failure exit.
         * F4 borrows the same channel/accel owner as the drawing hooks. */
        const struct gf4_test_ops *ops = display_ops();
        struct gf4_ctx *ctx = ops ? ops->ctx : 0;
        if ((ctx && ctx->ch && ctx->ch->state != GF2_RUN) ||
            (ctx && ctx->accel && ctx->accel->last_rc) ||
            (interrupt_point && interrupt_point(interrupt_context))) {
            geforce_fail(); /* DIRECT device failure */
            return;
        }
        geforce_context->counters->commits++;
        return;
    }
    u32 before = geforce_context->counters->present_bytes;
#endif
    int rc = geforce_transfer(x, y, w, h);
    if (rc) {
        kprintf(KLOG_ATTR, "[gf4] present fail stage=transfer rc=%d\n", rc);
#ifdef OS32_GEFORCE_MODULE
        scanout_logged = true;
#endif
    }
#ifdef OS32_GEFORCE_MODULE
    if (!rc) geforce_scanout_log(geforce_context->counters->present_bytes - before);
#endif
    if (rc && rc != OS32_ERR_INVAL) geforce_fail();
}

static void geforce_set_palette(int first, int count, const u8 *rgb)
{
    u8 converted[VIDEO_PALETTE_BYTES];
    if (!active || !rgb || first < 0 || count <= 0 || first >= (int)VIDEO_PALETTE_ENTRIES ||
        count > (int)VIDEO_PALETTE_ENTRIES - first) return;
    for (int i = 0; i < count * GFX_RGB_COMPONENTS; ++i)
        converted[i] = (rgb[i] & GFX_PALETTE_COMPONENT_MASK) * PEGC_PAL_KAPI_SCALE;
    if (video_palette(device, first, count, converted)) { geforce_fail(); return; }
    for (int i = 0; i < count && first + i < GF_LEASE_FIRST; ++i)
        palette_shadow_set(first + i, rgb[i * GFX_RGB_COMPONENTS],
                           rgb[i * GFX_RGB_COMPONENTS + 1], rgb[i * GFX_RGB_COMPONENTS + 2]);
}

static void geforce_enter(void)
{
    if (!active && !failed) geforce_init();
    else if (active && !routed) {
        if (pc98_route_to_gf()) geforce_fail();
        else routed = 1;
    }
#ifdef OS32_GEFORCE_MODULE
    gfx_policy_reeval();
#endif
}
static void geforce_leave(void)
{
    if (active && routed && geforce_stop() && geforce_display_failed)
        geforce_display_failed();
}
static void geforce_shutdown(void)
{
    if (active && geforce_stop() && geforce_display_failed) geforce_display_failed();
}

static const GfxMode geforce_modes[] = {
    {GF_WIDTH, GF_HEIGHT, GFX_FMT_PACKED8, VIDEO_RGB_BITS, 0, 0, GF_PITCH}
};
#ifdef OS32_GEFORCE_MODULE
GfxBackend geforce_backend_ops = {
#else
GfxBackend gfx_backend_geforce = {
#endif
    .name = "geforce", .probe = geforce_probe, .init = geforce_init,
    .query = geforce_query, .shutdown = geforce_shutdown,
    .present_rect = geforce_present, .set_palette = geforce_set_palette,
    .enter = geforce_enter, .leave = geforce_leave,
#ifdef OS32_GEFORCE_MODULE
    .direct_ok = geforce_direct_ok, .direct_ops = &gf_direct_ops,
    .set_mode = geforce_set_mode, .dpms = geforce_dpms,
    .cursor_image = geforce_cursor_image, .cursor_move = geforce_cursor_move,
    .cursor_show = geforce_cursor_show, .vsync = geforce_vsync,
    .page_flip = geforce_page_flip, .gamma = geforce_gamma,
#endif
    .bb_pitch = GF_PITCH, .bb_format = GFX_BB_PACKED8,
    .prepare = geforce_prepare, .modes = geforce_modes,
    .mode_count = sizeof(geforce_modes) / sizeof(geforce_modes[0])
};
