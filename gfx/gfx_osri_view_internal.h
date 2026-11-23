#ifndef GFX_OSRI_VIEW_INTERNAL_H
#define GFX_OSRI_VIEW_INTERNAL_H

#include "gfx_osri_view.h"
#include "con_sink.h"
#include "platform_osri.h"
#include "serial_diag.h"
#include "../exec/appslot.h"

static int osri_active;

int gfx_osri_poll_abort(int caller)
{
    AppSlot *slot = appslot_get(caller);
    return (slot && slot->abort_req) || appslot_stop_pending();
}

/* Keep the short-circuit expression at the call site for PC-98 image identity. */
#define OSRI_VIEW_BUSY(caller) (!gfx_is_started() || osri_active || \
    (con_sink_is_enabled() && (caller) != appslot_gfx_owner()) || \
    serial_gate_active() || platform_osri_mount_busy() || platform_osri_pcm_busy())

#endif
