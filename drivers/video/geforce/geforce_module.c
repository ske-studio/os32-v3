#include "geforce_module.h"
#include "geforce_imports.h"
#include "geforce_tick.h"
#include "../../../hwdev/gf/production.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "gfx_internal.h"
#include "../../../mm/pgalloc.h"
#include "kstring.h"
#include "kprintf.h"
#include "gf_production.h"
#include "pc98_route.h"
#include "../../../hwdev/gf/display/api.h"

extern const u32 geforce_kexports[];
extern const struct module_symbol_contract geforce_import_contracts[], geforce_export_contracts[];
static struct module_handle geforce_handle;
static GfxBackend candidate;
static int registered;
static const struct gf3_test_ops *accel_slot;
static const struct gf1_test_ops *foundation_slot;
/* OS32 module loans, GF_FEAT4_DESIGN C3-slot. C2 owns publication and
 * operation-time closure; the kernel owns these addresses until unload. */
static const struct gf4_test_ops *display_slot;
static const struct gf4_restore_ops *display_restore_slot;
static const struct gf5_test_ops *feature5_slot;
const struct gf3_test_ops *geforce_accel_ops(void)
{ return registered ? accel_slot : 0; }
const struct gf1_test_ops *geforce_foundation_ops(void)
{ return registered ? foundation_slot : 0; }
const struct gf4_test_ops *geforce_display_ops(void)
{ return registered ? display_slot : 0; }
const struct gf4_restore_ops *geforce_display_restore_ops(void)
{ return registered ? display_restore_slot : 0; }
const struct gf5_test_ops *geforce_feature5_ops(void)
{ return registered ? feature5_slot : 0; }
/* Resident guard also covers service callbacks that reach a foreground wait.
 * Do not retain a module loan between calls or gate thermal work on drawing. */
static bool foreground_poll_busy;
void geforce_foreground_poll(void)
{
    if (foreground_poll_busy) return;
    foreground_poll_busy=true;
    const struct gf5_test_ops *ops=geforce_feature5_ops();
    if (ops && ops->service_poll) (void)ops->service_poll(ops->ctx);
    /* F0s consumes pending heat stop here, with reentry still excluded. */
    foreground_poll_busy=false;
}
static int missing_probe(void) { return 0; }
GfxBackend gfx_backend_geforce = {.name="geforce-unavailable", .probe=missing_probe};
static GeforceModuleContext context = {
    .backend=&gfx_backend_geforce, .counters=&gfx_counters,
    .height=&gfx_current_height, .flip=&gfx_flip_enabled, .page=&gfx_display_page,
    .accel_out=&accel_slot, .foundation_out=&foundation_slot,
    .display_out=&display_slot, .feature5_out=&feature5_slot,
    .display_restore_out=&display_restore_slot
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
    if (rc) goto failed;
    const u32 entries[] = {
        (u32)(uptr)candidate.probe, (u32)(uptr)candidate.init,
        (u32)(uptr)candidate.query, (u32)(uptr)candidate.shutdown,
        (u32)(uptr)candidate.present_rect, (u32)(uptr)candidate.set_palette,
        (u32)(uptr)candidate.enter, (u32)(uptr)candidate.leave, (u32)(uptr)candidate.prepare
    };
    for (u32 i=0; i<sizeof(entries)/sizeof(entries[0]); i++)
        if (!module_text_contains(r, entries[i])) {
            rc=MODULE_INIT_FAILED;
            goto failed;
        }
    /* Layer 3 hooks are optional, but must belong to the pinned module. */
    if ((candidate.fill_rect && !module_text_contains(r, (u32)(uptr)candidate.fill_rect)) ||
        (candidate.blit && !module_text_contains(r, (u32)(uptr)candidate.blit))) {
        rc=MODULE_INIT_FAILED;
        goto failed;
    }
    const u32 display_entries[] = {
        (u32)(uptr)candidate.set_mode, (u32)(uptr)candidate.dpms,
        (u32)(uptr)candidate.cursor_image, (u32)(uptr)candidate.cursor_move,
        (u32)(uptr)candidate.cursor_show, (u32)(uptr)candidate.vsync,
        (u32)(uptr)candidate.page_flip, (u32)(uptr)candidate.gamma
    };
    for (u32 i=0; i<sizeof(display_entries)/sizeof(display_entries[0]); i++)
        if (display_entries[i] && !module_text_contains(r, display_entries[i])) {
            rc=MODULE_INIT_FAILED;
            goto failed;
        }
    if (!module_ref_acquire(geforce_handle, r->owner, MODULE_REF_REGISTRATION, 0)) {
        rc=MODULE_INIT_FAILED;
        goto failed;
    }
    registered=1;
    if (context.platform) gf_production_set_return(geforce_return, context.platform);
    return 0;
failed:
    accel_slot=0;
    display_slot=0;
    display_restore_slot=0;
    feature5_slot=0;
    return rc;
}
static int geforce_detach(struct module_record *r)
{
    (void)r;
    /* Close admission even when the module must retain its resources. */
    accel_slot=0;
    display_slot=0;
    /* A refused detach keeps the management loan for retained resources. */
    const struct gf4_restore_ops *restore=display_restore_slot;
    display_restore_slot=0;
    /* Device context and restoration callbacks live for the whole boot. */
    const struct geforce_platform *p=context.platform;
    const struct gf_production *owner=p ? p->opaque : 0;
    if (registered || (owner && (owner->foundation.registered ||
        owner->foundation.running || owner->foundation.bm ||
        owner->foundation.mapped || owner->foundation.imem.used ||
        owner->chan.state != GF2_OFF || owner->chan.fault.latched ||
        owner->chan.mapped || owner->chan.vram_reserved ||
        owner->chan.staging_live))) {
        display_restore_slot=restore;
        return 0;
    }
    if (geforce_tick_bind(0,0)) {
        display_restore_slot=restore;
        return 0;
    }
    foundation_slot=0;accel_slot=0;
    feature5_slot=0;
    context.display_data=0;
    context.display_state=0;
    return 1;
}
int geforce_module_load(void)
{
    if (geforce_handle.generation) return MODULE_BUSY;
    accel_slot=0;
    display_slot=0;
    display_restore_slot=0;
    feature5_slot=0;
    int rc=gf4_data_bind(&context.display_data, &context.display_state);
    if (rc) return rc;
    context.platform=geforce_platform_get();
    const struct bootinfo *bi=bootinfo_get();
    const struct module_request request = {
        .path = SYS_GEFORCE_MOD, .name="geforce",
        .policy={KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, GEFORCE_MODULE_ABI_GEN,
            bi->img_crc, GEFORCE_MODULE_OPS_VERSION, bi->img_valid,
            GEFORCE_IMPORT_COUNT, geforce_import_contracts, geforce_export_contracts, MODULE_ID_GEFORCE},
        .imports=geforce_kexports, .lifecycle={.start=geforce_start, .detach=geforce_detach}
    };
    rc=module_load(&request, &geforce_handle);
    if (!rc) rc=module_start(geforce_handle);
    if (!rc && !module_exports(geforce_handle)) rc=MODULE_INIT_FAILED;
    if (!rc) gfx_backend_geforce=candidate;
    else {
        foundation_slot=0;accel_slot=0;
        display_slot=0;display_restore_slot=0;feature5_slot=0;
    }
    kprintf(KLOG_ATTR, "[geforce] module rc=%d pair=%08x registered=%d\n", rc, bi->img_crc, registered);
    return rc;
}
int geforce_module_abort(void)
{
    accel_slot=0;
    display_slot=0;
    if (registered) return MODULE_ISOLATED;
    display_restore_slot=0;
    feature5_slot=0;
    return module_abort(geforce_handle);
}
