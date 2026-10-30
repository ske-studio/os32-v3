#include "geforce_module.h"
#include "geforce_imports.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "gfx_internal.h"
#include "../mm/pgalloc.h"
#include "kstring.h"
#include "kprintf.h"
#include "gf_production.h"
#include "pc98_route.h"

extern const u32 geforce_kexports[];
extern const struct module_symbol_contract geforce_import_contracts[], geforce_export_contracts[];
static struct module_handle geforce_handle;
static GfxBackend candidate;
static int registered;
static int missing_probe(void) { return 0; }
GfxBackend gfx_backend_geforce = {.name="geforce-unavailable", .probe=missing_probe};
static GeforceModuleContext context = {
    &gfx_backend_geforce, &gfx_counters, &gfx_current_height, &gfx_flip_enabled, &gfx_display_page, 0
};
int geforce_pc98_safe(void)
{
    const struct geforce_platform *p = geforce_platform_get();
    return !p || !p->pc98_safe || p->pc98_safe();
}
/* The P4 hook reserves/maps all BAR decode ranges before these two records.
 * RAM CLIENT is never confused with BAR1 DISPLAY, nor allocated for PEGC. */
int geforce_boot_reserve(void)
{
    const struct geforce_platform *p = geforce_platform_get();
    u32 first;
    struct ledger_surface sf = {
        .npages=MEM_GFX_BB8_SIZE / PAGE_SIZE,
        .width=MEM_GFX_BB8_WIDTH, .height=MEM_GFX_BB8_HEIGHT, .pitch=MEM_GFX_BB8_PITCH,
        .owner=LEDGER_OWNER_KERNEL, .backing=LEDGER_SB_MMIO, .backend=LEDGER_SF_GEFORCE,
        .role=LEDGER_ROLE_DISPLAY, .format=GFX_FMT_PACKED8, .planes=1,
        .cache=LEDGER_CACHE_UC, .perm_max=LEDGER_PERM_RW
    };
    if (!p || !p->reserve || !p->reserve()) return 0;
    sf.first = p->display_phys / PAGE_SIZE;
    if (!ledger_surface_create(&sf, 0)) return 0;
    if (!pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, sf.npages, MEM_PHYS_EXEC_FLOOR / PAGE_SIZE,
                              pgalloc_arena_end(), LEDGER_TOP_DOWN, &first)) return 0;
    sf.first=first; sf.owner=LEDGER_OWNER_BOOT; sf.backing=LEDGER_SB_RAM;
    sf.role=LEDGER_ROLE_CLIENT; sf.cache=LEDGER_CACHE_WB;
    kmemset(P2V(first * PAGE_SIZE), 0, MEM_GFX_BB8_SIZE);
    if (ledger_surface_create(&sf, 0)) return 1;
    (void)pgalloc_free_n_owner(sf.owner, first, sf.npages);
    return 0;
}
static int geforce_return(void *opaque)
{
    struct geforce_platform *p = opaque;
    int rc = p->stop ? p->stop() : 0;
    if (pc98_route_to_98()) rc = -1;
    return rc;
}
static int geforce_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(GfxBackend *, const GeforceModuleContext *) =
        (int (*)(GfxBackend *, const GeforceModuleContext *))(uptr)entry;
    int rc = init(&candidate, &context);
    if (rc) return rc;
    const u32 entries[] = {
        (u32)(uptr)candidate.probe, (u32)(uptr)candidate.init,
        (u32)(uptr)candidate.query, (u32)(uptr)candidate.shutdown,
        (u32)(uptr)candidate.present_rect, (u32)(uptr)candidate.set_palette,
        (u32)(uptr)candidate.enter, (u32)(uptr)candidate.leave, (u32)(uptr)candidate.prepare
    };
    for (u32 i=0; i<sizeof(entries)/sizeof(entries[0]); i++)
        if (entries[i] < r->text || entries[i] - r->text >= r->text_size)
            return MODULE_INIT_FAILED;
    if (candidate.fill_rect || candidate.blit) return MODULE_INIT_FAILED;
    if (!module_ref_acquire(geforce_handle, r->owner, MODULE_REF_REGISTRATION, 0))
        return MODULE_INIT_FAILED;
    registered=1;
    if (context.platform) gf_production_set_return(geforce_return, context.platform);
    return 0;
}
static int geforce_detach(struct module_record *r)
{
    (void)r;
    /* Device context and restoration callbacks live for the whole boot. */
    return !registered;
}
int geforce_module_load(void)
{
    if (geforce_handle.generation) return MODULE_BUSY;
    context.platform=geforce_platform_get();
    const struct bootinfo *bi=bootinfo_get();
    const struct module_request request = {
        .path = SYS_GEFORCE_MOD, .name="geforce",
        .policy={KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, GEFORCE_MODULE_ABI_GEN,
            bi->img_crc, GEFORCE_MODULE_OPS_VERSION, bi->img_valid,
            GEFORCE_IMPORT_COUNT, geforce_import_contracts, geforce_export_contracts, MODULE_ID_GEFORCE},
        .imports=geforce_kexports, .lifecycle={.start=geforce_start, .detach=geforce_detach}
    };
    int rc=module_load(&request, &geforce_handle);
    if (!rc) rc=module_start(geforce_handle);
    if (!rc && !module_exports(geforce_handle)) rc=MODULE_INIT_FAILED;
    if (!rc) gfx_backend_geforce=candidate;
    kprintf(KLOG_ATTR, "[geforce] module rc=%d pair=%08x registered=%d\n", rc, bi->img_crc, registered);
    return rc;
}
int geforce_module_abort(void) { return module_abort(geforce_handle); }
