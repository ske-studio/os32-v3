/* Foreground timeout diagnostics. Sample before teardown destroys state.
 * Diagnostic read failure never replaces the original GF2_ETIME. */
#ifndef HWDEV_GF_WAIT_DIAG_H
#define HWDEV_GF_WAIT_DIAG_H
#include "../../lib/serial_diag.h"

static bool gf_wait_timeout(struct gf_io *io, const char *site, u32 reg,
                            const char *want, const char *sample,
                            u32 deadline, u32 now)
{
    u32 last = 0;
    if (serial_gate_active()) return false;
    int read_rc = io->read(io->opaque, 0, reg, sizeof(last), &last);
    serial_puts_polled("[gf3] wait timeout site="); serial_puts_polled(site);
    serial_puts_polled(" reg="); serial_diag_hex(reg, 8);
    serial_puts_polled(" last="); serial_diag_hex(last, 8);
    serial_puts_polled(" want="); serial_puts_polled(want);
    serial_puts_polled(" dl="); serial_diag_u32(deadline);
    serial_puts_polled(" now="); serial_diag_u32(now);
    serial_puts_polled(sample); serial_diag_hex(last, 8);
    serial_puts_polled(" read_rc="); serial_diag_i32(read_rc);
    return true;
}
#endif
