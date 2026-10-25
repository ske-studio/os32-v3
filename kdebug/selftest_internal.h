#ifndef KDEBUG_SELFTEST_INTERNAL_H
#define KDEBUG_SELFTEST_INTERNAL_H

#include "types.h"

/* Local module functions, outside the kernel import ABI. */
u32 kst_con_sink(void);
u32 kst_kbd_inject(void);
int kst_lease(void);

#endif
