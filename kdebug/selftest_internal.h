#ifndef KDEBUG_SELFTEST_INTERNAL_H
#define KDEBUG_SELFTEST_INTERNAL_H

#include "types.h"

/* kdebug の中だけの入口 (kselftest.c から呼ぶ)。カーネルの import 表には載せない。
 * 戻り値は移す前の自己試験と同じビットの意味。 */
u32 kst_kbd_inject(void);
int kst_lease(void);

u32 kst_exec_tramp_user(void);
u32 kst_exec_kapi_layout(void);

#endif /* KDEBUG_SELFTEST_INTERNAL_H */
