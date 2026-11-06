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
static struct module_handle handle;
static GfxBackend candidate;
static bool pinned, armed;
static u32 restore_entry;
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
    .display_owner=appslot_gfx_owner
};
static int resident_ro(const struct module_record *r, const void *p, u32 bytes)
{
    u32 a = (u32)(uptr)p;
    return a >= r->ro && bytes <= r->ro_size && a - r->ro <= r->ro_size - bytes;
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
    /* All optional function slots are NULL in this CPU-only backend. Reject
     * unexpected hooks, rather than treating NULL as a required text address. */
    if (candidate.fill_rect || candidate.blit || candidate.set_mode || candidate.dpms ||
        candidate.cursor_image || candidate.cursor_move || candidate.cursor_show ||
        candidate.vsync || candidate.page_flip || candidate.gamma || candidate.osri_show)
        return MODULE_INIT_FAILED;
    if (!resident_ro(r, candidate.name, sizeof("trident")) ||
        !resident_ro(r, candidate.modes, sizeof(GfxMode)) ||
        candidate.mode_count != 1 || candidate.default_mode || candidate.selected_mode ||
        candidate.bb_base || candidate.bb_size || candidate.bb_pitch != TRI_PITCH ||
        candidate.bb_format != GFX_BB_PACKED8)
        return MODULE_INIT_FAILED;
    if (kstrcmp(candidate.name, "trident")) return MODULE_INIT_FAILED;
    const GfxMode *mode = candidate.modes;
    if (mode->width != TRI_WIDTH || mode->height != TRI_HEIGHT ||
        mode->format != GFX_FMT_PACKED8 || mode->dac_bits != 6 ||
        mode->hw_mode || mode->reserved || mode->pitch != TRI_PITCH)
        return MODULE_INIT_FAILED;
    restore_entry = exports[3];
    if (!module_ref_acquire(handle, r->owner, MODULE_REF_REGISTRATION, 0))
        return MODULE_INIT_FAILED;
    pinned = true;
    int (*prepare)(void) = (int (*)(void))(uptr)exports[2];
    rc = prepare(); /* real bind and descriptor checks, no indexed access */
    if (rc) kprintf(KLOG_ATTR, "[trident] stage=prepare rc=%d\n", rc);
    return rc;
}
static int detach(struct module_record *r)
{
    (void)r;
    if (armed) return 0;
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
    context.resources = trident_resources_get();
    if (!context.resources) {
        kprintf(KLOG_ATTR, "[trident] stage=resources rc=%d\n", TRI_E_RESOURCE);
        return TRI_E_RESOURCE;
    }
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path=SYS_TRIDENT_MOD, .name="trident",
        .policy={KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, TRIDENT_MODULE_ABI_GEN,
            bi->img_crc, TRIDENT_MODULE_OPS_VERSION, bi->img_valid,
            TRIDENT_IMPORT_COUNT, trident_import_contracts, trident_export_contracts,
            MODULE_ID_TRIDENT},
        .imports=trident_kexports, .lifecycle={.start=start, .detach=detach}
    };
    int rc = module_load(&request, &handle);
    if (!rc) rc = module_start(handle);
    if (!rc && !module_exports(handle)) rc = MODULE_INIT_FAILED;
    if (!rc) gfx_backend_trident = candidate;
    kprintf(KLOG_ATTR, "[trident] stage=module rc=%d pair=%08x pinned=%d\n", rc, bi->img_crc, pinned);
    return rc;
}
int trident_module_abort(void)
{
    if (armed) return TRI_E_BUSY;
    return module_abort(handle);
}
