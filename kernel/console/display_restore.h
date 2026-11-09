#ifndef DISPLAY_RESTORE_H
#define DISPLAY_RESTORE_H
/* CUI restore only. Restores mode FFs, graphics STOP/page0, text START, palette/cursor.
 * Uses immutable boot flags, never starts V86 or changes 09A8h frequency. */
#include "types.h"
/* Display start only: preserves ROM/fallback PRAM and mode settings. */
void display_text_start(void);
/* Legacy 16-color write only; caller owns bounds, mask and shadow state.
 * Selected statically by platform. PowerMac rendering is unsupported. */
void display_palette4_write(int index, u8 r, u8 g, u8 b);
/* Internal: normalized logical row, logical height and display page.
 * Caller retains scroll state and the no-backbuffer guard. */
void display_scroll(int y, int height, int flip, int page);
void display_restore_98(void);
int display_recover_ready(void);
int display_recover_cell_read(u32 *value);
int display_recover_cell_write(u32 value);
#endif
