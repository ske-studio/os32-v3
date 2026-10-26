/* Real emergency facade with the PC98 provider, independent of boot wiring. */
#include "console_output.h"
const EmergencyOps emergency_output = {
    .puts_polled = pc98_serial_puts_polled, .gate_active = pc98_serial_gate_active,
};
#include "../../kernel/serial_diag.c"
