/* 期待の出所: PowerPC KAPI db 詳細設計 §2-2・§2-3 の表 */
#include "sqlite_engine.h"
#include "sqlite_engine_body.h"
#include "pgalloc.h"
#include "kprintf.h"
#include "../lib/sqlite3/generated/sqlite_facade.h"

int services_public;
enum services_stage services_stage;
const SqliteModuleApi sqlite_stub_api;
const SqliteModuleApi *sqlite_module_api;
static int start_result, stop_result, init_result, check_result;
static int starts, stops, checks, logs, failed;
static SqliteModuleApi *candidate;

static int shutdown_stub(void) { return SQLITE_OK; }
int sqlite_module_init(void *pool, SqliteModuleApi *out)
{
    (void)pool;
    out->shutdown = shutdown_stub;
    return init_result;
}

int sqlite_engine_body_start(u32 owner, sqlite_body_init_fn *init,
                             SqliteModuleApi *out, int *init_rc)
{
    starts++;
    failed |= owner != LEDGER_OWNER_KERNEL || init != sqlite_module_init;
    candidate = out;
    *init_rc = init(0, out);
    return start_result;
}

int os32_sqlite_outer_boot_check(int live, int reserve)
{
    checks++;
    failed |= live || reserve || services_public || sqlite_module_api != candidate;
    return check_result;
}

int sqlite_engine_body_stop(int (*shutdown)(void))
{
    stops++;
    failed |= shutdown != shutdown_stub || sqlite_module_api != &sqlite_stub_api || services_public;
    return stop_result;
}

void kprintf(u8 attr, const char *format, ...)
{
    (void)attr;
    (void)format;
    va_list args;
    va_start(args, format);
    failed |= va_arg(args, int) != init_result;
    failed |= va_arg(args, int) != stop_result;
    va_end(args);
    logs++;
}

static int exercise(void)
{
    starts = stops = checks = logs = services_public = 0;
    services_stage = SERVICES_ROOT_READY;
    sqlite_module_api = &sqlite_stub_api;
    int rc = sqlite_static_boot();
    int checked = start_result == SQLITE_BODY_OK && init_result == SQLITE_OK;
    int published = checked && check_result == SQLITE_OK;
    int unwind = !published && (start_result == SQLITE_BODY_OK || start_result == SQLITE_BODY_INIT);
    int expected = unwind ? SQLITE_BODY_INIT : start_result;
    return failed || rc != expected || starts != 1 || checks != checked ||
           stops != unwind || logs != unwind || services_public != published ||
           services_stage != (published ? SERVICES_SQLITE_ONLY : SERVICES_ROOT_READY) ||
           sqlite_module_api != (published ? candidate : &sqlite_stub_api);
}

void _start(void)
{
    int result = 0;
    for (start_result = SQLITE_BODY_OK; start_result <= SQLITE_BODY_INIT; start_result++)
        for (stop_result = SQLITE_STOP_DONE; stop_result <= SQLITE_STOP_REMAP; stop_result++)
            for (init_result = 0; init_result <= 1; init_result++)
                for (check_result = 0; check_result <= 1; check_result++)
                    result |= exercise();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result));
    __builtin_unreachable();
}
