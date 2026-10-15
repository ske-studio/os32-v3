/* Real side-effect-free init, and every S1 import replaced with a counter. */
#include "kernel/module.h"
#include "kernel/kselftest.h"
#include "kprintf.h"
#include "kernel/v86_gcap.h"
static unsigned calls;
void kprintf(unsigned char color, const char *format, ...)
{
    (void)color; (void)format;
    calls++;
}
void crt_diag_bootlog(void) { calls++; }
int kselftest_run(void) { calls++; return 0; }
int kselftest_run_post_exec(void) { calls++; return 0; }
int kselftest_run_post_unicode(void) { calls++; return 0; }
int kselftest_run_post_probe(void) { calls++; return 0; }
void nic_trace(u32 stage) { (void)stage; calls++; }
int v86_gdc_capture(int mode, V86Gcap *out) { (void)mode; (void)out; calls++; return 0; }
int v86_gcap_active(void) { calls++; return 0; }
int v86_gcap_keep_if(void) { calls++; return 0; }
int v86_gcap_insn_abort(int width, u8 op) { (void)width; (void)op; calls++; return 0; }
int v86_gcap_port_passed(u16 port) { (void)port; calls++; return 0; }
int v86_gcap_pass_in(u16 p, int s, u32 *v) { (void)p; (void)s; (void)v; calls++; return 0; }
int v86_gcap_pass_out(u16 p, int s, u32 v) { (void)p; (void)s; (void)v; calls++; return 0; }
void v86_gcap_note_emul_in(u16 p, int s, u32 v) { (void)p; (void)s; (void)v; calls++; }
void v86_gcap_note_emul_out(u16 p, int s, u32 v) { (void)p; (void)s; (void)v; calls++; }
void v86_gcap_detach(void) { calls++; }
void v86_gcap_release(void) { calls++; }
extern const struct kdebug_ops kdebug_ops;

void _start(void)
{
    struct kdebug_ops candidate;
    for (u32 i = 0; i < sizeof(candidate); i++) ((u8 *)&candidate)[i] = 0xa5;
    int rc = kdebug_init(&candidate);
    const u8 *a = (const u8 *)&candidate, *b = (const u8 *)&kdebug_ops;
    for (u32 i = 0; i < sizeof(candidate); i++) if (a[i] != b[i]) rc = 1;
    if (calls || candidate.version != KDEBUG_OPS_VERSION ||
        candidate.crt_diag_bootlog != crt_diag_bootlog || !candidate.selftest_run ||
        !candidate.selftest_post_exec || !candidate.selftest_post_unicode ||
        !candidate.selftest_post_probe ||
        !candidate.gcap_capture || !candidate.gcap_active || !candidate.gcap_keep_if ||
        !candidate.gcap_insn_abort || !candidate.gcap_port_passed ||
        !candidate.gcap_pass_in || !candidate.gcap_pass_out ||
        !candidate.gcap_note_in || !candidate.gcap_note_out ||
        !candidate.gcap_detach || !candidate.gcap_release || candidate.nic_selftest ||
        candidate.nic_trace != nic_trace) rc = 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
