#ifndef OS32_KSTACK_HW_H
#define OS32_KSTACK_HW_H
#include "types.h"
void kstack_hw_init(void);
/* Packed byte counts: fixed kstack in low 16 bits, shell stack in high 16. */
u32 kstack_high_water(void);
#endif
