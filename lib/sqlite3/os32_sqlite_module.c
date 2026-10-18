/* POST_ROOT SQLite body. A single typed initialize export fills the facade. */
#define OS32_SQLITE_OUTER_IMPL
#include "generated/sqlite_facade.h"
extern char *sqlite_module_pool;
#include "generated/sqlite_facade_bind.inc"
int sqlite_module_init(void *pool, SqliteModuleApi *out)
{
    sqlite_module_pool = pool;
    *out = api;
    return os32_sqlite_init();
}
const struct {
    unsigned long version;
    int (*initialize)(void *, SqliteModuleApi *);
} sqlite_module_ops = {SQLITE_MODULE_OPS_VERSION, sqlite_module_init};
