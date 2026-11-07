#include "boot_cpu.h"
#include "gdt.h"
#include "tss.h"
#include "double_fault.h"
#include "idt.h"
#include "memmap.h"

void arch_boot_segments(void) { gdt_init(); }
void arch_boot_exception_stack(void)
{
    tss_init(MEM_KSTACK_TOP);
    double_fault_init();
}
void arch_boot_vectors(void) { idt_init(); }
