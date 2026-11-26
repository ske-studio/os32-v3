#ifndef OS32_PLATFORM_TRAP_SINK_H
#define OS32_PLATFORM_TRAP_SINK_H
#include "types.h"
struct ppc_frame;
/* Explicit H2/platform provider; never called in real mode or before ready.
 * Dump the complete frame and reason, using only the prepared I/G alias. */
void ppc_trap_fatal_sink(const struct ppc_frame *frame, u32 reason);
#endif
