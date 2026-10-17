#define OS32_SQLITE_OUTER_IMPL
#include "sqlite_engine.h"
#include "io.h"
#include "irq.h"
#include "pgalloc.h"
#include "memmap.h"
#include "module_runtime.h"
#include "memory_boot.h"
#include "module.h"
#include "bootinfo.h"
#include "config.h"
#include "kstring.h"
#include "kprintf.h"
#include "os32_generations.h"
#include "../lib/sqlite3/generated/sqlite_imports.h"
#include "../lib/sqlite3/generated/sqlite_facade.h"

volatile u32 sqlite_engine_active;
u32 sqlite_engine_stack_top;
extern void sqlite_call_on_alt_stack(void (*call)(void *), u32 top, void *frame);

int sqlite_engine_available(int io)
{
    u32 flags = irq_save();
    int ok = sqlite_engine_stack_top && !sqlite_engine_active && !kctx_irq_depth && !kctx_exc_depth &&
             (!io || (flags & 0x200u));
    irq_restore(flags);
    return ok;
}

int sqlite_engine_run(void (*call)(void *), void *frame, int io)
{
    u32 flags = irq_save();
    if (!sqlite_engine_stack_top || sqlite_engine_active || kctx_irq_depth || kctx_exc_depth ||
        (io && !(flags & 0x200u))) {
        irq_restore(flags);
        return 0;
    }
    sqlite_engine_active = 1;
    irq_restore(flags);
    sqlite_call_on_alt_stack(call, sqlite_engine_stack_top, frame);
    (void)irq_save();
    sqlite_engine_active = 0;
    irq_restore(flags);
    /* STOP belongs to the caller's safe point, after output and teardown. */
    return 1;
}

/* Boot-only adapter: code, pool and alternate stack share one module owner.
 * The candidate stays private until module_start has returned its prefix. */
const SqliteModuleApi *sqlite_module_api = &sqlite_stub_api;
static SqliteModuleApi candidate_api;
static struct module_handle sqlite_handle;
static int init_called, candidate_valid;
static u32 pool_phys;
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
        if (entries[i] < r->text || entries[i] - r->text >= r->text_size) return 0;
    return 1;
}

struct sqlite_init_call { u32 entry; int rc; };
static void call_module_init(void *arg)
{
    struct sqlite_init_call *call = arg;
    int (*init)(void *, SqliteModuleApi *) =
        (int (*)(void *, SqliteModuleApi *))(uptr)call->entry;
    init_called = 1;
    call->rc = init(P2V(pool_phys), &candidate_api);
}

static int sqlite_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    pool_phys = pgalloc_alloc_phys(r->owner, SQLITE_MODULE_POOL_SIZE / PAGE_SIZE);
    if (!pool_phys) return MODULE_NO_PAGES;
    u32 stack = pgalloc_alloc_phys(r->owner, SQLITE_ENGINE_STACK_SIZE / PAGE_SIZE);
    if (!stack) return MODULE_NO_PAGES;
    sqlite_engine_stack_top = (u32)(uptr)P2V(stack) + SQLITE_ENGINE_STACK_SIZE;
    struct sqlite_init_call call = {entry, SQLITE_MISUSE};
    sqlite_engine_run(call_module_init, &call, 0);
    candidate_valid = valid_api(r);
    return !candidate_valid || call.rc != SQLITE_OK;
}

static void call_shutdown(void *arg)
{
    *(int *)arg = candidate_api.shutdown();
}
static int sqlite_quiesce(struct module_record *r)
{
    (void)r;
    sqlite_module_api = &sqlite_stub_api;
    if (sqlite_engine_active) return 0;
    if (init_called) {
        int rc = SQLITE_MISUSE;
        if (!candidate_valid || !sqlite_engine_run(call_shutdown, &rc, 0) ||
            rc != SQLITE_OK) return 0;
    }
    sqlite_engine_stack_top = pool_phys = 0;
    init_called = candidate_valid = 0;
    return 1;
}

int os32_sqlite_start(void)
{
    if (sqlite_module_ready()) return SQLITE_OK;
    if (sqlite_handle.generation) return OS32_ERR_NOSYS;
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_SQLITE_MOD, .name = "sqlite",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, SQLITE_MODULE_ABI_GEN,
            bi->img_crc, SQLITE_MODULE_OPS_VERSION, bi->img_valid,
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
    kprintf(0x07, "[sqlite] module rc=%d pair=%08x\n", rc, bi->img_crc);
    struct module_record *record = module_record_get(sqlite_handle);
    struct memory_page_counts counts;
    memory_boot_page_counts(kdebug.pages ? kdebug.base_pa / PAGE_SIZE : 0,
                            kdebug.pages, &counts);
    /* Separate observation; do not increment the existing boot audit runs.
     * SQLite stays inside SLACK/pool, including quarantined owner pages. */
    kprintf(0x07, "[sqlite] owner=%u state=%u pages=%u body=%u prefix=%u diff=%d\n",
            record ? record->owner : 0, record ? record->state : 0,
            record && record->owner ? ledger_owner_pages(record->owner) : 0,
            record ? record->pages : 0, record ? record->prefix_pages : 0,
            counts.diff);
    return rc;
}
