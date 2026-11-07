/* MIT reference notices in trident.h. trident_driver-1.4.0.c:1201-1247,
 * 2920-2969,3333-3348,3471-3488; vgaHW.c:1786-1789.
 * PM 2026-11-06: PreInit capacity read precedes Save/PC98 Init, unlike the
 * original S3 contract. PM fix6: every HAL init is ScreenInit, so Save and
 * PC98 Init repeat (D:3472-3487).
 * 0xFAC is shared with pc98_route.c (mask 3): Trident owns values 2/0 while
 * active/returning. Keep Enable/Disable's unconditional OUTB and ordering;
 * route_set's conditional write/readback cannot replace this sequence.
 * Cooperative switching serializes the backends. No 98 drawing here.
 */
#include "trident.h"

static int failed(struct trident_device *d, u32 step, int rc)
{
    if (rc && !d->last_rc) {
        d->failed_step = step;
        d->last_rc = rc;
    }
    return rc;
}

#define STAGE(stage, call) do { \
    int rc = (call); \
    if (rc) return failed(d, (stage), rc); \
    d->step = (stage); \
} while (0)

static int mode_enable(struct trident_device *d)
{
    /* vtSema covers partial mode programming, not just a published screen. */
    d->closed = d->leave_done = false;
    const struct tri_mode *mode = d->mode.valid ? &d->mode.selected :
                                  &tri_modes[TRI_MODE_DEFAULT_ID - 1u];
    STAGE(TRI_STEP_MODE, tri_mode_set(d, mode));
    STAGE(TRI_STEP_ENABLE, trident_route_enable(d));
    return 0;
}

/* D:3534-3546,3637-3661: framebuffer and offscreen manager precede
 * accelerator/cursor setup. Release old consumers before resetting F5's
 * bookkeeping; the core callback owns the surface/lease replacement. */
static int client_init(struct trident_device *d)
{
    int rc = tri_video_stop(d, true);
    if (rc) return rc;
    rc = tri_vram_free(d, &d->accel.pattern);
    if (rc) return rc;
    d->accel.initialized = false;
    rc = tri_vram_free(d, &d->cursor.allocation);
    if (rc) return rc;
    d->cursor.available = false;
    rc = tri_vram_init(d, &d->mode.selected);
    if (rc) return rc;
    if (!d->res->configure) return TRI_E_STATE;
    rc = d->res->configure(&d->mode.selected, tri_vram_display_max_bytes(d));
    if (rc) return rc;
    d->mode.display = d->res->display;
    d->mode.client = d->res->client_surface;
    d->mode.generation = d->res->generation;
    for (u32 i = 0; i < d->mode.client.bytes; ++i) {
        if (d->res->ram_fallback) d->res->client[i] = 0;
        else {
            rc = trident_write8(d, TRI_SPACE_LFB, d->mode.client.off + i, 0);
            if (rc) return rc;
        }
    }
    return TRI_OK;
}

/* D:3692,3700-3703: optional initialization never rejects ScreenInit.
 * Keep the failure diagnostic, but publish only usable component features. */
static void accel_init(struct trident_device *d)
{
    int rc = tri_accel_init(d, &d->mode.selected);
    if (rc) {
        failed(d, TRI_STEP_CLIENT, rc);
        d->accel.initialized = false;
        d->mode.no_accel = true;
    }
    if (!d->accel.initialized) d->mode.features &= ~TRI_CAP_GE;
    if (!d->accel.pattern.allocated) d->mode.features &= ~TRI_CAP_MONO_PATTERN;
}

int trident_screen_init(struct trident_device *d)
{
    if (!d || !d->res || !d->io) return TRI_E_INVAL;
    if (!d->armed || !d->pinned || !d->res->ready) return TRI_E_STATE;
    if (d->returning) return TRI_E_BUSY;
    d->last_rc = d->failed_step = d->restore_rc = d->disable_rc = 0;
    d->screen_ready = false;
    if (!d->preinit_done) {
        u8 misc;
        STAGE(TRI_STEP_IDENTIFY, trident_identify(d));
        STAGE(TRI_STEP_CAPACITY_PLAN, trident_read8(d, TRI_SPACE_REGS, TRI_MISC_READ, &misc));
        d->cur_vga_base = (misc & 1u) ? TRI_CR_COLOR : TRI_CR_MONO;
        STAGE(TRI_STEP_CAPACITY_PLAN, trident_capacity_plan(d));
        d->preinit_done = true;
    }
    d->closed = d->leave_done = false;
    STAGE(TRI_STEP_SAVE, trident_state_save(d));
    d->init_done = false;
    STAGE(TRI_STEP_PC98_INIT, trident_pc98_init(d));
    d->init_done = true;
    int rc = mode_enable(d);
    if (rc) return rc;
    STAGE(TRI_STEP_MODE, trident_screen_blank(d, true));
    STAGE(TRI_STEP_MODE, trident_adjust_frame(d));
    rc = client_init(d);
    if (rc) return failed(d, TRI_STEP_CLIENT, rc);
    d->step = TRI_STEP_CLIENT;
    accel_init(d);
    rc = tri_cursor_init(d);
    if (rc) {
        failed(d, TRI_STEP_CLIENT, rc);
        d->cursor.available = false;
    }
    if (!d->cursor.available) d->mode.features &= ~TRI_CAP_CURSOR;
    STAGE(TRI_STEP_CLIENT, tri_palette_load_default(d));
    /* D:3741 DPMS registration is F10's static HAL hook; no automatic On. */
    rc = trident_video_reset(d);
    d->video.last_rc = rc;
    if (rc) {
        failed(d, TRI_STEP_CLIENT, rc);
        d->video.polling = false;
        d->mode.features &= ~TRI_CAP_VIDEO;
    }
    d->screen_ready = true;
    d->step = TRI_STEP_ACTIVE;
    return 0;
}

int trident_enter(struct trident_device *d)
{
    if (!d) return TRI_E_INVAL;
    if (d->returning) return TRI_E_BUSY;
    if (!d->screen_ready) return TRI_E_STATE;
    d->last_rc = d->failed_step = 0;
    u32 features = d->mode.features;
    int rc = mode_enable(d);
    if (rc) return rc;
    /* Enter does not reinitialize cursor/video: retain their availability. */
    d->mode.features &= features | TRI_CAP_GE;
    accel_init(d);
    STAGE(TRI_STEP_CLIENT, tri_palette_reload(d));
    d->step = TRI_STEP_ACTIVE;
    return TRI_OK;
}

static int restore_lock(struct trident_device *d)
{
    int restore = 0;
    if (d->saved.valid) {
        restore = trident_state_restore(d);
        if (restore) failed(d, TRI_STEP_RESTORE, restore);
        else d->step = TRI_STEP_RESTORE;
    }
    /* CloseScreen locks under vtSema even when Save never completed (D:3348). */
    int lock = trident_crtc_lock(d, d->cur_vga_base, true);
    if (lock) failed(d, TRI_STEP_LOCK, lock);
    else d->step = TRI_STEP_LOCK;
    if (!restore) restore = lock;
    d->restore_rc = restore;
    return restore;
}

static int disable(struct trident_device *d)
{
    d->disable_rc = trident_route_disable(d);
    if (d->disable_rc) failed(d, TRI_STEP_DISABLE, d->disable_rc);
    else d->step = TRI_STEP_DISABLE;
    return d->disable_rc;
}

static int return98(struct trident_device *d, bool close)
{
    if (!d) return TRI_E_INVAL;
    if (d->returning) return TRI_E_BUSY;
    if (!d->active) {
        if (close) { d->closed = true; d->step = TRI_STEP_CLOSED; }
        return d->restore_rc ? d->restore_rc : d->disable_rc;
    }
    d->returning = true;
    int first = tri_sync(d);
    if (first) failed(d, TRI_STEP_ACTIVE, first);
    /* F4: remove cooperative polling even if the stop itself fails. */
    d->video.polling = false;
    int stop = tri_video_stop(d, true);
    if (stop) failed(d, TRI_STEP_ACTIVE, stop);
    if (!first) first = stop;
    int second, third;
    if (close) {
        second = disable(d);
        third = restore_lock(d);
    } else {
        second = restore_lock(d);
        third = disable(d);
    }
    d->active = false; /* state_restore requires vtSema until it returns */
    d->returning = false;
    d->leave_done = !close;
    d->closed = close;
    if (close && !first && !second && !third) d->step = TRI_STEP_CLOSED;
    return first ? first : (second ? second : third);
}

int trident_leave(struct trident_device *d) { return return98(d, false); }
int trident_close(struct trident_device *d) { return return98(d, true); }
int trident_restore98(void *opaque) { return trident_close(opaque); }
