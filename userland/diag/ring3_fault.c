/* PM observation / per-case expectations: ring3_marker.h acceptance table. */
#include "ring3_marker.h"
#include "memmap.h"
OS32_KAPI_LAYOUT_STAMP();
void _start(void) __attribute__((section(".text.startup"), used, noreturn));
void _start(void)
{
    volatile unsigned long *mark = r3_marker(0x4652UL); /* RF */
    volatile unsigned int *kern = (volatile unsigned int *)KERNEL_LOAD_ADDR;
    r3_arm(mark, 0x3F544C46UL, (unsigned long)kern); /* FLT? */
    *kern = 0xDEADBEEFUL;
    mark[1] = R3_SURV; /* Unexpected survival. */
    r3_exit(1);
}
