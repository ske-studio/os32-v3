#ifndef SQLITE_ENGINE_H
#define SQLITE_ENGINE_H
#include "types.h"
/* Core state: never infer engine activity from ESP or FD ownership. */
extern volatile u32 sqlite_engine_active;
extern int sqlite_engine_owner;
extern int sqlite_engine_group;
extern u32 sqlite_engine_generation;
extern u32 sqlite_engine_stack_top;
int sqlite_engine_available(int io);
int sqlite_engine_run(void (*call)(void *), void *frame, int io);
int sqlite_engine_stop_deferred(void);
void sqlite_engine_fault(void);
#endif
