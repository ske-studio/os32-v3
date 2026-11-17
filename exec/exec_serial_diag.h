#ifndef OS32_EXEC_SERIAL_DIAG_H
#define OS32_EXEC_SERIAL_DIAG_H
#include "exec_hooks.h"
#if OS32_HOOK_SERIAL_DIAG
#include "serial_diag.h"
static inline void exec_serial_puts_polled(const char *s) { serial_puts_polled(s); }
#else
/* The shell has no provider for this interface. */
void exec_serial_puts_polled(const char *s);
#endif
#endif
