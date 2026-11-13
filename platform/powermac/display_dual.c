#include "kernel/console/console_dual.h"
#include "gfx/v2/gfx2_int.h"
#include "drivers/video/of_fb/backend_of_fb.h"

u32 display_dual_key(void)
{
    struct gfx2_adapter *a = gfx2_active();
    return of_fb_ready() + (a ? a->gen * 4 + a->state * 2 : 0);
}
int display_dual_outputs(struct con_out *out, int max)
{
    struct gfx2_adapter *a = gfx2_active();
    if (!out || max <= 0) return 0;
    int live = of_fb_ready() && a && a->kind == GFX2_KIND_OF_FB && a->state == GFX2_AD_OK;
    out[0] = (struct con_out){.adapter = a ? a->index : 0,
        .conn_status = CON_CONN_ON, .running = live, .role = CON_OUT_TEXT, .live = live};
    return 1;
}
int display_dual_diag(struct con_out *out, int max) { return display_dual_outputs(out, max); }
