#ifndef HOST_FONT_GUARD_INACTIVE_H
#define HOST_FONT_GUARD_INACTIVE_H
/* These legacy fixtures have no font callback. Active behavior is executed by
 * test_font_guard.py against the product guard and the same exec/ISR slices. */
static volatile unsigned long font_guard_active;
static inline void font_guard_fault(unsigned long vector, unsigned long pc,
                                    unsigned long address)
{
    (void)vector; (void)pc; (void)address;
    __builtin_trap();
}
#endif
