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

_Static_assert(sizeof(u64) == 8, "SQLite binary64 integer wire");

_Static_assert(__builtin_types_compatible_p(
    __typeof__(((SqliteModuleApi *)0)->column_f64_bits),
    u64 (*)(sqlite3_stmt *, int)), "SQLite facade integer return");

_Static_assert(sizeof(double) == sizeof(u64), "SQLite internal binary64");
