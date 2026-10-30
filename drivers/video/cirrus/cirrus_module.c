#include "cirrus_module.h"
#include "cirrus_imports.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "gfx_internal.h"
#include "kprintf.h"

extern const u32 cirrus_kexports[];
extern const struct module_symbol_contract cirrus_import_contracts[];
extern const struct module_symbol_contract cirrus_export_contracts[];
static struct module_handle cirrus_handle;
static GfxBackend candidate;
static int registered;
static int missing_probe(void) { return 0; }
GfxBackend gfx_backend_cirrus = {.name = "cirrus-unavailable", .probe = missing_probe};
static const CirrusModuleContext context = {
    &gfx_backend_cirrus, &gfx_counters,
    &gfx_current_height, &gfx_flip_enabled, &gfx_display_page
};

static int cirrus_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(GfxBackend *, const CirrusModuleContext *) =
        (int (*)(GfxBackend *, const CirrusModuleContext *))(uptr)entry;
    int rc = init(&candidate, &context);
    if (rc) return rc;
    const u32 entries[] = {
        (u32)(uptr)candidate.probe, (u32)(uptr)candidate.init,
        (u32)(uptr)candidate.query, (u32)(uptr)candidate.shutdown,
        (u32)(uptr)candidate.present_rect, (u32)(uptr)candidate.set_palette,
        (u32)(uptr)candidate.enter, (u32)(uptr)candidate.leave,
        (u32)(uptr)candidate.fill_rect, (u32)(uptr)candidate.blit
    };
    for (u32 i = 0; i < sizeof(entries) / sizeof(entries[0]); i++)
        if (!module_text_contains(r, entries[i]))
            return MODULE_INIT_FAILED;
    if (candidate.prepare) return MODULE_INIT_FAILED;
    if (!module_ref_acquire(cirrus_handle, r->owner, MODULE_REF_REGISTRATION, 0))
        return MODULE_INIT_FAILED;
    registered = 1;
    return 0;
}

static int cirrus_detach(struct module_record *r)
{
    (void)r;
    /* A selected HAL may still be used by graphics/leases. Keep the body and
     * registration until it is no longer selected. DEVICE windows are core
     * reservations, never module-owned pages. No IRQ/tick/system DMA here. */
    if (registered && g_backend == &gfx_backend_cirrus) return 0;
    gfx_backend_cirrus = (GfxBackend){.name = "cirrus-unavailable", .probe = missing_probe};
    if (registered) {
        module_ref_release(cirrus_handle, MODULE_REF_REGISTRATION);
        registered = 0;
    }
    return 1;
}

int cirrus_module_load(void)
{
    if (cirrus_handle.generation) return MODULE_BUSY;
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_CIRRUS_MOD, .name = "cirrus",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, CIRRUS_MODULE_ABI_GEN,
            bi->img_crc, CIRRUS_MODULE_OPS_VERSION, bi->img_valid,
            CIRRUS_IMPORT_COUNT, cirrus_import_contracts, cirrus_export_contracts,
            MODULE_ID_CIRRUS
        },
        .imports = cirrus_kexports,
        .lifecycle = {.start = cirrus_start, .detach = cirrus_detach}
    };
    int rc = module_load(&request, &cirrus_handle);
    if (!rc) rc = module_start(cirrus_handle);
    if (!rc && !module_exports(cirrus_handle)) rc = MODULE_INIT_FAILED;
    if (!rc) gfx_backend_cirrus = candidate;
    kprintf(KLOG_ATTR, "[cirrus] module rc=%d pair=%08x registered=%d\n",
            rc, bi->img_crc, registered);
    return rc;
}
int cirrus_module_abort(void) { return module_abort(cirrus_handle); }
