/* PM observation / per-case expectations: ring3_marker.h acceptance table. */
#include "ring3_marker.h"
OS32_KAPI_LAYOUT_STAMP();
void _start(void) __attribute__((section(".text.startup"), used, noreturn));
void _start(void)
{
    volatile unsigned long *mark = r3_marker(0x3352UL); /* R3 */
    unsigned long i;
    mark[0] = 0x33474E52UL; /* RNG3 */
    for (i = 0; i < 20000000UL; i++) {
        __asm__ __volatile__("" ::: "memory");
    }
    mark[1] = R3_DONE;
    r3_exit(0);
}
