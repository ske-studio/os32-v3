#ifndef OS32_V86_GCAP_DIAG_H
#define OS32_V86_GCAP_DIAG_H
#include "v86_gcap_math.h"
#include "../lib/serial_diag.h"

static void __attribute__((cold, unused)) v86g_phase_diag(const V86Gcap *g,
        V86gPhaseCounts before, u32 phase, int reason, int done, u16 ax)
{
    if (serial_gate_active()) return;
    V86gPhaseCounts d = v86g_phase_delta(g, before);
    serial_puts_polled("[gcap] ph="); serial_diag_u32(phase);
    serial_puts_polled(" reason="); serial_diag_i32(reason);
    serial_puts_polled(" done="); serial_diag_i32(done);
    serial_puts_polled(" ax="); serial_diag_hex(ax, 4);
    serial_puts_polled(" in=+"); serial_diag_u32(d.in);
    serial_puts_polled(" inother=+"); serial_diag_u32(d.inother);
    serial_puts_polled(" outrec=+"); serial_diag_u32(d.outrec);
    serial_puts_polled(" outexec=+"); serial_diag_u32(d.outexec);
    serial_puts_polled("\r\n");
}
#endif
