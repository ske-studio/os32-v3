/* Unsupported display provider: compile-only, no FEP boot. */
#include "kernel/ime/ime_display_private.h"
int ime_display_cols(void) { return 80; }
int ime_display_preedit_row(void) { return 24; }
int ime_display_save(int top, int rows) { (void)top; (void)rows; return 0; }
int ime_display_restore(int top, int rows) { (void)top; (void)rows; return 0; }
int ime_display_boot(u32 fault) { (void)fault; return -1; }
u32 ime_display_bytes(void) { return 0; }
void ime_display_abort(void) { }
