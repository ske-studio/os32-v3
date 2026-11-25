/* Internal display resources; outside the FEP import/export contracts. */
#ifndef OS32_IME_DISPLAY_PRIVATE_H
#define OS32_IME_DISPLAY_PRIVATE_H
#include "types.h"
#define IME_CANDLIST_SAVE_ROWS 3
int ime_display_cols(void);
int ime_display_preedit_row(void);
int ime_display_save(int top, int rows);
int ime_display_restore(int top, int rows);
int ime_display_boot(u32 fault);
u32 ime_display_bytes(void);
void ime_display_abort(void);
#endif
