/* Real side-effect-free init, and every S1 import replaced with a counter. */
#include "kernel/module.h"
#include "kernel/kselftest.h"
#include "kprintf.h"
#include "kernel/v86_gcap.h"
#include "kdebug/kpeek.h"
#include "kdebug/recover.h"
#include "kdebug/recover_diag.h"
#include "drivers/serial.h"
#include "hwdev_private.h"
#include "kdebug/gfinit.h"
#include "kdebug/recover_wire.h"
static unsigned calls;
static unsigned status_diag_lines;
const struct rec_tx_diag *recover_diag_get(void)
{ static const struct rec_tx_diag diag={0}; return &diag; }
const char *recover_diag_fn(u32 fn) { (void)fn; return "NONE"; }
const char *recover_diag_where(u32 where) { (void)where; return "NONE"; }
#ifdef HWDEV_DISPATCH
static int dispatch_tests(const struct kdebug_ops *);
static unsigned loaded, recover_done, release_calls, module_calls, attach_calls;
static int recover_rc;
#endif
void kprintf(unsigned char color, const char *format, ...)
{
    (void)color; (void)format;
    if(format[0]=='t' && format[1]=='x' && format[2]=='d' && format[3]==' ') {
        const char *expected="txd sid=%u valid=%u seq=%u pos=%u fn=%s rc=%d where=%s sent=%u polls=%u bytes=%u\n";
        unsigned i=0;
        while(expected[i] && expected[i]==format[i])i++;
        status_diag_lines+=expected[i]==format[i] ? 1:100;
    }
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
{ (void)session; calls++;
#ifdef HWDEV_DISPATCH
    recover_done++; if (recover_rc != -2) r->state = REC_VERIFIED;
    return recover_rc ? -1 : 0;
#else
    (void)r; return -1;
#endif
}
#ifdef RECOVER_ROUTE_WIRE
static u32 route_wire_calls;
#endif
int recover_route_test_session(struct recover *r, u32 session)
{
#ifdef RECOVER_ROUTE_WIRE
    route_wire_calls++;
    if (session) return -1;
    r->state = REC_VERIFIED; r->s0_verified = true; r->token.session = 1;
    return 0;
#else
    return recover_now_session(r, session);
#endif
}
int recover_owner_configure(const struct recover_ops *ops, void *opaque,
    const struct recover_profile *profile, bool approved)
{
    (void)ops; (void)opaque; (void)profile; (void)approved; calls++;
#ifdef RECOVER_ROUTE_WIRE
    owner.ops = ops; return 0;
#else
    return -1;
#endif
}
int recover_budget_apply(struct recover_profile *p, u32 k, u32 hz, u32 baud,
    u32 bits, const struct recover_static_work *work)
{ (void)p; (void)k; (void)hz; (void)baud; (void)bits; (void)work; calls++;
#ifdef RECOVER_ROUTE_WIRE
    return 0;
#else
    return -1;
#endif
}
static bool mode_route_active;
bool recover_mode_route_active(const struct recover *r)
{ (void)r; calls++; return mode_route_active; }
int recover_cell_98(struct recover *r, const struct recover_screen_ops *ops, void *opaque)
{ (void)r; (void)ops; (void)opaque; calls++; return -1; }
int recover_screen_98(struct recover *r, const struct recover_screen_ops *ops, void *opaque)
{ (void)r; (void)ops; (void)opaque; calls++; return -1; }
int recover_hal_wait_frame(void *opaque, u32 us)
{ (void)opaque; (void)us; calls++; return -1; }
const struct serial_setup *serial_get_setup(void) { calls++;
#if defined(HWDEV_DISPATCH) || defined(RECOVER_ROUTE_WIRE)
    static const struct serial_setup serial = {.actual=9600};
#ifdef HWDEV_DISPATCH
    attach_calls++;
#endif
    return &serial;
#else
    return 0;
#endif
}
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
const struct hwdev_core_api *hwdev_core_get(void) { calls++; return 0; }
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
        candidate.nic_trace != nic_trace || !candidate.dbg_call || candidate.hwdev_core_get != hwdev_core_get) rc = 1;
#ifdef HWDEV_DISPATCH
    if (dispatch_tests(&candidate)) rc = 1;
#endif
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}

int serial_checked_begin(void) { calls++; return -1; }
int serial_checked_ready(void) { calls++; return -1; }
int serial_checked_put(u32 byte) { (void)byte; calls++; return -1; }
int serial_checked_empty(void) { calls++; return -1; }
void serial_checked_end(void) { calls++; }
int recover_uart_flush(struct recover *r, int (*empty)(void *), void *opaque, u32 frame, u32 polls)
{ (void)r; (void)empty; (void)opaque; (void)frame; (void)polls; calls++; return -1; }
/* Mutation-only trap: a direct GF call must fail dispatch expectations. */
int gfinit_call(u32 op, void *buf, u32 size, const struct recover_ops *base,
                struct recover_transport *transport, const struct recover_profile *s0)
{ (void)op; (void)buf; (void)size; (void)base; (void)transport; (void)s0; calls++; return -1; }

#ifdef HWDEV_DISPATCH
static int device_call(u32 op, void *buf, u32 size, const struct recover_ops *base,
                       struct recover_transport *transport, const struct recover_profile *s0)
{
    module_calls++;
    if (!base || !transport || !s0 || !buf || size != 40 ||
        !base->screen_restore_verify || !transport->uart.wait_frame) return -99;
    *(u32 *)buf = op;
    return (int)op;
}
static void device_release(void)
{
    if (!recover_done || owner.state != REC_VERIFIED) release_calls += 100;
    release_calls++;
}
static const struct hwdev_ops device = {HWDEV_ABI, sizeof(device), device_call, device_release};
const struct hwdev_ops *hwdev_module_ops(void) { calls++; return loaded ? &device : 0; }
static int dispatch_tests(const struct kdebug_ops *ops)
{
    u32 buf[10] = {0};
    static const struct recover_ops base = {0};
    owner.ops = &base;
    for (u32 op = GF_OP_INSPECT; op <= GF_OP_ABORT; op++) {
        if (ops->dbg_call(op, buf, sizeof(buf)) != -1 || module_calls || attach_calls || buf[0] != 0) return 1;
    }
    if (ops->dbg_call(KDEBUG_OP_RECOVER_STATUS, buf, sizeof(buf))) return 1;
    if (status_diag_lines != 1) return 1;
    if (ops->dbg_call(KDEBUG_OP_RECOVER_NOW, buf, sizeof(buf)) || release_calls) return 1;
    loaded = 1;
    for (u32 op = GF_OP_INSPECT; op <= GF_OP_ABORT; op++) {
        if (ops->dbg_call(op, buf, sizeof(buf)) != (int)op || buf[0] != op) return 1;
    }
    if (module_calls != 6) return 1;
    owner.state = REC_PAUSED;
    if (ops->dbg_call(KDEBUG_OP_RECOVER_STATUS, buf, sizeof(buf)) != -1) return 1;
    recover_done = 0;
    if (ops->dbg_call(KDEBUG_OP_RECOVER_NOW, buf, sizeof(buf)) || release_calls != 1) return 1;
    owner.state = REC_PAUSED; recover_rc = -2;
    if (ops->dbg_call(KDEBUG_OP_RECOVER_NOW, buf, sizeof(buf)) != -1 || release_calls != 1) return 1;
    recover_rc = -1;
    if (ops->dbg_call(KDEBUG_OP_RECOVER_NOW, buf, sizeof(buf)) != -1 || release_calls != 2) return 1;
    return 0;
}
#else
const struct hwdev_ops *hwdev_module_ops(void) { calls++; return 0; }
#endif

/* Virtual execution is covered by recover_virtual_host.c. */
int recover_virtual_call(const KdebugRecoverVirtual *q, u32 hz)
{ (void)q; (void)hz; return KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED; }
void recover_virtual_detach(void) {}

bool recover_virtual_allowed(const KdebugRecoverVirtual *q)
{ (void)q; return false; }
