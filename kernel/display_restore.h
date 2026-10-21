#ifndef DISPLAY_RESTORE_H
#define DISPLAY_RESTORE_H
/* CUI only. Restores mode FFs, graphics STOP/page0, text START, palette/cursor.
 * Uses immutable boot flags, never starts V86 or changes 09A8h frequency. */
#include "types.h"
#include "pc98.h"
/* Last character word, outside both the 25- and 30-row console grids.
 * The console never uses it; recover alone leases it for the round trip. */
#define DISPLAY_RECOVER_CELL (TVRAM_ATTR_BASE - sizeof(u16))
void display_restore_98(void);
int display_recover_ready(void);
int display_recover_cell_read(u32 *value);
int display_recover_cell_write(u32 value);
#endif
