#include "platform_boot.h"
#include "idt.h"
#include "sysclk.h"
#include "pc98.h"
#include "irq.h"

void platform_boot_irq_controller(void) { pic_init(); irq_topology_check(); }
void platform_boot_clock_detect(void) { sysclk_detect(); }
void platform_boot_irq_start(void)
{
    irq_enable(0);
    irq_enable(ICU_SLAVEID);
}
