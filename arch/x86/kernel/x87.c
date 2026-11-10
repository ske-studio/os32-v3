#include "x87.h"

/* FNSAVE/FRSTOR の域は 108 バイト。AppSlot の中の位置を動かさない (整列 1)。 */
_Static_assert(sizeof(struct arch_fp_context) == 108 &&
               __alignof__(struct arch_fp_context) == 1, "x87 FNSAVE area");

static int x87_present;

void x87_probe(void)
{
    unsigned short status = 0xffff, control = 0xffff;
    __asm__ volatile("fninit\n\tfnstsw %0\n\tfnstcw %1"
                     : "+m"(status), "+m"(control));
    x87_present = status == 0 && (control & 0x103f) == 0x003f;
}

void arch_fp_save(struct arch_fp_context *state)
{
    if (x87_present) __asm__ volatile("fnsave %0; fwait" : "=m"(*state) : : "memory");
}

void arch_fp_restore(const struct arch_fp_context *state)
{
    if (x87_present) __asm__ volatile("frstor %0" : : "m"(*state) : "memory");
}

void arch_fp_init(void)
{
    if (x87_present) __asm__ volatile("fninit" : : : "memory");
}
