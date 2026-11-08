#include "debug_probe.h"
#include "init/sysconfig.h"
#include "kernel/time/cpu_calibrate.h"
/* Resident loans and pin precede prepare/arm; no hardware initialization here. */
#include "trident_module.h"
#include "trident_imports.h"
#include "kernel/module/module_runtime.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "gfx_internal.h"
#include "gfx/gfx_emergency.h"
#include "exec/appslot.h"
#include "kprintf.h"
#include "lib/kstring.h"

extern const u32 trident_kexports[];
extern const struct module_symbol_contract trident_import_contracts[], trident_export_contracts[];
/* F10's VGA DAC width, independent of the framebuffer pixel depth. */
#define TRI_MODULE_DAC_BITS 6u
static struct module_handle handle;
static GfxBackend candidate;
static bool pinned, armed;
static u32 restore_entry;
static const struct TridentDiagnosticOps *diagnostic_candidate, *diagnostic_slot;
const struct TridentDiagnosticOps *trident_diagnostic_ops(void)
{
    return diagnostic_slot;
}
static int missing_probe(void) { return 0; }
GfxBackend gfx_backend_trident = {.name="trident-unavailable", .probe=missing_probe};
static int arm(int (*restore)(void *), void *opaque, u32 owner)
{
    if (!pinned || (u32)(uptr)restore != restore_entry) return TRI_E_STATE;
    int rc = gfx_arm_emergency(restore, opaque, owner);
    if (!rc) armed = true;
    return rc;
}
static TridentModuleContext context = {
    .backend=&candidate, .counters=&gfx_counters, .arm=arm,
    .height=&gfx_current_height, .flip=&gfx_flip_enabled, .page=&gfx_display_page,
    .display_owner=appslot_gfx_owner, .size=TRIDENT_MODULE_CONTEXT_SIZE,
    .diagnostic_out=&diagnostic_candidate
};
static int resident_ro(const struct module_record *r, const void *p, u32 bytes)
{
    u32 a = (u32)(uptr)p;
    return a >= r->ro && bytes <= r->ro_size && a - r->ro <= r->ro_size - bytes;
}
/* The runtime retains RO separately, but modes and the production device
 * live in DATA/BSS. The body allocation excludes the container prefix. */
static int resident_data(const struct module_record *r, const void *p, u32 bytes)
{
    u32 a = (u32)(uptr)p;
    if (r->prefix_pages > r->pages) return 0;
    u32 size = (r->pages - r->prefix_pages) * MODULE_PAGE_SIZE;
    return a >= r->ro && a >= r->base && bytes <= size &&
           a - r->base <= size - bytes;
}
static int modes_valid(const struct module_record *r, bool prepared)
{
    if (!candidate.mode_count || candidate.mode_count > TRI_MODE_COUNT ||
        candidate.default_mode >= candidate.mode_count ||
        !resident_data(r, candidate.modes, candidate.mode_count * sizeof(GfxMode)))
        return 0;
    if (candidate.selected_mode) {
        u32 selected = (u32)(uptr)candidate.selected_mode;
        u32 first = (u32)(uptr)candidate.modes;
        if (selected < first || (selected - first) % sizeof(GfxMode) ||
            (selected - first) / sizeof(GfxMode) >= candidate.mode_count) return 0;
    }
    const struct trident_resources *res = context.resources;
    for (u32 i = 0; i < candidate.mode_count; ++i) {
        const GfxMode *m = &candidate.modes[i];
        u32 cpp;
        switch (m->format) {
        case GFX_FMT_PACKED8: cpp = 1; break;
        case GFX_FMT_RGB565: cpp = 2; break;
        case GFX_FMT_XRGB8888: cpp = 4; break;
        default: return 0;
        }
        /* F10 publishes the six-bit VGA DAC and packed byte pitches. */
        if (!m->width || !m->height || m->dac_bits != TRI_MODULE_DAC_BITS || m->reserved ||
            m->pitch < (u32)m->width * cpp || m->pitch % cpp ||
            m->pitch > res->mapped_bytes / m->height ||
            m->pitch > res->decode_bytes / m->height) return 0;
    }
    const GfxMode *selected = gfx_backend_mode(&candidate);
    /* F10 keeps the legacy BB marker; the mode format carries the depth. */
    if (candidate.bb_pitch != selected->pitch || candidate.bb_format != GFX_BB_PACKED8)
        return 0;
    if (!prepared && !candidate.bb_base && !candidate.bb_size) return 1;
    return candidate.bb_base == res->client && candidate.bb_size == res->client_bytes &&
           candidate.bb_base && selected->pitch <= candidate.bb_size / selected->height &&
           (!res->client_surface.pitch || res->client_surface.pitch == selected->pitch);
}
static int diagnostics_valid(const struct module_record *r)
{
    const struct TridentDiagnosticOps *d = diagnostic_candidate;
    if (!d) return 1; /* Optional diagnostics do not gate display startup. */
    if (!resident_ro(r, d, sizeof(*d)) ||
        d->version != TRIDENT_DIAGNOSTIC_OPS_VERSION ||
        d->size != TRIDENT_DIAGNOSTIC_OPS_SIZE ||
        !resident_data(r, d->context, sizeof(*d->context))) return 0;
    const u32 entries[] = {
        (u32)(uptr)d->set_mode, (u32)(uptr)d->dpms,
        (u32)(uptr)d->cursor_image, (u32)(uptr)d->cursor_move,
        (u32)(uptr)d->cursor_show, (u32)(uptr)d->sync,
        (u32)(uptr)d->clear_sync, (u32)(uptr)d->copy_direction,
        (u32)(uptr)d->line_setup, (u32)(uptr)d->fill,
        (u32)(uptr)d->copy, (u32)(uptr)d->pattern,
        (u32)(uptr)d->line, (u32)(uptr)d->pattern_upload,
        (u32)(uptr)d->video_query, (u32)(uptr)d->video_put,
        (u32)(uptr)d->video_set_key, (u32)(uptr)d->video_stop,
        (u32)(uptr)d->poll, (u32)(uptr)d->video_surface_alloc,
        (u32)(uptr)d->video_surface_display, (u32)(uptr)d->video_surface_stop,
        (u32)(uptr)d->video_surface_free, (u32)(uptr)d->present, (u32)(uptr)d->set_gap
    };
    for (u32 i = 0; i < sizeof(entries)/sizeof(entries[0]); ++i)
        if (entries[i] && !module_text_contains(r, entries[i])) return 0;
    return 1;
}
static int start(struct module_record *r, u32 entry, u32 *exports)
{
    /* Runtime validates the exported facade before dispatching start. */
    if (!exports || exports[0] != TRIDENT_MODULE_OPS_VERSION || entry != exports[1]) return MODULE_INIT_FAILED;
    for (u32 i=1; i<=3; ++i)
        if (!module_text_contains(r, exports[i])) return MODULE_INIT_FAILED;
    int (*initialize)(GfxBackend *, const TridentModuleContext *) =
        (int (*)(GfxBackend *, const TridentModuleContext *))(uptr)entry;
    int rc = initialize(&candidate, &context);
    if (rc) {
        kprintf(KLOG_ATTR, "[trident] stage=context rc=%d\n", rc);
        return rc;
    }
    const u32 required[] = {
        (u32)(uptr)candidate.probe, (u32)(uptr)candidate.init,
        (u32)(uptr)candidate.query, (u32)(uptr)candidate.shutdown,
        (u32)(uptr)candidate.present_rect, (u32)(uptr)candidate.set_palette,
        (u32)(uptr)candidate.enter, (u32)(uptr)candidate.leave,
        (u32)(uptr)candidate.prepare
    };
    for (u32 i=0; i<sizeof(required)/sizeof(required[0]); ++i)
        if (!module_text_contains(r, required[i])) return MODULE_INIT_FAILED;
    const u32 optional[] = {
        (u32)(uptr)candidate.fill_rect, (u32)(uptr)candidate.blit,
        (u32)(uptr)candidate.set_mode, (u32)(uptr)candidate.dpms,
        (u32)(uptr)candidate.cursor_image, (u32)(uptr)candidate.cursor_move,
        (u32)(uptr)candidate.cursor_show, (u32)(uptr)candidate.vsync,
        (u32)(uptr)candidate.page_flip, (u32)(uptr)candidate.gamma,
        (u32)(uptr)candidate.osri_show
    };
    for (u32 i=0; i<sizeof(optional)/sizeof(optional[0]); ++i)
        if (optional[i] && !module_text_contains(r, optional[i])) return MODULE_INIT_FAILED;
    if (!resident_ro(r, candidate.name, sizeof("trident")) || !modes_valid(r, false))
        return MODULE_INIT_FAILED;
    if (kstrcmp(candidate.name, "trident")) return MODULE_INIT_FAILED;
    restore_entry = exports[3];
    if (!module_ref_acquire(handle, r->owner, MODULE_REF_REGISTRATION, 0))
        return MODULE_INIT_FAILED;
    pinned = true;
    const struct probe_decl *probe_table = 0;
    u32 probe_count = 0;
    if (!module_text_contains(r, exports[4])) return MODULE_INIT_FAILED;
    probe_get_decls_fn get_decls = (probe_get_decls_fn)(uptr)exports[4];
    if (get_decls(&probe_table, &probe_count) ||
        debug_probe_register(handle, probe_table, probe_count)) return MODULE_INIT_FAILED;
    int (*prepare)(void) = (int (*)(void))(uptr)exports[2];
    rc = prepare(); /* real bind and descriptor checks, no indexed access */
    if (!rc && (!modes_valid(r, true) || !diagnostics_valid(r))) rc = MODULE_INIT_FAILED;
    if (!rc) diagnostic_slot = diagnostic_candidate;
    else diagnostic_candidate = diagnostic_slot = NULL;
    if (rc) kprintf(KLOG_ATTR, "[trident] stage=prepare rc=%d\n", rc);
    return rc;
}
static int detach(struct module_record *r)
{
    (void)r;
    if (debug_probe_unregister(handle)) return 0;
    if (armed) return 0;
    diagnostic_candidate = diagnostic_slot = NULL;
    gfx_backend_trident = (GfxBackend){.name="trident-unavailable", .probe=missing_probe};
    if (pinned) {
        module_ref_release(handle, MODULE_REF_REGISTRATION);
        pinned = false;
    }
    return trident_resources_rollback(false) == 0;
}
int trident_module_load(void)
{
    if (handle.generation) return MODULE_BUSY;
    diagnostic_candidate = diagnostic_slot = NULL;
    context.resources = trident_resources_get();
    if (!context.resources) {
        kprintf(KLOG_ATTR, "[trident] stage=resources rc=%d\n", TRI_E_RESOURCE);
        return TRI_E_RESOURCE;
    }
    int gap_ns = sysconfig_get_int(SYS_SYSTEM_CFG, "TRI_WRITE_GAP", TRI_WRITE_GAP_DEFAULT_NS);
    context.write_gap_ns = gap_ns < 0 ? TRI_WRITE_GAP_DEFAULT_NS : (u32)gap_ns;
    context.write_gap_lpt = cpu_loops_per_tick();
    const struct boot_common *bi = boot_common_get();
    const struct module_request request = {
        .path=SYS_TRIDENT_MOD, .name="trident",
        .policy={KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, TRIDENT_MODULE_ABI_GEN,
            bi->image_crc, TRIDENT_MODULE_OPS_VERSION, bi->image_valid,
            TRIDENT_IMPORT_COUNT, trident_import_contracts, trident_export_contracts,
            MODULE_ID_TRIDENT},
        .imports=trident_kexports, .lifecycle={.start=start, .detach=detach}
    };
    int rc = module_load(&request, &handle);
    if (!rc) rc = module_start(handle);
    if (!rc && !module_exports(handle)) rc = MODULE_INIT_FAILED;
    if (!rc) {
        gfx_backend_trident = candidate;
        context.backend = &gfx_backend_trident;
    }
    else diagnostic_candidate = diagnostic_slot = NULL;
    kprintf(KLOG_ATTR, "[trident] stage=module rc=%d pair=%08x pinned=%d\n", rc, bi->image_crc, pinned);
    return rc;
}
int trident_module_abort(void)
{
    if (armed) return TRI_E_BUSY;
    return module_abort(handle);
}
