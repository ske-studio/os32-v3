/* HAL boundary; ScreenInit/EnterVT/LeaveVT/CloseScreen live in lifecycle.c.
 * MIT reference notices and source correspondence are in trident.h.
 * Loader calls prepare only after validating and pinning module text/data.
 */
#include "trident_module.h"
#include "gfx/gfx_internal.h"
#include "lib/serial_diag.h"

static struct trident_device device;

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
    if (device.last_rc) return device.last_rc;
    const TridentModuleContext *c = trident_context;
    if (!c || !c->backend || !c->counters || !c->resources || !c->arm ||
        !c->height || !c->flip || !c->page || !c->display_owner)
        return remember(TRI_STEP_RESOURCES, TRI_E_INVAL);
    const struct trident_resources *r = c->resources;
    if (!r->ready || !r->regs || !r->lfb || !r->client ||
        r->client_bytes < TRI_DISPLAY_BYTES || r->mapped_bytes < TRI_DISPLAY_BYTES ||
        r->decode_bytes < TRI_DISPLAY_BYTES || r->regs_bytes < TRI_REG_MAPPED_BYTES)
        return remember(TRI_STEP_RESOURCES, TRI_E_RESOURCE);
    int rc = trident_pci_validate(&r->pci);
    if (rc) return remember(TRI_STEP_RESOURCES, rc);
    device.res = r;
    rc = trident_regs_bind(&device); /* descriptor-only, no hardware access */
    if (rc) return remember(TRI_STEP_RESOURCES, rc);
    device.pinned = true; /* loader's prepare boundary guarantees the pin */
    if (!device.armed) device.step = TRI_STEP_PIN;
    c->backend->bb_base = r->client;
    c->backend->bb_size = r->client_bytes;
    return 0;
}
static void prepare(void) { int rc = trident_backend_prepare(); report(device.failed_step, rc); }
static int probe(void) { return !device.last_rc && device.pinned && device.res && device.res->ready; }

static void finish(u32 stage, int rc, bool recover)
{
    if (!rc) return;
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
    int rc = trident_backend_prepare();
    if (rc) { finish(device.failed_step, rc, false); return; }
    if (!device.armed) {
        device.display_owner = (u32)trident_context->display_owner();
        rc = trident_context->arm(trident_restore98, &device, device.display_owner);
        if (rc) { finish(TRI_STEP_ARM, rc, false); return; }
        device.armed = true;
        device.step = TRI_STEP_ARM;
    }
    rc = trident_screen_init(&device);
    if (rc) { finish(device.failed_step, rc, true); return; }
    *trident_context->height = TRI_HEIGHT;
    *trident_context->flip = *trident_context->page = 0;
}
static int query(GFX_ScreenInfo *info)
{
    if (device.last_rc) return device.last_rc;
    if (!info) return TRI_E_INVAL;
    if (!probe()) return TRI_E_STATE;
    *info = (GFX_ScreenInfo){0};
    info->width = TRI_WIDTH; info->height = TRI_HEIGHT;
    info->bpp = GFX_FMT_PACKED8_BPP; info->format = GFX_FMT_PACKED8;
    info->lease_first = GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP);
    info->lease_count = TRI_DAC_COLORS - info->lease_first;
    return 0;
}
static void enter(void)
{
    int rc = trident_enter(&device);
    finish(device.failed_step ? device.failed_step : TRI_STEP_MODE, rc, true);
}
/* No serial/console output inside the emergency callback trident_restore98. */
static void leave(void) { int rc = trident_leave(&device); finish(device.failed_step, rc, false); }
static void shutdown(void) { int rc = trident_close(&device); finish(device.failed_step, rc, false); }
static void present(int x, int y, int w, int h)
{
    if (device.last_rc || !device.active) return;
    int rc = trident_present(&device, x, y, w, h);
    if (rc) { finish(TRI_STEP_ACTIVE, rc, true); return; }
    if (gfx_clip_screen(&x, &y, &w, &h, TRI_WIDTH, TRI_HEIGHT)) {
        trident_context->counters->present_bytes += (u32)w * (u32)h;
        ++trident_context->counters->commits;
    }
}
static void set_palette(int first, int count, const u8 *rgb)
{
    if (device.last_rc || !device.active) return;
    int rc = trident_palette(&device, first, count, rgb);
    if (rc) { finish(TRI_STEP_CLIENT, rc, true); return; }
    for (int i = 0; i < count && first + i < (int)GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP); ++i)
        palette_shadow_set(first + i, rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
}
static const GfxMode modes[] = {
    {TRI_WIDTH, TRI_HEIGHT, GFX_FMT_PACKED8, 6, 0, 0, TRI_PITCH}
};
GfxBackend trident_backend_ops = {
    .name = "trident", .probe = probe, .prepare = prepare, .init = init,
    .query = query, .shutdown = shutdown, .present_rect = present,
    .set_palette = set_palette, .enter = enter, .leave = leave,
    .bb_pitch = TRI_PITCH, .bb_format = GFX_BB_PACKED8,
    .modes = modes, .mode_count = sizeof(modes) / sizeof(modes[0])
};
