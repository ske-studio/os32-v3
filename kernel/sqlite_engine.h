#ifndef SQLITE_ENGINE_H
#define SQLITE_ENGINE_H
#include "types.h"
#define SQLITE_ENGINE_STACK_SIZE (128u * 1024u)
/* Core state: never infer engine activity from ESP or FD ownership. */
extern volatile u32 sqlite_engine_active;
extern u32 sqlite_engine_stack_top;
int sqlite_engine_available(int io);
int sqlite_engine_run(void (*call)(void *), void *frame, int io);
void sqlite_engine_fault(u32 vector, u32 eip, u32 cr2);
#endif
