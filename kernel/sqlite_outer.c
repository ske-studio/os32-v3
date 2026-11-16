/* Typed resident dispatch; the generated API always has callable stubs. */
#define OS32_SQLITE_OUTER_IMPL
#include "os32_sqlite_outer.h"
#include "sqlite_engine.h"
#include "../lib/sqlite3/generated/sqlite_facade.h"
#include <stdarg.h>

#include "../lib/sqlite3/generated/sqlite_outer_calls.inc"

char *os32_sqlite_snprintf(int size, char *out, const char *format, ...)
{
    char *result;
    va_list args;
    va_start(args, format);
    result = os32_sqlite_outer_vsnprintf(size, out, format, args);
    va_end(args);
    return result;
}

_Static_assert(sizeof(u64) == 8, "SQLite binary64 integer wire");

_Static_assert(__builtin_types_compatible_p(
    __typeof__(((SqliteModuleApi *)0)->column_f64_bits),
    u64 (*)(sqlite3_stmt *, int)), "SQLite facade integer return");
