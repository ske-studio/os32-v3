#include "x87.h"

static int x87_present;

void x87_probe(void)
{
    unsigned short status = 0xffff, control = 0xffff;
    __asm__ volatile("fninit\n\tfnstsw %0\n\tfnstcw %1"
                     : "+m"(status), "+m"(control));
    x87_present = status == 0 && (control & 0x103f) == 0x003f;
}

void x87_save(X87State *state)
{
    if (x87_present) __asm__ volatile("fnsave %0; fwait" : "=m"(*state) : : "memory");
}

void x87_restore(const X87State *state)
{
    if (x87_present) __asm__ volatile("frstor %0" : : "m"(*state) : "memory");
}

void x87_init(void)
{
    if (x87_present) __asm__ volatile("fninit" : : : "memory");
}
