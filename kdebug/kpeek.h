#ifndef KDEBUG_KPEEK_H
#define KDEBUG_KPEEK_H
#include "types.h"
/* kdebug ops slot 19 (dbg_call)。KAPI kdebug_call の本体 (kdebug/kpeek.c)。 */
i32 kpeek_call(u32 op, void *buf, u32 size);
#endif
