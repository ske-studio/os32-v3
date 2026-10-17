/* T4a-2 link/size prototype only; never linked into the product kernel.
 * The pool is supplied by the caller, not reserved in the module body. */
#include "os32_sqlite_vfs.h"
extern char *sqlite_module_pool;
int sqlite_module_init(void *pool)
{
    sqlite_module_pool = pool;
    return os32_sqlite_init();
}
const struct {
    unsigned long version;
    int (*initialize)(void *);
} sqlite_module_ops = {1, sqlite_module_init};
