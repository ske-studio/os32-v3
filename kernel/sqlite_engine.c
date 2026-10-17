#include "sqlite_engine.h"
#include "io.h"
#include "irq.h"
#include "pgalloc.h"
#include "memmap.h"

volatile u32 sqlite_engine_active;
u32 sqlite_engine_stack_top;
extern void sqlite_call_on_alt_stack(void (*call)(void *), u32 top, void *frame);

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
    sqlite_engine_active = 1;
    if (!sqlite_engine_stack_top) sqlite_engine_stack_top = MEM_SQLITE_STACK_TOP;
    irq_restore(flags);
    sqlite_call_on_alt_stack(call, sqlite_engine_stack_top, frame);
    (void)irq_save();
    sqlite_engine_active = 0;
    irq_restore(flags);
    /* STOP belongs to the caller's safe point, after output and teardown. */
    return 1;
}
