/* backend_pc98_osri.c — gfx_osri_show の pc98 GDC 640x400 planar4 の hook (KAPI v80)。
 * backend_pc98.c の表の .osri_show から呼ばれる。正典 docs/decisions/gui/OSRI_VIEWER_DESIGN.md §3。 */
#include "../../../gfx/gfx_internal.h"
#include "gfx_hal.h"
#include "os32_kapi_shared.h"
#include "../../../gfx/gfx_osri_view.h"
#include "splash_raster_run.h"
#include "config.h"
#include "kmalloc.h"
#include "input_service.h"
#include "con_sink.h"
#include "kbd.h"

int pc98_osri_show(const struct OsriImage *image, struct OsriViewRun *ctl);

/* OSRI owns all 16 colors and temporarily suppresses text attributes. */
#define OSRI_TEXT_CELLS (TVRAM_COLS * TVRAM_ROWS)
#define OSRI_POLL_KEYS 32u /* Bound work at one IRQ-open frame boundary. */
#define OSRI_ESC_ASCII 0x1b
extern volatile u32 tick_count;
struct Pc98OsriPoll { struct PlRun *run; int caller; };

static int pc98_osri_poll(void *ctx)
{
    struct Pc98OsriPoll *p = ctx;
    if (gfx_osri_poll_abort(p->caller)) return 1;
    int gui = con_sink_is_enabled();
    for (u32 i = 0; i < OSRI_POLL_KEYS; i++) {
        if (gui) {
            /* WM cannot forward keys while this synchronous call owns display. */
            int raw = input_trygetrawkey();
            if (raw < 0) break;
            if (!(raw & KBD_RAW_MAKE)) continue;
            if ((raw & KBD_RAW_KEY_MAKE_MASK) == (KEY_ESC | KBD_RAW_MAKE)) return 1;
        } else {
            if (input_peekkey() < 0) break;
            int key = input_trygetkey();
            if (key < 0) break;
            if ((key & 0xff) == OSRI_ESC_ASCII) return 1;
        }
        if (p->run) pl_run_extend(p->run, tick_count,
                                  GFX_OSRI_SHOW_EXTEND_MS * SPLASH_RASTER_US_PER_MS);
    }
    return 0;
}

static void pc98_osri_black(void)
{
    for (u32 i = 0; i < OSRI_COLORS; i++) palette_set(i, 0, 0, 0);
}

int pc98_osri_show(const struct OsriImage *image, struct OsriViewRun *ctl)
{
    struct SplashRaster *state = pc98_splash_raster_state();
    if (gfx_current_height != GFX_HEIGHT || !_irq_enabled() || !state ||
        !state->checked || state->mode > SPLASH_RASTER_31K ||
        (image->count && pl_run_timing(state))) return OS32_ERR_NOSYS;
    struct PlV2Plan *plan = 0;
    if (image->count) {
        plan = kmalloc(sizeof(*plan));
        if (!plan) return OS32_ERR_NOSPC;
        if (pl_v2_prepare(image, plan)) { kfree(plan); return OS32_ERR_NOSYS; }
    }
    u16 *text = kmalloc(OSRI_TEXT_CELLS * sizeof(*text));
    if (!text) { if (plan) kfree(plan); return OS32_ERR_NOSPC; }
    PaletteEntry colors[OSRI_COLORS];
    for (u32 i = 0; i < OSRI_COLORS; i++)
        palette_get(i, &colors[i].r, &colors[i].g, &colors[i].b);
    volatile u16 *attrs = P2V_IO(TVRAM_ATTR_BASE);
    for (u32 i = 0; i < OSRI_TEXT_CELLS; i++) { text[i] = attrs[i]; attrs[i] = 0; }
    pc98_osri_black();
    gfx_add_dirty_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);
    gfx_present_nosync();
    int rc = OS32_ERR_AGAIN;
    struct PlRun run;
    struct Pc98OsriPoll poll = { .run = plan ? &run : 0, .caller = ctl->caller };
    if (plan) {
        u32 now = tick_count;
        unsigned long long prefix = (unsigned long long)(now - ctl->start) * PL_RUN_TICK_US;
        u32 budget = ctl->duration_ms * SPLASH_RASTER_US_PER_MS;
        u32 remaining = prefix < budget ? budget - (u32)prefix : 0;
        pl_run_init(&run, state, now, remaining);
        while (pl_run_more(&run, tick_count)) {
            int frame = pl_v2_frame_poll(state, image, plan, pc98_osri_poll, &poll);
            if (frame == PL_FRAME_CANCEL) { rc = GFX_OSRI_SHOW_CANCELLED; goto done; }
            if (frame == PL_FRAME_NO_SYNC) goto done;
            pl_run_charge(&run, frame);
        }
        if (run.good) rc = GFX_OSRI_SHOW_DONE;
    } else {
        /* Fixed images have no deadline. Each finite VSYNC wait keeps IF open. */
        for (;;) {
            if (pc98_splash_raster_frame(state, image)) break;
            if (pc98_osri_poll(&poll)) { rc = GFX_OSRI_SHOW_CANCELLED; break; }
        }
    }
done:
    pc98_osri_black();
    gfx_clear_planar_pages(GFX_PLANE_SZ);
    _out(GDC_ACCESS_PAGE, gfx_flip_enabled ? 1 - gfx_display_page : GDC_PAGE_0);
    for (u32 i = 0; i < OSRI_COLORS; i++)
        palette_set(i, colors[i].r, colors[i].g, colors[i].b);
    for (u32 i = 0; i < OSRI_TEXT_CELLS; i++) attrs[i] = text[i];
    kfree(text);
    if (plan) kfree(plan);
    return rc;
}

