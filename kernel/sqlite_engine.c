#define OS32_SQLITE_OUTER_IMPL
#include "sqlite_engine.h"
#include "io.h"
#include "irq/irq.h"
#include "../mm/pgalloc.h"
#include "../mm/paging.h"
#include "memmap.h"
#include "module/module_runtime.h"
#include "../mm/memory_boot.h"
#include "module/module.h"
#include "bootinfo.h"
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
    sqlite_call_on_alt_stack(call, sqlite_engine_stack_top, frame);
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
    if (services_boot_fault == SERVICES_FAULT_POOL) return MODULE_NO_PAGES;
    pool_phys = pgalloc_alloc_phys(r->owner, SQLITE_MODULE_POOL_SIZE / PAGE_SIZE);
    if (!pool_phys) return MODULE_NO_PAGES;
    if (services_boot_fault == SERVICES_FAULT_STACK) return MODULE_NO_PAGES;
    u32 stack = pgalloc_alloc_phys(r->owner, SQLITE_ENGINE_STACK_SIZE / PAGE_SIZE + 1);
    if (!stack) return MODULE_NO_PAGES;
    paging_sqlite_stack_guard = (u32)(uptr)P2V(stack);
    if (paging_set_not_present(paging_sqlite_stack_guard,
                              paging_sqlite_stack_guard + PAGE_SIZE - 1) ||
        paging_is_present(paging_sqlite_stack_guard)) return MODULE_NO_PAGES;
    sqlite_engine_stack_top = paging_sqlite_stack_guard + PAGE_SIZE + SQLITE_ENGINE_STACK_SIZE;
    /* Fill once, before module init uses the stack. Never repaint on entry. */
    kmemset((void *)(uptr)(sqlite_engine_stack_top - SQLITE_ENGINE_STACK_SIZE),
            SQLITE_STACK_MARK, SQLITE_ENGINE_STACK_SIZE);
    sqlite_stack_peak = 0;
    struct sqlite_init_call call = {entry, SQLITE_MISUSE};
    sqlite_engine_run(call_module_init, &call, 0);
    candidate_valid = valid_api(r);
    return !candidate_valid || call.rc != SQLITE_OK ||
           services_boot_fault == SERVICES_FAULT_SQLITE_INIT;
}

static void call_shutdown(void *arg)
{
    *(int *)arg = services_boot_fault == SERVICES_FAULT_SHUTDOWN
               ? SQLITE_BUSY : candidate_api.shutdown();
}
static int sqlite_quiesce(struct module_record *r)
{
    (void)r;
    sqlite_module_api = &sqlite_stub_api;
    if (sqlite_engine_active || cleanup_blocked) return 0;
    if (init_called) {
        int rc = SQLITE_MISUSE;
        if (!candidate_valid || !sqlite_engine_run(call_shutdown, &rc, 0) ||
            rc != SQLITE_OK) return 0;
    }
    /* The owner reclaimer writes freed pages; restore the guard before it runs. */
    if (paging_sqlite_stack_guard &&
        paging_set_page(paging_sqlite_stack_guard,
                        V2P((void *)(uptr)paging_sqlite_stack_guard), PAGE_RW)) return 0;
    paging_sqlite_stack_guard = 0;
    sqlite_engine_stack_top = pool_phys = 0;
    init_called = candidate_valid = 0;
    return 1;
}

int os32_sqlite_start(void)
{
    if (sqlite_module_ready()) return SQLITE_OK;
    if (sqlite_handle.generation) return OS32_ERR_NOSYS;
    const struct boot_common *bi = boot_common_get();
    const struct module_request request = {
        .path = SYS_SQLITE_MOD, .name = "sqlite",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, SQLITE_MODULE_ABI_GEN,
            bi->image_crc, SQLITE_MODULE_OPS_VERSION, bi->image_valid,
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
    kprintf(KLOG_ATTR, "[sqlite] module rc=%d pair=%08x\n", rc, bi->image_crc);
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

/* Core owns the immutable boot snapshot; a switch probe never updates it. */
static FepMeta boot_meta;
static u32 boot_reserve_kb;
static int boot_config_saved, boot_user_rc = OS32_ERR_AGAIN;
static char boot_user_path[OS32_MAX_PATH];

int fep_boot_config_get(FepMeta *out, u32 *reserve_kb)
{
    if (!out || !reserve_kb) return OS32_ERR_INVAL;
    if (!boot_config_saved) return OS32_ERR_AGAIN;
    *out = boot_meta;
    *reserve_kb = boot_reserve_kb;
    return 0;
}

int fep_boot_user_path_get(char *out, u32 capacity)
{
    if (!out || !capacity) return OS32_ERR_INVAL;
    if (boot_user_rc) return boot_user_rc;
    u32 n = kstrlen(boot_user_path);
    if (n >= capacity) return OS32_ERR_INVAL;
    kmemcpy(out, boot_user_path, n + 1);
    return 0;
}

static int probe_close(Os32SqliteGroup *group, sqlite3 *db, sqlite3_stmt *stmt)
{
    int rc = os32_sqlite_outer_group_begin_close(group);
    if (rc) return OS32_ERR_BUSY;
    if (stmt) (void)os32_sqlite_finalize(stmt); /* destroys even after step error */
    rc = db ? os32_sqlite_close(db) : SQLITE_OK;
    return os32_sqlite_outer_group_finish(group, rc) ? OS32_ERR_BUSY : 0;
}

static int meta_uint(const unsigned char *s, int n, u32 *out)
{
    u32 value = 0;
    if (!s || !n) return OS32_ERR_INVAL;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9' ||
            value > (0xffffffffu - (s[i] - '0')) / 10u) return OS32_ERR_INVAL;
        value = value * 10u + s[i] - '0';
    }
    *out = value;
    return 0;
}

int fep_meta_probe(const char *path, FepMeta *out)
{
    if (!path || !out) return OS32_ERR_INVAL;
    u8 header[FEP_DICT_HEADER_SIZE];
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return fd == VFS_ERR_NOTFOUND ? OS32_ERR_NOTFOUND : OS32_ERR_IO;
    int n = vfs_read_fd(fd, header, sizeof(header));
    vfs_close(fd);
    if (n != sizeof(header)) return OS32_ERR_IO;
    if (memcmp(header, "SQLite format 3", 16) ||
        ((u32)header[16] << 8 | header[17]) != FEP_DICT_PAGE_SIZE)
        return OS32_ERR_INVAL;
    u32 version = (u32)header[60] << 24 | (u32)header[61] << 16 |
                  (u32)header[62] << 8 | header[63];
    if (version > FEP_DICT_FORMAT_VERSION) return OS32_ERR_INVAL;
    FepMeta meta = {0};
    meta.format = version;
    meta.variant = !kstrcmp(path, FEP_DICT_PATH_S) ? 'S' :
                   !kstrcmp(path, FEP_DICT_PATH_M) ? 'M' :
                   !kstrcmp(path, FEP_DICT_PATH_L) ? 'L' : 0;
    Os32SqliteGroup group;
    sqlite3 *db = 0;
    sqlite3_stmt *stmt = 0;
    int rc = os32_sqlite_outer_group_acquire(OS32_SQLITE_RESIDENT, 0, &group);
    if (rc) return rc == SQLITE_FULL || rc == SQLITE_BUSY ? OS32_ERR_BUSY : OS32_ERR_IO;
    rc = os32_sqlite_outer_group_open(&group, path, SQLITE_OPEN_READONLY, &db);
    if (rc) { rc = OS32_ERR_IO; goto done; }
    /* Validate the legacy table without scanning dictionary rows. */
    rc = os32_sqlite_prepare_v2(db, "SELECT yomi,kanji,pos_id,cost FROM dict LIMIT 0",
                                -1, &stmt, 0);
    if (rc) { rc = OS32_ERR_IO; goto done; }
    rc = os32_sqlite_finalize(stmt);
    stmt = 0;
    if (rc) { rc = OS32_ERR_IO; goto done; }
    if (!version) {
        meta.min_reader = FEP_DICT_READER_VERSION;
        meta.pos_version = FEP_DICT_POS_VERSION;
        meta.reserve_kb = FEP_DICT_RESERVE_KB;
        meta.cache_pages = FEP_DICT_CACHE_PAGES;
        kstrncpy(meta.dict_id, FEP_DICT_ID, sizeof(meta.dict_id));
    } else {
        static const char *const keys[] = {
            "format_version", "min_fep_version", "dict_id", "kind", "language",
            "charset", "variant", "source", "source_version", "license",
            "attribution", "build_tool", "build_date", "entry_count",
            "pos_table_version", "cost_scale", "mem_reserve_kb", "cache_pages", "checksum"
        };
        u32 seen = 0;
        rc = os32_sqlite_prepare_v2(db, "SELECT key,value FROM meta", -1, &stmt, 0);
        if (rc) { rc = OS32_ERR_IO; goto done; }
        int step;
        while ((step = os32_sqlite_step(stmt)) == SQLITE_ROW) {
            const char *key = (const char *)os32_sqlite_column_text(stmt, 0);
            const unsigned char *value = os32_sqlite_column_text(stmt, 1);
            int bytes = os32_sqlite_column_bytes(stmt, 1);
            if (!key || !value) { rc = OS32_ERR_INVAL; goto done; }
            u32 i;
            for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
                if (!kstrcmp(key, keys[i])) break;
            if (i == sizeof(keys) / sizeof(keys[0])) continue;
            if (bytes <= 0 || seen & (1u << i)) { rc = OS32_ERR_INVAL; goto done; }
            seen |= 1u << i;
            u32 number = 0;
            if (i == 0 || i == 1 || i == 13 || i == 14 || i == 16 || i == 17) {
                rc = meta_uint(value, bytes, &number);
                if (rc) goto done;
            } else if (kstrlen((const char *)value) != (u32)bytes) {
                rc = OS32_ERR_INVAL; goto done;
            }
            switch (i) {
            case 0: if (number != version) rc = OS32_ERR_INVAL; break;
            case 1: meta.min_reader = number;
                if (number > FEP_DICT_READER_VERSION) rc = OS32_ERR_INVAL;
                break;
            case 2:
                if ((u32)bytes >= sizeof(meta.dict_id)) rc = OS32_ERR_INVAL;
                else kstrncpy(meta.dict_id, (const char *)value, sizeof(meta.dict_id));
                break;
            case 3: if (kstrcmp((const char *)value, "system")) rc = OS32_ERR_INVAL; break;
            case 4: if (kstrcmp((const char *)value, "ja")) rc = OS32_ERR_INVAL; break;
            case 5: if (kstrcmp((const char *)value, "UTF-8")) rc = OS32_ERR_INVAL; break;
            case 6:
                if (bytes != 1 || (value[0] != 'S' && value[0] != 'M' && value[0] != 'L') ||
                    (meta.variant && meta.variant != value[0])) rc = OS32_ERR_INVAL;
                else meta.variant = value[0];
                break;
            case 13: meta.entries = number; break;
            case 14: meta.pos_version = number;
                if (number != FEP_DICT_POS_VERSION) rc = OS32_ERR_INVAL;
                break;
            case 15: if (kstrcmp((const char *)value, FEP_DICT_COST_SCALE)) rc = OS32_ERR_INVAL; break;
            case 16: meta.reserve_kb = number; break;
            case 17: meta.cache_pages = number; break;
            case 18:
                if (bytes != 71 || memcmp(value, "sha256:", 7)) rc = OS32_ERR_INVAL;
                else for (int j = 7; j < bytes; j++)
                    if (!((value[j] >= '0' && value[j] <= '9') ||
                          (value[j] >= 'a' && value[j] <= 'f') ||
                          (value[j] >= 'A' && value[j] <= 'F'))) rc = OS32_ERR_INVAL;
                break;
            }
            if (rc) goto done;
        }
        if (step != SQLITE_DONE) { rc = OS32_ERR_IO; goto done; }
        if (seen != (1u << (sizeof(keys) / sizeof(keys[0]))) - 1u) {
            rc = OS32_ERR_NOTFOUND; goto done;
        }
        if (!meta.reserve_kb || !meta.cache_pages ||
            meta.reserve_kb > 0x7fffffffu / KIB ||
            meta.cache_pages > meta.reserve_kb ||
            FEP_USER_CACHE_PAGES > meta.reserve_kb - meta.cache_pages) {
            rc = OS32_ERR_INVAL; goto done;
        }
    }
done:;
    int close_rc = probe_close(&group, db, stmt);
    if (close_rc) return close_rc;
    if (!rc) *out = meta;
    return rc;
}

/* Missing/unreadable reserve settings retain the metadata default. The user
 * path snapshot retains its read error so learning can report that failure. */
static int services_settings(int *reserve, int *present)
{
    Os32SqliteGroup group;
    sqlite3 *db = 0;
    sqlite3_stmt *stmt = 0;
    boot_user_rc = 0;
    kstrncpy(boot_user_path, FEP_USER_PATH, sizeof(boot_user_path));
    int rc = os32_sqlite_outer_group_acquire(OS32_SQLITE_RESIDENT, 0, &group);
    if (rc) { boot_user_rc = OS32_ERR_IO; return rc; }
    rc = os32_sqlite_outer_group_open(&group, SYS_SETTINGS_DB, SQLITE_OPEN_READONLY, &db);
    int has_settings = 0;
    if (rc) {
        OS32_Stat st;
        rc = vfs_stat(SYS_SETTINGS_DB, &st) == VFS_ERR_NOTFOUND ? 0 : OS32_ERR_IO;
    } else {
        rc = os32_sqlite_prepare_v2(db,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='settings'",
            -1, &stmt, 0);
        if (!rc) {
            int step = os32_sqlite_step(stmt);
            if (step == SQLITE_ROW) has_settings = 1;
            else if (step != SQLITE_DONE) rc = OS32_ERR_IO;
        }
        if (os32_sqlite_finalize(stmt)) rc = OS32_ERR_IO;
        stmt = 0;
    }
    if (!rc && has_settings && !os32_sqlite_prepare_v2(db,
            "SELECT ival FROM settings WHERE scope='system' AND key='fep_mem_reserve_kb' AND type=0",
            -1, &stmt, 0) && os32_sqlite_step(stmt) == SQLITE_ROW) {
        int kb = os32_sqlite_column_int(stmt, 0);
        *present = 1;
        if (kb < OS32_SQLITE_FEP_RESERVE_MIN / (int)KIB) {
            kprintf(KLOG_ATTR, "[services] fep_mem_reserve_kb below minimum: %d; using %d KiB\n",
                    kb, OS32_SQLITE_FEP_RESERVE_MIN / (int)KIB);
            *reserve = OS32_SQLITE_FEP_RESERVE_MIN;
        } else {
            *reserve = kb <= (int)(0x7fffffffu / KIB) ? kb * (int)KIB : -1;
        }
    }
    if (stmt) { (void)os32_sqlite_finalize(stmt); stmt = 0; }
    if (!rc && has_settings) rc = os32_sqlite_prepare_v2(db,
        "SELECT tval FROM settings WHERE scope='system' AND key='fep_user_dict_path' AND type=1",
        -1, &stmt, 0);
    if (!rc && has_settings) {
        int step = os32_sqlite_step(stmt);
        if (step == SQLITE_ROW) {
            const char *text = (const char *)os32_sqlite_column_text(stmt, 0);
            int bytes = os32_sqlite_column_bytes(stmt, 0);
            if (!text || bytes < 0 || (u32)bytes >= sizeof(boot_user_path) ||
                kstrlen(text) != (u32)bytes) rc = OS32_ERR_INVAL;
            else kmemcpy(boot_user_path, text, bytes + 1);
        } else if (step != SQLITE_DONE) rc = OS32_ERR_IO;
    }
    if (stmt && os32_sqlite_finalize(stmt)) rc = OS32_ERR_IO;
    boot_user_rc = rc > 0 ? OS32_ERR_IO : rc;
    rc = os32_sqlite_outer_group_begin_close(&group);
    if (rc) return rc;
    rc = services_boot_fault == SERVICES_FAULT_SETTINGS_CLOSE ? SQLITE_BUSY
         : db ? os32_sqlite_close(db) : SQLITE_OK;
    return os32_sqlite_outer_group_finish(&group, rc);
}

static const char *services_state_name(void);

void services_boot(void)
{
    /* Boot only, including failure: never retry an isolated lifetime. */
    if (services_stage != SERVICES_UNICODE) return;
    u32 start_tick = tick_count;
    int reserve = OS32_SQLITE_FEP_RESERVE, reserve_present = 0;
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
    rc = services_settings(&reserve, &reserve_present);
    if (rc) goto rollback;
    rc = os32_sqlite_outer_boot_check(0, 0);
    if (rc) goto rollback;
    services_stage = SERVICES_SETTINGS_CLOSED;
    kprintf(KLOG_ATTR, "[services] SETTINGS_CLOSED tick=%u\n", tick_count);
    if (no_dictionary) {
        /* No live resident group and no later FEP load: boot_check(0, 0)
         * has released the FEP reservation along with settings. */
        reserve = 0;
        services_stage = SERVICES_SQLITE_ONLY;
        services_public = 1;
        kprintf(KLOG_ATTR, "[services] SQLITE_ONLY: no FEP dictionary\n");
        goto report;
    }
    FepMeta meta;
    rc = fep_meta_probe(IME_DICT_PATH, &meta);
    if (rc) goto rollback;
    if (!reserve_present) {
        u32 kb = meta.reserve_kb;
        if (kb < FEP_DICT_MIN_RESERVE_KB) {
            kprintf(KLOG_ATTR, "[services] metadata reserve below minimum: %u; using %u KiB\n",
                    kb, FEP_DICT_MIN_RESERVE_KB);
            kb = FEP_DICT_MIN_RESERVE_KB;
        }
        reserve = kb <= 0x7fffffffu / KIB ? (int)(kb * KIB) : -1;
    }
    if (services_boot_fault == SERVICES_FAULT_RESERVE) reserve = SQLITE_MODULE_POOL_SIZE;
    rc = os32_sqlite_outer_boot_check(0, reserve);
    if (rc) goto rollback;
    boot_meta = meta;
    boot_reserve_kb = (u32)reserve / KIB;
    boot_config_saved = 1;
    rc = fep_module_load(sqlite_handle);
    kprintf(KLOG_ATTR, "[IME] boot dictionary rc=%d\n", rc);
    if (rc) goto rollback;
    services_stage = SERVICES_FEP_INTERNAL_READY;
    kprintf(KLOG_ATTR, "[services] FEP_INTERNAL_READY tick=%u\n", tick_count);
    rc = os32_sqlite_outer_boot_check(ime_boot_connections(), reserve);
    if (rc) goto rollback;
    if (services_boot_fault == SERVICES_FAULT_FEP_CLOSE ||
        services_boot_fault == SERVICES_FAULT_SHUTDOWN) {
        rc = MODULE_INIT_FAILED;
        goto rollback;
    }
    services_stage = SERVICES_READY;
    services_public = 1;
    goto report;

rollback:;
    enum services_stage failed_stage = services_stage;
    services_public = 0;
    boot_config_saved = 0;
    boot_user_rc = OS32_ERR_AGAIN;
    /* FEP statements/groups must disappear while internal exports still work. */
    cleanup_blocked = fep_module_abort() != 0;
    if (sqlite_module_ready() && os32_sqlite_outer_boot_check(0, 0))
        cleanup_blocked = 1;
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
