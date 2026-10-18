#ifndef SQLITE_ENGINE_H
#define SQLITE_ENGINE_H
#include "types.h"
#define SQLITE_ENGINE_STACK_SIZE (128u * 1024u)
/* Core state: never infer engine activity from ESP or FD ownership. */
extern volatile u32 sqlite_engine_active;
extern u32 sqlite_engine_stack_top;
int sqlite_engine_available(int io);
int sqlite_engine_run(void (*call)(void *), void *frame, int io);
u32 sqlite_engine_stack_high_water(void);
void sqlite_engine_stack_report(void);
void sqlite_engine_fault(u32 vector, u32 eip, u32 cr2);
/* Boot-only transaction. Internal SQLite exports are independent of this gate. */
extern int services_public;
enum services_stage {
    SERVICES_ROOT_READY, SERVICES_UNICODE, SERVICES_SQLITE_INTERNAL_READY,
    SERVICES_SETTINGS_CLOSED, SERVICES_FEP_INTERNAL_READY, SERVICES_READY,
    SERVICES_MINIMAL, SERVICES_MINIMAL_QUARANTINED
};
extern enum services_stage services_stage;
enum services_fault {
    SERVICES_FAULT_NONE, SERVICES_FAULT_POOL, SERVICES_FAULT_STACK,
    SERVICES_FAULT_SQLITE_INIT, SERVICES_FAULT_SETTINGS_CLOSE,
    SERVICES_FAULT_FEP_TEXT, SERVICES_FAULT_FEP_ATTR, SERVICES_FAULT_FEP_CRC,
    SERVICES_FAULT_DICTIONARY, SERVICES_FAULT_RESERVE,
    SERVICES_FAULT_FEP_CLOSE, SERVICES_FAULT_SHUTDOWN
};
/* Debugger injection before services_boot; zero in normal images. */
extern volatile u32 services_boot_fault;
void services_boot(void);
#endif
