/* Synchronous OSRI owner. The backend owns hardware and plan restoration. */
#include "gfx_osri_view.h"
#include "gfx_osri.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "v2/gfx2_int.h"
#include "kmalloc.h"
#include "kstring.h"
#include "con_sink.h"
#include "pcm_contract.h"
#include "serial_diag.h"
#include "../fs/serialfs/serialfs.h"
#include "../exec/appslot.h"

extern volatile u32 tick_count;
static int osri_active;

int gfx_osri_poll_abort(int caller)
{
    AppSlot *slot = appslot_get(caller);
    return (slot && slot->abort_req) || appslot_stop_pending();
}

int gfx_osri_view(const char *path, u32 duration_ms, int caller)
{
    if (!gfx_is_started() || osri_active ||
        (con_sink_is_enabled() && caller != appslot_gfx_owner()) ||
        serial_gate_active() || serialfs_is_mounted() || pcm_state() != PCM_ST_CLOSED)
        return OS32_ERR_BUSY;
    if (!g_backend || !g_backend->osri_show) return OS32_ERR_NOSYS;
    struct gfx_kernel_fb fb;
    if (gfx_kernel_framebuffer(&fb) || fb.width != GFX_WIDTH ||
        fb.height != GFX_HEIGHT || fb.pitch != GFX_BPL || fb.format != GFX_BB_PLANAR4)
        return OS32_ERR_NOSYS;
    (void)v1_bridge_begin(GFX2_ROLE_CLIENT, GFX2_CPU_W);
    (void)v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_W);
    osri_active = 1;
    struct OsriViewRun run = { .caller = caller, .start = tick_count, .duration_ms = duration_ms };
    struct OsriImage *image = kmalloc(sizeof(*image));
    int rc = OS32_ERR_NOSPC;
    if (image) {
        rc = osri_load_checked(path, &fb, image);
        if (!rc) rc = g_backend->osri_show(image, &run);
        kfree(image);
    }
    /* A failed loader may have partially overwritten the borrowed BB. */
    for (u32 i = 0; i < GVRAM_PLANES; i++) kmemset(fb.planes[i], 0, GFX_PLANE_SZ);
    dirty_queue.count = 0;
    prev_dirty.count = 0;
    v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_W, 0);
    v1_bridge_end(GFX2_ROLE_CLIENT, GFX2_CPU_W, 0);
    osri_active = 0;
    return rc;
}
