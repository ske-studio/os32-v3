#include "sqlite_engine.h"
#include "sqlite_engine_body.h"
#include "pgalloc.h"
#include "kprintf.h"
#include "../lib/sqlite3/generated/sqlite_facade.h"

extern sqlite_body_init_fn sqlite_module_init;
static SqliteModuleApi static_api;

int sqlite_static_boot(void)
{
    int init_rc;
    int rc = sqlite_engine_body_start(LEDGER_OWNER_KERNEL, sqlite_module_init,
                                      &static_api, &init_rc);
    if (rc != SQLITE_BODY_OK && rc != SQLITE_BODY_INIT) return rc;
    if (rc == SQLITE_BODY_OK && init_rc == SQLITE_OK) {
        sqlite_module_api = &static_api;
        if (os32_sqlite_outer_boot_check(0, 0) == SQLITE_OK) {
            services_stage = SERVICES_SQLITE_ONLY;
            services_public = 1;
            return SQLITE_BODY_OK;
        }
        sqlite_module_api = &sqlite_stub_api;
    }
    int stop = sqlite_engine_body_stop(static_api.shutdown);
    kprintf(KLOG_ATTR, "[sqlite] static init=%d stop=%d\n", init_rc, stop);
    return SQLITE_BODY_INIT;
}
