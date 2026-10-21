/* Real side-effect-free init, and every S1 import replaced with a counter. */
#include "kernel/module.h"
#include "kernel/kselftest.h"
#include "kprintf.h"
#include "kernel/v86_gcap.h"
#include "kdebug/kpeek.h"
#include "kdebug/recover.h"
#include "drivers/serial.h"
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
i32 kpeek_call(u32 op, void *buf, u32 size) { (void)op; (void)buf; (void)size; calls++; return 0; }
/* T3 entry adapters must remain dormant during module init. Each dependency
 * counts an unexpected call; no hardware callback is executed by this test. */
static struct recover owner;
struct recover *recover_owner(void) { calls++; return &owner; }
int recover_now_session(struct recover *r, u32 session)
{ (void)r; (void)session; calls++; return -1; }
int recover_owner_configure(const struct recover_ops *ops, void *opaque,
    const struct recover_profile *profile, bool approved)
{ (void)ops; (void)opaque; (void)profile; (void)approved; calls++; return -1; }
int recover_budget_apply(struct recover_profile *p, u32 k, u32 hz, u32 baud,
    u32 bits, const struct recover_static_work *work)
{ (void)p; (void)k; (void)hz; (void)baud; (void)bits; (void)work; calls++; return -1; }
int recover_screen_98(struct recover *r, const struct recover_screen_ops *ops, void *opaque)
{ (void)r; (void)ops; (void)opaque; calls++; return -1; }
int recover_hal_wait_frame(void *opaque, u32 us)
{ (void)opaque; (void)us; calls++; return -1; }
const struct serial_setup *serial_get_setup(void) { calls++; return 0; }
int serial_gate_active(void) { calls++; return 0; }
int serial_trygetchar(void) { calls++; return -1; }
unsigned int serial_cmd_port(int mode) { (void)mode; calls++; return 0; }
unsigned int serial_data_port(int mode) { (void)mode; calls++; return 0; }
u8 serial_txrdy_mask(int mode) { (void)mode; calls++; return 0; }
void console_hw_cursor_enable(void) { calls++; }
void display_restore_98(void) { calls++; }
int display_recover_ready(void) { calls++; return -1; }
int display_recover_cell_read(u32 *value) { (void)value; calls++; return -1; }
int display_recover_cell_write(u32 value) { (void)value; calls++; return -1; }
u32 kdi_get_tick_count(void) { calls++; return 0; }
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
        candidate.nic_trace != nic_trace || !candidate.dbg_call) rc = 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
