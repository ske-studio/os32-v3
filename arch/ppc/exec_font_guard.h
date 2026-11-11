#ifndef OS32_EXEC_FONT_GUARD_H
#define OS32_EXEC_FONT_GUARD_H
#include "types.h"
u32 exec_font_guard_active(void);
void exec_font_guard_fault(u32 vector, u32 pc, u32 address);
void exec_font_guard_fixture(void);
int exec_font_guard_run(int (*call)(void *), void *ctx);
#endif
