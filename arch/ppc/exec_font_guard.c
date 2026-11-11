#include "exec_font_guard.h"
u32 exec_font_guard_active(void) { return 0; }
void exec_font_guard_fault(u32 vector, u32 pc, u32 address) { (void)vector; (void)pc; (void)address; }
void exec_font_guard_fixture(void) { }
int exec_font_guard_run(int (*call)(void *), void *ctx) { return call(ctx); }
