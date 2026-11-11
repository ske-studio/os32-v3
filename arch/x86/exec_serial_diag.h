#ifndef OS32_EXEC_SERIAL_DIAG_H
#define OS32_EXEC_SERIAL_DIAG_H
#include "serial_diag.h"
static inline void exec_serial_puts_polled(const char *s) { serial_puts_polled(s); }
#endif
