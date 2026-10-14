#include "kernel/module.h"
#include "crt_diag.h"

/* mkmod derives the export records from this initializer. */
const struct kdebug_ops kdebug_ops = {
    .version = KDEBUG_OPS_VERSION,
    .crt_diag_bootlog = crt_diag_bootlog
};

/* No imports, allocation, device access or registration during init. */
int kdebug_init(struct kdebug_ops *candidate)
{
    *candidate = kdebug_ops;
    return 0;
}
