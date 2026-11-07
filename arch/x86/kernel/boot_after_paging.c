#include "boot_cpu.h"
#include "cpu.h"
#include "x87.h"

void arch_boot_after_paging(void)
{
    u32 cr0_val;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0_val));
    cr0_val &= ~((u32)(CR0_EM | CR0_TS));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0_val) : "memory");
    x87_probe();
}
