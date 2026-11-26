#ifndef KERNEL_MODULE_H
#define KERNEL_MODULE_H

#include "kdebug_resident_contract.h"

/* Boot only, after dma_pool_init; cooperative, normal kernel context. */
void kdebug_load(void);
/* KAPI kdebug_call: ops.dbg_call or OS32_ERR_NOSYS (sys/kern/module/kdebug_call.c). */
i32 kdebug_call(u32 op, void *buf, u32 size);
int kdebug_init(struct kdebug_ops *candidate);

#endif
