#include "sqlite_engine.h"
#include "io.h"
#include "irq.h"
#include "pgalloc.h"
#include "memmap.h"
#include "fd_redirect.h"

volatile u32 sqlite_engine_active;
int sqlite_engine_owner;
int sqlite_engine_group = -1;
u32 sqlite_engine_generation;
u32 sqlite_engine_stack_top;
extern void sqlite_call_on_alt_stack(void (*call)(void *), u32 top, void *frame);

int sqlite_engine_stop_deferred(void)
{
    return sqlite_engine_active != 0;
}

void sqlite_engine_fault(void)
{
    /* No unwind: SQLite state and its stack may already be damaged. */
    _disable();
    for (;;) _stop();
}

int sqlite_engine_available(int io)
{
    u32 flags = irq_save();
    int ok = !sqlite_engine_active && !kctx_irq_depth && !kctx_exc_depth &&
             (!io || (flags & 0x200u));
    irq_restore(flags);
    return ok;
}

int sqlite_engine_run(void (*call)(void *), void *frame, int io)
{
    u32 flags = irq_save();
    if (sqlite_engine_active || kctx_irq_depth || kctx_exc_depth ||
        (io && !(flags & 0x200u))) {
        irq_restore(flags);
        return 0;
    }
    sqlite_engine_owner = res_owner_get();
    sqlite_engine_active = 1;
    if (!sqlite_engine_stack_top) sqlite_engine_stack_top = MEM_SQLITE_STACK_TOP;
    irq_restore(flags);
    sqlite_call_on_alt_stack(call, sqlite_engine_stack_top, frame);
    (void)irq_save();
    sqlite_engine_active = 0;
    sqlite_engine_owner = 0;
    sqlite_engine_group = -1;
    sqlite_engine_generation = 0;
    irq_restore(flags);
    /* STOP belongs to the caller's safe point, after output and teardown. */
    return 1;
}
