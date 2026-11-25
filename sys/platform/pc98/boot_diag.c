#include "boot_diag.h"
#include "pit.h"
#include "idt.h"
void platform_boot_diag(struct boot_diag *out)
{
    const struct pit_setup *pit = pit_get_setup();
    out->irq_master_vector = PIC_MASTER_OFFSET;
    out->irq_slave_vector = PIC_SLAVE_OFFSET;
    out->timer_kind = BOOT_TIMER_8254;
    out->timer_valid = pit->valid;
    out->timer_hz = pit->hz;
    out->timer_input = pit->clk_hz;
}
