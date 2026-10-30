#include "kernel/module/module.h"
#include "hwdev/generated/hwdev_contract.h"
#include "crt_diag.h"
#include "nic_trace.h"
#include "kernel/kselftest.h"
#include "kernel/v86/v86_gcap.h"
#include "kpeek.h"

#include "recover.h"
#include "recover_diag.h"
#include "recover_wire.h"
#include "recover_virtual.h"
#include "gfinit.h"
#include "kernel/module/hwdev_module.h"
#include "kernel/console/display_restore.h"
#include "arch/x86/kernel/idt.h"
#include "drivers/serial.h"
#include "drivers/serial_plan.h"
#include "lib/serial_diag.h"
#include "console.h"
#include "memmap.h"
#include "io.h"
#include "generated/kdebug_import_macros.h"

static u32 recovery_ticks(void *opaque) { (void)opaque; return tick_count; }
static int recovery_route(void *opaque, u32 *value)
{ (void)opaque; *value = inp(RECOVER_ROUTE_PORT); return 0; }
static int recovery_route_write(void *opaque, u32 value)
{
    (void)opaque;
    if (value != RECOVER_ROUTE_98 &&
        (value != RECOVER_ROUTE_RGB_IN || !recover_mode_route_active(recover_owner()))) return -1;
    outp(RECOVER_ROUTE_PORT, value);
    return 0;
}
static int recovery_cui(void *opaque)
{ (void)opaque; return display_recover_ready(); }
static int recovery_ready(void *opaque)
{
    (void)opaque;
    return display_recover_ready() || (inp(RECOVER_ROUTE_PORT) & RECOVER_ROUTE_MASK) ? -1 : 0;
}
static int recovery_restore(void *opaque)
{ (void)opaque; display_restore_98(); return 0; }
static int recovery_cell_read(void *opaque, u32 *value)
{ (void)opaque; return display_recover_cell_read(value); }
static int recovery_cell_write(void *opaque, u32 value)
{ (void)opaque; return display_recover_cell_write(value); }
/* The HAL preserves the CUI TVRAM contents. Re-enable/synchronize the cursor
 * after restoring display START; no shadow console buffer needs repainting. */
static int recovery_redraw(void *opaque)
{ (void)opaque; console_hw_cursor_enable(); return 0; }
static int recovery_local(void *opaque) { (void)opaque; return 0; }
static const struct recover_screen_ops recovery_screen = {
    recovery_ready, recovery_ready, recovery_restore,
    recovery_cell_read, recovery_cell_write, recovery_redraw
};
static int recovery_screen_verify(void *opaque)
{ (void)opaque; return recover_screen_98(recover_owner(), &recovery_screen, 0); }
static int recovery_mode_cell(void *opaque, struct recover *r, u32 *actual)
{
    (void)opaque;
    int rc=recover_cell_98(r,&recovery_screen,0);
    *actual=!rc;
    return rc;
}
static const struct recover_ops recovery_ops = {
    .ticks = recovery_ticks, .start_ready = recovery_cui,
    .route_read = recovery_route, .route_write = recovery_route_write,
    .screen_safe = recovery_ready, .screen_restore_verify = recovery_screen_verify,
    .mode_cell = recovery_mode_cell, .local_cleanup = recovery_local
};
static int recovery_rx(void *opaque)
{ (void)opaque; return serial_gate_active() ? -1 : serial_trygetchar(); }
static int recovery_tx_ready(void *opaque)
{ (void)opaque; return serial_checked_ready(); }
static int recovery_tx_put(void *opaque, u32 byte)
{ (void)opaque; return serial_checked_put(byte); }
static int recovery_tx_begin(void *opaque)
{ (void)opaque; return serial_checked_begin(); }
static int recovery_tx_empty(void *opaque)
{ (void)opaque; return serial_checked_empty(); }
static int recovery_tx_end(struct recover *r, void *opaque, int rc)
{
    if (!rc) rc = recover_uart_flush(r, recovery_tx_empty, opaque,
                                    r->transport->frame_us, r->transport->max_polls);
    serial_checked_end();
    return rc;
}
#define RECOVERY_UART_POLL_LIMIT RECOVER_CLEANUP_TRANSPORT_POLLS
static struct recover_transport recovery_transport = {
    .get = recovery_rx,
    .uart = {recovery_tx_ready, recovery_tx_put, recover_hal_wait_frame},
    .max_polls = RECOVERY_UART_POLL_LIMIT,
    .begin = recovery_tx_begin, .end = recovery_tx_end
};
static struct recover_profile recovery_s0;
static int recovery_attach(void)
{
    struct recover *r = recover_owner();
    const struct serial_setup *setup = serial_get_setup();
    if (serial_gate_active() || !setup || !setup->actual) return -1;
    if (!r->ops) {
        /* Includes REC state/terminal/RX rows plus the bounded HAL callbacks.
         * The fixed S0 envelope remains 4096 bytes/10 s/16384 polls. */
        const struct recover_static_work work = {RECOVER_BUDGET_BYTES_S0, RECOVER_BUDGET_S0_WAIT_MS, RECOVER_BUDGET_S0_POINTS,
            RECOVER_BUDGET_BYTES_S0, RECOVER_BUDGET_S0_WAIT_MS, RECOVER_BUDGET_S0_POINTS};
        recovery_s0.version = RECOVER_PROFILE_VERSION;
        if (recover_budget_apply(&recovery_s0, REC_BUDGET_S0, PIT_HZ,
                setup->actual, RECOVER_UART_FRAME_BITS, &work) ||
            recover_owner_configure(&recovery_ops, 0, &recovery_s0, false)) return -1;
    }
    if (!r->transport) {
        recovery_transport.frame_us = RECOVER_UART_FRAME_US / setup->actual +
                                      (RECOVER_UART_FRAME_US % setup->actual != 0);
        r->transport = &recovery_transport;
    }
    return 0;
}
static i32 recovery_call(u32 op, void *buf, u32 size)
{
    struct recover *r = recover_owner();
    if ((op >= GF_OP_INSPECT && op <= GF_OP_ABORT) || op == GF_OP_MODESET || op == GF_OP_C_ONLY) {
        const struct hwdev_ops *ops = hwdev_module_ops();
        if (!ops || recovery_attach()) return -1;
        int rc = ops->call(op, buf, size, &recovery_ops, &recovery_transport, &recovery_s0);
        recover_virtual_detach();
        return rc;
    }
    const KdebugRecoverVirtual *req = 0;
    if (op == KDEBUG_OP_RECOVER_VIRTUAL) {
        req = buf;
        if (!req || size != sizeof(*req) || req->version != RECOVER_VIRTUAL_VERSION)
            return KDEBUG_RECOVER_VIRTUAL_INVALID;
        for (u32 i = 0; i < sizeof(req->reserved) / sizeof(req->reserved[0]); i++)
            if (req->reserved[i]) return KDEBUG_RECOVER_VIRTUAL_INVALID;
    }
    if (r->state == REC_PAUSED && op != KDEBUG_OP_RECOVER_NOW &&
        op != KDEBUG_OP_RECOVER_RESTORE &&
        !(op == KDEBUG_OP_RECOVER_VIRTUAL && recover_virtual_allowed(req))) return -1;
    if (op == KDEBUG_OP_RECOVER_VIRTUAL) {
        if (req->action == RECOVER_VIRTUAL_BEGIN &&
            req->case_id != RECOVER_VIRTUAL_NORMAL && req->case_id != RECOVER_VIRTUAL_FAULT &&
            req->case_id != RECOVER_VIRTUAL_TRIAL_EXPIRE && req->case_id != RECOVER_VIRTUAL_POLL_STOP &&
            req->case_id != RECOVER_VIRTUAL_TXBUSY_STOP)
            return KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED;
        if (recovery_attach()) return -1;
        return recover_virtual_call(req, PIT_HZ);
    }
    if (op != KDEBUG_OP_RECOVER_STATUS && op != KDEBUG_OP_RECOVER_NOW &&
        op != KDEBUG_OP_RECOVER_ROUTE_TEST && op != KDEBUG_OP_RECOVER_RESTORE)
        return kpeek_call(op, buf, size);
    if (!buf || size != sizeof(KdebugRecover)) return -1;
    KdebugRecover *out = buf;
    if (op == KDEBUG_OP_RECOVER_ROUTE_TEST &&
        (r->state != REC_IDLE || r->token.session || out->session ||
         (r->ops && r->ops != &recovery_ops))) return -1;
    if (recovery_attach()) return -1;
    if (op == KDEBUG_OP_RECOVER_NOW || op == KDEBUG_OP_RECOVER_ROUTE_TEST ||
        op == KDEBUG_OP_RECOVER_RESTORE) {
        enum recover_state before = r->state;
        int rc = op == KDEBUG_OP_RECOVER_ROUTE_TEST ?
            recover_route_test_session(r, out->session) : op == KDEBUG_OP_RECOVER_RESTORE ?
            recover_restore_session(r) : recover_now_session(r, out->session);
        recover_virtual_detach();
        if (!rc || r->state != before || op == KDEBUG_OP_RECOVER_RESTORE) {
            const struct hwdev_ops *ops = hwdev_module_ops();
            if (ops) ops->release_work();
        }
        if (rc) return -1;
    }
    if (op == KDEBUG_OP_RECOVER_STATUS) {
        const struct rec_tx_diag *d = recover_diag_get();
        kprintf(ATTR_WHITE, "txd sid=%u valid=%u seq=%u pos=%u fn=%s rc=%d where=%s sent=%u polls=%u bytes=%u\n",
            d->sid, d->valid, d->seq, d->pos, recover_diag_fn(d->fn), d->rc,
            recover_diag_where(d->where), d->sent, d->polls, d->bytes);
    }
    *out = (KdebugRecover){r->token.session, r->state, r->reason,
        (r->s0_verified ? 1U : 0U) | (r->s1_verified ? 2U : 0U),
        r->token.profile_crc, r->token.plan_hash, r->token.snapshot_crc,
        r->seq, r->lost_lines, r->discarded_bytes};
    return 0;
}

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
    .dbg_call = recovery_call,
    .hwdev_core_get = hwdev_core_get
};

/* No imports, allocation, device access or registration during init. */
int kdebug_init(struct kdebug_ops *candidate)
{
    *candidate = kdebug_ops;
    return 0;
}
