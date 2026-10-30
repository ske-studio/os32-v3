#ifndef FONT_GUARD_H
#define FONT_GUARD_H

#include "types.h"

/* Internal outermost font boundary ("guard 区間"); no public KAPI.
 * The raster is integer only (TASK_FONTPERF F5/F6), so the guard no longer
 * saves or restores x87 state and never answers NOSYS. It still marks the
 * font section: BUSY rejects reentry/IRQ/exception context, park/exec paths
 * check font_guard_active, and a fault inside is attributed to the font.
 * The callback returns an integer result, including errors.
 * STOP is consumed by the caller's existing safe point after this returns.
 * The same boundary serves the shell (service table v2 font_guard_run). */
int font_guard_run(int (*call)(void *), void *context);
extern volatile u32 font_guard_active;
void font_guard_fault(u32 vector, u32 eip, u32 address) __attribute__((noreturn));

#endif
