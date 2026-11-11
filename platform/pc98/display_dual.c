#include "kernel/console/console_dual.h"
#include "gfx/v2/gfx2_int.h"
#include "gfx/gfx_internal.h"
#include "gfx/gfx_osri_view.h"
#include "drivers/video/pc98/pc98_route.h"
#include "display_private.h"
#include "io.h"
#include "kprintf.h"

_Static_assert(CON_OUT_MAX >= 1 + GFX2_MAX_ADAPTERS *
               (sizeof(((struct gfx2_adapter *)0)->head) / sizeof(((struct gfx2_adapter *)0)->head[0])),
               "console output table too small");
_Static_assert(CON_CONN_UNKNOWN == GFX2_CONN_UNKNOWN &&
               CON_CONN_ON == GFX2_CONN_ON && CON_CONN_OFF == GFX2_CONN_OFF,
               "connector status values differ");

static void display_dual_text_on(void)
{
    outp(GDC_GFX_CMD, GDC_CMD_STOP);
    io_wait();
    outp(MODE_FF1_PORT, MFF1_DISP_ON);
    io_wait();
    outp(GDC_TEXT_CMD, GDC_CMD_START);
    io_wait();
}

u32 display_dual_key(void)
{
    struct gfx2_adapter *a = gfx2_active();
    u32 k = 0;
    k = k * 31 + gfx_is_started();
    k = k * 31 + gfx_init_gen;
    if (!a) return k;
    k = k * 31 + a->index;
    k = k * 31 + a->gen;
    k = k * 31 + a->state;
    k = k * 31 + a->flags;
    for (u32 h = 0; h < a->heads; h++) {
        GFX2_HeadInfo v = {0};
        gfx2_head_info(a->index, h, &v);
        k = k * 31 + v.attached;
        k = k * 31 + v.connector_mask;
        k = k * 31 + v.mode_id;
        k = k * 31 + v.scanout_sid;
    }
    return k;
}

int display_dual_outputs(struct con_out *o, int max)
{
    static int warned;
    struct gfx2_adapter *a = gfx2_active();
    if (!gfx_is_started() || max <= 0 || !o) return 0;
    if (!a || a->kind != GFX2_KIND_GEFORCE) {
        if (con_dual_cfg() && !warned) {
            warned = 1;
            kprintf(KLOG_ATTR, "[con] CONSOLE_DUAL: %s is not a target -> off\n",
                    a && a->v1 && a->v1->name ? a->v1->name : "unknown");
        }
        return 0;
    }
    if (!pc98_route_safe() || pc98_route_cell_check()) return 0;
    display_dual_text_on();
    u32 pc98 = 0;
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++) {
        struct gfx2_adapter *p = gfx2_adapter_get(i);
        if (p && p->kind == GFX2_KIND_PC98) { pc98 = p->index; break; }
    }
    o[0] = (struct con_out){pc98, 0, 0, GFX2_CONN_PC98_ANALOG,
                            CON_CONN_ON, 1, CON_OUT_TEXT, 1};
    int n = 1;
    for (u32 h = 0; h < a->heads && n < max; h++) {
        GFX2_HeadInfo v = {0};
        gfx2_head_info(a->index, h, &v);
        u8 conn = 0xff;
        for (u32 bit = 0; bit < 32; bit++)
            if (v.connector_mask & (1UL << bit)) { conn = bit; break; }
        u8 role = a->state == GFX2_AD_OK && v.attached ? CON_OUT_DESK : CON_OUT_NONE;
        o[n++] = (struct con_out){a->index, h, conn, GFX2_CONN_UNKNOWN,
                                  CON_CONN_UNKNOWN, !!v.attached, role, role == CON_OUT_DESK};
    }
    return n;
}

/* Explicit diagnostics may perform connector DDC I/O. */
int display_dual_diag(struct con_out *o, int max)
{
    int n = display_dual_outputs(o, max);
    for (int i = 1; i < n; i++) {
        GFX2_ConnectorInfo v = {0};
        if (o[i].connector != 0xff &&
            !gfx2_connector_info(o[i].adapter, o[i].connector, &v)) {
            o[i].conn_type = v.type;
            o[i].conn_status = v.status;
        }
    }
    return n;
}
