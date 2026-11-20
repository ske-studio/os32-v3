#ifndef OS32_PLATFORM_FEP_INPUT_H
#define OS32_PLATFORM_FEP_INPUT_H

#include "types.h"

/* Source and SHIFT_* modifiers of the last dequeued key. Remote keys have no modifiers. */
int platform_fep_key_local(void);
u8 platform_fep_key_shift(void);

#endif
