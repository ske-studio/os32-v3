#ifndef PLATFORM_EMERGENCY_H
#define PLATFORM_EMERGENCY_H

#include "types.h"

/* Link-time fixed, resident output: no registration, allocation or waits.
 * Boot attributes are byte stores; fault attributes replace the full cell. */
void platform_boot_puts(int x, int y, const char *str, u8 color);
void platform_fault_cell(u32 pos, u16 code, u8 attr);

#endif
