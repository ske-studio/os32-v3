#ifndef PC98_DISPLAY_PRIVATE_H
#define PC98_DISPLAY_PRIVATE_H
#include "types.h"
#include "pc98.h"
/* Last character word, outside both the 25- and 30-row console grids.
 * The console never uses it; recover alone leases it for the round trip. */
#define DISPLAY_RECOVER_CELL (TVRAM_ATTR_BASE - sizeof(u16))
#endif
