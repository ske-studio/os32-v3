#include "font_guard.h"
#include "io.h"
#include "pgalloc.h"
#include "os32_kapi_shared.h"

/* 32-bit protected-mode FNSAVE: 28-byte environment + eight 80-bit values.
 * Core BSS is resident above 1MiB in every app PD, not the V86 low area.
 * No allocation, module slot, or borrowed caller stack is needed. */
static u8 font_x87_save[108] __attribute__((aligned(4)));
volatile u32 font_guard_active;
u32 font_x87_present;
extern void font_x87_enter(void *save);
extern void font_x87_leave(const void *save);

int font_guard_run(int (*call)(void *), void *context)
{
    u32 flags = irq_save();
    if (font_guard_active || kctx_irq_depth || kctx_exc_depth) {
        irq_restore(flags);
        return OS32_ERR_BUSY;
    }
    if (!font_x87_present) {
        irq_restore(flags);
        return OS32_ERR_NOSYS;
    }
    if (!call) {
        irq_restore(flags);
        return OS32_ERR_INVAL;
    }
    font_guard_active = 1;
    font_x87_enter(font_x87_save);
    irq_restore(flags);
    int result = call(context);
    (void)irq_save();
    font_x87_leave(font_x87_save);
    font_guard_active = 0;
    irq_restore(flags);
    return result;
}
