#include "fault_fixture.h"

void arch_fixture_ud(void) { __asm__ volatile("ud2"); }
void arch_fixture_gp(void)
{
    __asm__ volatile("mov $0xffff, %%ax; mov %%ax, %%ds" ::: "eax");
}
