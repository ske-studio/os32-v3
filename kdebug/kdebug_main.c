#include "kernel/module.h"
#include "crt_diag.h"
#include "nic_trace.h"
#include "kernel/kselftest.h"
#include "kernel/v86_gcap.h"
#include "kpeek.h"

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
    .crt_diag_bootlog = crt_diag_bootlog,
    .gcap_capture = v86_gdc_capture,
    .gcap_active = v86_gcap_active,
    .gcap_keep_if = v86_gcap_keep_if,
    .gcap_insn_abort = v86_gcap_insn_abort,
    .gcap_port_passed = v86_gcap_port_passed,
    .gcap_pass_in = v86_gcap_pass_in,
    .gcap_pass_out = v86_gcap_pass_out,
    .gcap_note_in = v86_gcap_note_emul_in,
    .gcap_note_out = v86_gcap_note_emul_out,
    .gcap_detach = v86_gcap_detach,
    .gcap_release = v86_gcap_release,
    .nic_selftest = 0,
    .nic_trace = nic_trace,
    .dbg_call = kpeek_call
};

/* No imports, allocation, device access or registration during init. */
int kdebug_init(struct kdebug_ops *candidate)
{
    *candidate = kdebug_ops;
    return 0;
}
