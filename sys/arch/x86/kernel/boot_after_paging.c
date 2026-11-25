#include "boot_cpu.h"
#include "x87.h"

void arch_boot_after_paging(void)
{
    x87_probe();
}
