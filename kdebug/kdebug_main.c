#include "kernel/module.h"
#include "crt_diag.h"
#include "kernel/kselftest.h"

static void selftest_run(void) { (void)kselftest_run(); }
static void selftest_post_exec(void) { (void)kselftest_run_post_exec(); }
static void selftest_post_unicode(void) { (void)kselftest_run_post_unicode(); }
static void selftest_post_probe(void) { (void)kselftest_run_post_probe(); }

/* mkmod derives the export records from this initializer. */
const struct kdebug_ops kdebug_ops = {
    .version = KDEBUG_OPS_VERSION,
    .selftest_run = selftest_run,
    .selftest_post_exec = selftest_post_exec,
    .selftest_post_unicode = selftest_post_unicode,
    .selftest_post_probe = selftest_post_probe,
    .crt_diag_bootlog = crt_diag_bootlog
};

/* No imports, allocation, device access or registration during init. */
int kdebug_init(struct kdebug_ops *candidate)
{
    *candidate = kdebug_ops;
    return 0;
}
