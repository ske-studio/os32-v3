/* Saved platform diagnostics; no probing or output. */
#ifndef BOOT_DIAG_H
#define BOOT_DIAG_H
#include "types.h"
#define BOOT_TIMER_8254 1u
struct boot_diag {
    u32 irq_master_vector, irq_slave_vector;
    u32 timer_kind, timer_valid, timer_hz, timer_input;
};
void platform_boot_diag(struct boot_diag *out);
#endif
