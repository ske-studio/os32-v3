#ifndef OS32_EXEC_FONT_GUARD_H
#define OS32_EXEC_FONT_GUARD_H
#include "font_guard.h"
#include "r1_fixture.h"
static inline u32 exec_font_guard_active(void) { return font_guard_active; }
static inline __attribute__((noreturn)) void exec_font_guard_fault(u32 vector, u32 pc, u32 address) { font_guard_fault(vector, pc, address); }
#ifdef OS32_R1_FIXTURE
static inline void exec_font_guard_fixture(void) { font_guard_fixture(); }
#endif
static inline int exec_font_guard_run(int (*call)(void *), void *ctx) { return font_guard_run(call, ctx); }
#endif
