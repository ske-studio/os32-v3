/* Emergency output is fixed at link time, independent of boot registration. */
#include "console_output.h"
#include "serial_diag.h"

void serial_puts_polled(const char *str) { emergency_output.puts_polled(str); }
int serial_gate_active(void) { return emergency_output.gate_active(); }
