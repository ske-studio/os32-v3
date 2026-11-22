#define OS32_SQLITE_OUTER_IMPL
#include "sqlite_engine.h"
#include "fep_boot.h"
#include "sqlite_engine_body.h"
#include "exec_arch.h"
#include "arch_kernel_fp.h"

_Static_assert(__builtin_types_compatible_p(__typeof__(&arch_kernel_fp_begin),
                                            arch_kernel_fp_begin_fn *),
               "arch_kernel_fp_begin signature");
_Static_assert(__builtin_types_compatible_p(__typeof__(&arch_kernel_fp_end),
                                            arch_kernel_fp_end_fn *),
               "arch_kernel_fp_end signature");

#include "io.h"
#include "irq/irq.h"
#include "../mm/pgalloc.h"
#include "../mm/paging.h"
#include "memmap.h"
#include "module/module_runtime.h"
#include "../mm/memory_boot.h"
#include "module/module.h"
#include "platform_module_pair.h"
#include "config.h"
#include "ime/ime.h"
#include "ime/ime_store.h"
#include "ime/fep_module.h"
#include "console/unicode_file.h"
#include "kstring.h"
#include "kprintf.h"
#include "os32_generations.h"
#include "../lib/sqlite3/generated/sqlite_imports.h"
#include "../lib/sqlite3/generated/sqlite_facade.h"

volatile u32 sqlite_engine_active;
u32 sqlite_engine_stack_top;
#define SQLITE_STACK_MARK 0xa5
static u32 sqlite_stack_peak;
extern void sqlite_call_on_alt_stack(void (*call)(void *), u32 top, void *frame);

int sqlite_engine_available(int io)
{
    int irq_on = _irq_enabled();
    u32 flags = irq_save();
    int ok = sqlite_engine_stack_top && !sqlite_engine_active && !kctx_irq_depth && !kctx_exc_depth &&
             (!io || irq_on);
    irq_restore(flags);
    return ok;
}

int sqlite_engine_run(void (*call)(void *), void *frame, int io)
{
    int irq_on = _irq_enabled();
    u32 flags = irq_save();
    if (!sqlite_engine_stack_top || sqlite_engine_active || kctx_irq_depth || kctx_exc_depth ||
        (io && !irq_on)) {
        irq_restore(flags);
        return 0;
    }
    sqlite_engine_active = 1;
    irq_restore(flags);
    struct arch_kernel_fp_guard guard;
    if (arch_kernel_fp_begin(&guard)) {
        (void)irq_save();
        sqlite_engine_active = 0;
        irq_restore(flags);
        return 0;
    }
    sqlite_call_on_alt_stack(call, sqlite_engine_stack_top, frame);
    arch_kernel_fp_end(&guard);
    (void)irq_save();
    sqlite_engine_active = 0;
    irq_restore(flags);
    /* STOP belongs to the caller's safe point, after output and teardown. */
    return 1;
}

/* Downward stack: the untouched prefix excludes the NP guard page.
 * Only diagnostics scan it; neither IRQ masking nor an engine call is needed. */
u32 sqlite_engine_stack_high_water(void)
{
    if (!sqlite_engine_stack_top) return 0;
    const volatile u8 *base = (const volatile u8 *)(uptr)
        (sqlite_engine_stack_top - SQLITE_ENGINE_STACK_SIZE);
    u32 untouched = 0;
    while (untouched < SQLITE_ENGINE_STACK_SIZE - sqlite_stack_peak &&
           base[untouched] == SQLITE_STACK_MARK) untouched++;
    sqlite_stack_peak = SQLITE_ENGINE_STACK_SIZE - untouched;
    return sqlite_stack_peak;
}

void sqlite_engine_stack_report(void)
{
    kprintf(KLOG_ATTR, "sqlite stack high-water=%u of %u\n",
            sqlite_engine_stack_high_water(), SQLITE_ENGINE_STACK_SIZE);
}

/* Boot-only adapter: code, pool and alternate stack share one module owner.
 * The candidate stays private until module_start has returned its prefix. */
const SqliteModuleApi *sqlite_module_api = &sqlite_stub_api;
static SqliteModuleApi candidate_api;
static struct module_handle sqlite_handle;
static int init_called, candidate_valid, cleanup_blocked;
int services_public;
enum services_stage services_stage;
volatile u32 services_boot_fault;
extern volatile u32 tick_count;
static u32 pool_phys;
static int services_reserve;
extern const u32 sqlite_kexports[];
extern const struct module_symbol_contract sqlite_import_contracts[];
extern const struct module_symbol_contract sqlite_export_contracts[];

int sqlite_module_ready(void)
{
    return sqlite_module_api != &sqlite_stub_api;
}

static int valid_api(const struct module_record *r)
{
    u32 entries[SQLITE_MODULE_API_SLOTS];
    STATIC_ASSERT(sizeof(candidate_api) == sizeof(entries), sqlite_facade_size);
    kmemcpy(entries, &candidate_api, sizeof(entries));
    for (u32 i = 0; i < SQLITE_MODULE_API_SLOTS; ++i)
        if (!module_text_contains(r, entries[i])) return 0;
    return 1;
}

struct sqlite_init_call { sqlite_body_init_fn *init; SqliteModuleApi *out; int rc; };
static void call_module_init(void *arg)
{
    struct sqlite_init_call *call = arg;
    init_called = 1;
    call->rc = call->init(P2V(pool_phys), call->out);
}

int sqlite_engine_body_start(u32 owner, sqlite_body_init_fn *init, SqliteModuleApi *out, int *init_rc)
{
    *init_rc = SQLITE_MISUSE;
    if (services_boot_fault == SERVICES_FAULT_POOL) return SQLITE_BODY_NO_POOL;
    pool_phys = pgalloc_alloc_phys(owner, SQLITE_MODULE_POOL_SIZE / PAGE_SIZE);
    if (!pool_phys) return SQLITE_BODY_NO_POOL;
    if (services_boot_fault == SERVICES_FAULT_STACK) return SQLITE_BODY_NO_STACK;
    u32 stack = pgalloc_alloc_phys(owner, SQLITE_ENGINE_STACK_SIZE / PAGE_SIZE + 1);
    if (!stack) return SQLITE_BODY_NO_STACK;
    paging_sqlite_stack_guard = (u32)(uptr)P2V(stack);
    if (paging_set_not_present(paging_sqlite_stack_guard,
                              paging_sqlite_stack_guard + PAGE_SIZE - 1) ||
        paging_is_present(paging_sqlite_stack_guard)) return SQLITE_BODY_NO_GUARD;
    sqlite_engine_stack_top = paging_sqlite_stack_guard + PAGE_SIZE + SQLITE_ENGINE_STACK_SIZE;
    /* Fill once, before module init uses the stack. Never repaint on entry. */
    kmemset((void *)(uptr)(sqlite_engine_stack_top - SQLITE_ENGINE_STACK_SIZE),
            SQLITE_STACK_MARK, SQLITE_ENGINE_STACK_SIZE);
    sqlite_stack_peak = 0;
    struct sqlite_init_call call = {init, out, SQLITE_MISUSE};
    sqlite_engine_run(call_module_init, &call, 0);
    *init_rc = call.rc;
    return call.rc == SQLITE_OK ? SQLITE_BODY_OK : SQLITE_BODY_INIT;
}

static int sqlite_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int init_rc;
    int rc = sqlite_engine_body_start(r->owner, (sqlite_body_init_fn *)(uptr)entry,
                                      &candidate_api, &init_rc);
    if (rc != SQLITE_BODY_OK && rc != SQLITE_BODY_INIT) return MODULE_NO_PAGES;
    candidate_valid = valid_api(r);
    return !candidate_valid || init_rc != SQLITE_OK ||
           services_boot_fault == SERVICES_FAULT_SQLITE_INIT;
}

struct sqlite_shutdown_call { int (*shutdown)(void); int rc; };
static void call_shutdown(void *arg)
{
    struct sqlite_shutdown_call *call = arg;
    call->rc = call->shutdown();
}

static int sqlite_module_shutdown(void)
{
    return services_boot_fault == SERVICES_FAULT_SHUTDOWN
         ? SQLITE_BUSY : candidate_api.shutdown();
}

int sqlite_engine_body_stop(int (*shutdown)(void))
{
    if (sqlite_engine_active) return SQLITE_STOP_BUSY;
    if (init_called) {
        struct sqlite_shutdown_call call = {shutdown, SQLITE_MISUSE};
        if (!sqlite_engine_run(call_shutdown, &call, 0)) return SQLITE_STOP_BUSY;
        if (call.rc != SQLITE_OK) return SQLITE_STOP_SHUTDOWN;
    }
    init_called = 0;
    /* The owner reclaimer writes freed pages; restore the guard before it runs. */
    int remap_failed = paging_sqlite_stack_guard &&
        paging_set_page(paging_sqlite_stack_guard,
                        V2P((void *)(uptr)paging_sqlite_stack_guard), PAGE_RW);
    sqlite_engine_stack_top = 0;
    if (remap_failed) return SQLITE_STOP_REMAP;
    paging_sqlite_stack_guard = 0;
    pool_phys = 0;
    return SQLITE_STOP_DONE;
}

static int sqlite_quiesce(struct module_record *r)
{
    (void)r;
    sqlite_module_api = &sqlite_stub_api;
    if (cleanup_blocked || (init_called && !candidate_valid)) return 0;
    if (sqlite_engine_body_stop(sqlite_module_shutdown) != SQLITE_STOP_DONE) return 0;
    candidate_valid = 0;
    return 1;
}

int os32_sqlite_start(void)
{
    if (sqlite_module_ready()) return SQLITE_OK;
    if (sqlite_handle.generation) return OS32_ERR_NOSYS;
    u32 pair = 0;
    int pair_valid = platform_module_pair(&pair);
    const struct module_request request = {
        .path = SYS_SQLITE_MOD, .name = "sqlite",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, SQLITE_MODULE_ABI_GEN,
            pair, SQLITE_MODULE_OPS_VERSION, pair_valid,
            SQLITE_IMPORT_COUNT, sqlite_import_contracts,
            sqlite_export_contracts, MODULE_ID_SQLITE
        },
        .imports = sqlite_kexports,
        .lifecycle = {.start = sqlite_start, .quiesce = sqlite_quiesce}
    };
    int rc = module_load(&request, &sqlite_handle);
    if (!rc) rc = module_start(sqlite_handle);
    if (!rc && module_exports(sqlite_handle)) sqlite_module_api = &candidate_api;
    else if (!rc) rc = MODULE_INIT_FAILED;
    kprintf(KLOG_ATTR, "[sqlite] module rc=%d pair=%08x\n", rc, pair);
    struct module_record *record = module_record_get(sqlite_handle);
    struct memory_page_counts counts;
    memory_boot_page_counts(kdebug.pages ? kdebug.base_pa / PAGE_SIZE : 0,
                            kdebug.pages, &counts);
    /* Separate observation; do not increment the existing boot audit runs.
     * SQLite stays inside SLACK/pool, including quarantined owner pages. */
    kprintf(KLOG_ATTR, "[sqlite] owner=%u state=%u pages=%u body=%u prefix=%u diff=%d\n",
            record ? record->owner : 0, record ? record->state : 0,
            record && record->owner ? ledger_owner_pages(record->owner) : 0,
            record ? record->pages : 0, record ? record->prefix_pages : 0,
            counts.diff);
    kprintf(KLOG_ATTR, "[sqlite] stack_guard=%08x p=%u\n", paging_sqlite_stack_guard,
            paging_sqlite_stack_guard ? paging_is_present(paging_sqlite_stack_guard) : 0);
    return rc;
}

static const char *services_state_name(void);

void services_boot(void)
{
    /* Boot only, including failure: never retry an isolated lifetime. */
    if (services_stage != SERVICES_UNICODE) return;
    u32 start_tick = tick_count;
    int reserve = OS32_SQLITE_FEP_RESERVE;
    OS32_Stat dictionary;
    /* Only definite absence selects recovery mode; other stat failures retain
     * the normal FEP initialization/rollback path. Never open the dictionary. */
    int no_dictionary = vfs_stat(IME_DICT_PATH, &dictionary) == VFS_ERR_NOTFOUND;
    /* Required bodies stay required even on the dictionary-free recovery FD.
     * The policy table generates this list; absence never publishes services. */
    static const char *const required_paths[] = MODULE_REQUIRED_PATHS;
    int rc = MODULE_NOT_FOUND;
    for (u32 i = 0; i < sizeof(required_paths) / sizeof(required_paths[0]); i++)
        if (vfs_stat(required_paths[i], &dictionary) != VFS_OK) goto rollback;
    rc = os32_sqlite_start();
    if (rc) goto rollback;
    services_stage = SERVICES_SQLITE_INTERNAL_READY;
    kprintf(KLOG_ATTR, "[services] SQLITE_INTERNAL_READY tick=%u\n", tick_count);
    rc = os32_sqlite_test();
    kprintf(KLOG_ATTR, "[SQ] kernel test rc=%d\n", rc);
    if (rc) goto rollback;
    rc = fep_boot_start(&sqlite_handle, no_dictionary, &reserve);
    if (rc) goto rollback;
    goto report;

rollback:;
    enum services_stage failed_stage = services_stage;
    services_public = 0;
    cleanup_blocked = fep_boot_rollback(&sqlite_handle, rc);
    int isolated = module_abort(sqlite_handle);
    sqlite_module_api = &sqlite_stub_api;
    services_stage = isolated || cleanup_blocked ? SERVICES_MINIMAL_QUARANTINED
                                               : SERVICES_MINIMAL;
    kprintf(KLOG_ATTR, "[services] rollback stage=%u rc=%d fault=%u cleanup=%u\n",
            failed_stage, rc, services_boot_fault, (u32)cleanup_blocked);
report:;
    services_reserve = reserve;
    struct module_record *r = module_record_get(sqlite_handle);
    u32 pages = r && r->state != MODULE_ABSENT && r->owner ? ledger_owner_pages(r->owner) : 0;
    u32 fep = ime_boot_bytes();
    u32 fep_pages = fep_module_pages();
    struct memory_page_counts counts;
    memory_boot_page_counts(kdebug.pages ? kdebug.base_pa / PAGE_SIZE : 0,
                            kdebug.pages, &counts);
    /* Observation only: preserve kaudit's existing run count and classifications.
     * Both module bodies remain in SLACK/pool; FEP buffers are in KHEAP. */
    kprintf(KLOG_ATTR, "[services] %s public=%u sqlite_pages=%u fep_pages=%u fep_bytes=%u quarantine_bytes=%u diff=%d\n",
            services_state_name(),
            (u32)services_public, pages, fep_pages, fep,
            services_stage == SERVICES_MINIMAL_QUARANTINED ? (pages + fep_pages) * PAGE_SIZE + fep : 0,
            counts.diff);
    kprintf(KLOG_ATTR, "[services] tick=%u..%u reserve=%d unicode=%08x\n",
            start_tick, tick_count, reserve, unicode_file_debug_phys());
}

/* Diagnostic only; no engine entry, allocation or ABI change. */
static const char *services_state_name(void)
{
    switch (services_stage) {
    case SERVICES_READY: return "SERVICES_READY";
    case SERVICES_SQLITE_ONLY: return "SQLITE_ONLY";
    case SERVICES_MINIMAL: return "MINIMAL";
    case SERVICES_MINIMAL_QUARANTINED: return "MINIMAL_QUARANTINED";
    default: return "STARTING";
    }
}

void services_ver_state(ServicesVerState *out)
{
    out->stack_used = sqlite_engine_stack_high_water();
    out->stack_size = SQLITE_ENGINE_STACK_SIZE;
    out->public = (u32)services_public;
    kstrncpy(out->state, services_state_name(), sizeof(out->state));
}

void services_report(void)
{
    struct module_record *r = module_record_get(sqlite_handle);
    u32 pages = r && r->state != MODULE_ABSENT && r->owner ? ledger_owner_pages(r->owner) : 0;
    u32 fep_pages = fep_module_pages();
    u32 fep = ime_boot_bytes();
    kprintf(KLOG_ATTR, "  services: %s public=%u sqlite_pages=%u fep_pages=%u quarantine_bytes=%u reserve=%d\n",
            services_state_name(), (u32)services_public, pages, fep_pages,
            services_stage == SERVICES_MINIMAL_QUARANTINED ? (pages + fep_pages) * PAGE_SIZE + fep : 0,
            services_reserve);
}
