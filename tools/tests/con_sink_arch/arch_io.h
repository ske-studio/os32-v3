/* Observe the existing host IRQ model through io.h's normal arch boundary. */
#define irq_save host_model_irq_save
#define irq_restore host_model_irq_restore
#include "../host_arch/arch_io.h"
#undef irq_save
#undef irq_restore

static unsigned int sink_irq_saves, sink_irq_restores;
static unsigned int sink_irq_bad_restore;
static inline unsigned int irq_save(void)
{
    sink_irq_saves++;
    return host_model_irq_save();
}
static inline void irq_restore(unsigned int flags)
{
    if (_irq_enabled()) sink_irq_bad_restore++;
    sink_irq_restores++;
    host_model_irq_restore(flags);
}
