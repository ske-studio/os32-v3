#ifndef PC98_ROUTE_H
#define PC98_ROUTE_H
#include "types.h"
#define PC98_ROUTE_PORT 0x0facU
#define PC98_ROUTE_MASK 3U
#define PC98_ROUTE_98 0U
#define PC98_ROUTE_GF 1U
int pc98_route_to_gf(void);
int pc98_route_select_98(void);
void pc98_console_set_safe(int safe);
int pc98_console_safe(void) __attribute__((weak));
int pc98_route_to_98(void);
int pc98_route_cell_check(void);
/* Includes route readback and the platform owner's legacy-decode check. */
int pc98_route_safe(void) __attribute__((weak));
#endif
