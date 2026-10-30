#include "font_guard.h"
#include "io.h"
#include "../../mm/pgalloc.h"
#include "os32_kapi_shared.h"

/* TASK_FONTPERF F6: the x87 save/restore (FNSAVE/FRSTOR, font_x87.asm) is
 * gone with the integer raster. The name and the shell service slot stay:
 * the section is still the shell's read context and the fault attribution. */
volatile u32 font_guard_active;

int font_guard_run(int (*call)(void *), void *context)
{
    u32 flags = irq_save();
    if (font_guard_active || kctx_irq_depth || kctx_exc_depth) {
        irq_restore(flags);
        return OS32_ERR_BUSY;
    }
    if (!call) {
        irq_restore(flags);
        return OS32_ERR_INVAL;
    }
    font_guard_active = 1;
    irq_restore(flags);
    int result = call(context);
    (void)irq_save();
    font_guard_active = 0;
    irq_restore(flags);
    return result;
}
