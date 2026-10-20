/* KAPI kdebug_call の受け口 (TASK_KPEEK §2-3)。本体は起動時に読む部品
 * (kdebug.bin の ops slot 19)。部品が無ければ OS32_ERR_NOSYS。 */
#include "module.h"

/* 並びは kdebug 部品・shell と共有 (os32_kapi_shared.h)。 */
STATIC_ASSERT(sizeof(KdebugPeek) == 40 + KDEBUG_PEEK_CHUNK, kdebug_peek_layout);

i32 kdebug_call(u32 op, void *buf, u32 size)
{
    return kdebug.ops.dbg_call ? kdebug.ops.dbg_call(op, buf, size)
                               : OS32_ERR_NOSYS;
}
