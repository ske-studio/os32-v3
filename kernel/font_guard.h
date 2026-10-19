#ifndef FONT_GUARD_H
#define FONT_GUARD_H

#include "types.h"

/* Internal outermost font boundary; no public KAPI until T7b-b.
 * NOSYS means use KCG (no x87), BUSY rejects reentry/IRQ/exception context.
 * The callback returns an integer result, including errors; every return
 * restores the caller. No FP arguments/results cross this boundary.
 * STOP is consumed by the caller's existing safe point after this returns.
 * The same boundary is intended for shell and private font callers. */
int font_guard_run(int (*call)(void *), void *context);
extern volatile u32 font_guard_active;
extern u32 font_x87_present;   /* Set once by kentry, after BSS clear. */
void font_guard_fault(u32 vector, u32 eip, u32 address) __attribute__((noreturn));

#endif
