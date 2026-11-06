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
#include "../../../fs/vfs.h"
#include "../../../exec/appslot.h"
#include "serial_diag.h"
#include "io.h"
#include "idt.h"
#include "time_units.h"
#include "../../../hwdev/gf/display/api.h"
#include "leave_log.h"

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
static bool heat_pending;
static bool foreground_deadline_valid;
static u32 foreground_next_tick;
bool geforce_fg_due(u32 now_tick)
{
    return heat_pending || !foreground_deadline_valid ||
        (i32)(now_tick-foreground_next_tick)>=0;
}
/* F5 owns the absolute microsecond deadlines. Cache values only, never its
 * context. Floor the delay using a tick sampled before clock64 so the cheap
 * syscall gate may wake early but cannot postpone the shortest F5 deadline.
 * Limit the cache to one second (also keeps division strictly 32-bit). */
static void foreground_deadline_update(const struct gf5_test_ops *ops)
{
    foreground_deadline_valid=false;
    if (!ops || !ops->ctx || !ops->ctx->links.clock64) return;
    struct gf5 *g=ops->ctx;
    u32 tick=tick_count;
    u64 now, next;
    if (g->busy || g->links.clock64(g->links.owner,&now)) return;
    next=now+USEC_PER_SEC;
    const u64 deadlines[]={g->next_sensor,g->next_auto,g->next_step,g->next_toggle};
    for (u32 i=0;i<sizeof(deadlines)/sizeof(deadlines[0]);i++)
        if (deadlines[i] && deadlines[i]<next) next=deadlines[i];
    if ((g->fan.sensor_poll && !g->next_sensor) ||
        (g->fan.mode==GF5_FAN_AUTO && !g->next_auto)) next=now;
    u32 delay=next>now ? (u32)(next-now) : 0;
    foreground_next_tick=tick+delay/(USEC_PER_SEC/PIT_HZ);
    foreground_deadline_valid=true;
}
static i32 heat_reason, heat_milli_c, heat_source_rc;
static struct geforce_heat_bridge heat_bridge={.request=geforce_heat_stop_request};
void geforce_heat_stop_request(i32 reason,i32 milli_c,i32 source_rc)
{
    /* Preserve the first cause across nested waits and repeated requests. */
    if (heat_bridge.latched) return;
    heat_bridge.latched=true;
    heat_reason=reason; heat_milli_c=milli_c; heat_source_rc=source_rc;
    heat_pending=true;
}
int geforce_heat_latch_clear(void)
{
    if (!heat_bridge.latched) return 0;
    if (foreground_poll_busy || heat_pending || !registered || !heat_bridge.clear)
        return GF5_ESTATE;
    foreground_poll_busy=true;
    int rc=heat_bridge.clear();
    if (!rc) heat_bridge.latched=false;
    foreground_poll_busy=false;
    return rc;
}
static void heat_stop_consume(void)
{
    if (!heat_pending) return;
    heat_pending=false;
    int stop_rc=heat_bridge.stop ? heat_bridge.stop() : gf_production_return();
    if (!pc98_route_safe || !pc98_route_safe()) {
        int sync_rc=vfs_sync();
        /* Emergency output must not use a console on the stopped device. */
        serial_puts_polled("[geforce] heat halt reason="); serial_diag_i32(heat_reason);
        serial_puts_polled(" milli_c="); serial_diag_i32(heat_milli_c);
        serial_puts_polled(" source_rc="); serial_diag_i32(heat_source_rc);
        serial_puts_polled(" stop_rc="); serial_diag_i32(stop_rc);
        serial_puts_polled(" sync_rc="); serial_diag_i32(sync_rc);
        serial_puts_polled("\n");
        _disable();
        for (;;) { _halt(); }
    }
    /* Stop and route precede GUI notification and CUI/running-app recovery. */
    geforce_display_failed();
    (void)appslot_abort_request();
    kprintf(KLOG_ATTR,"[geforce] heat stop reason=%d milli_c=%d source_rc=%d stop_rc=%d\n",
            heat_reason,heat_milli_c,heat_source_rc,stop_rc);
}
void geforce_foreground_poll(void)
{
    if (foreground_poll_busy) return;
    foreground_poll_busy=true;
    const struct gf5_test_ops *ops=geforce_feature5_ops();
    if (ops && ops->service_poll) (void)ops->service_poll(ops->ctx);
    foreground_deadline_update(ops);
    /* The borrowed module context is no longer in use when stop runs. */
    heat_stop_consume();
    foreground_poll_busy=false;
}
static int missing_probe(void) { return 0; }
GfxBackend gfx_backend_geforce = {.name="geforce-unavailable", .probe=missing_probe};
static GeforceModuleContext context = {
    .backend=&gfx_backend_geforce, .counters=&gfx_counters,
    .height=&gfx_current_height, .flip=&gfx_flip_enabled, .page=&gfx_display_page,
    .accel_out=&accel_slot, .foundation_out=&foundation_slot,
    .display_out=&display_slot, .feature5_out=&feature5_slot,
    .display_restore_out=&display_restore_slot, .heat=&heat_bridge
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
    struct gf_display_context *video = p->device ? p->device->context : 0;
    struct gf_io *leave_io = video ? &video->io : 0;
    int rc = p->stop ? p->stop() : 0;
    /* Match backend stop: sample after quiesce, before 98 restore/redraw. */
    geforce_leave_log(leave_io, rc);
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
