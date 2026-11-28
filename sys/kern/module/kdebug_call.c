/* KAPI kdebug_call の受け口 (TASK_KPEEK §2-3)。本体は起動時に読む部品
 * (kdebug.bin の ops slot 19)。部品が無ければ OS32_ERR_NOSYS。 */
#include "module.h"
#ifdef INPUT_MEASURE_DIAG
#include "input_measure.h"
#endif
#include "gfx_measure_wire.h"
#include "platform_hwb_session.h"
#ifdef PM_KDEBUG
struct kdebug kdebug;
#endif
#ifdef PCM_E4_DIAG
#include "pcm_e4_diag.h"
#include "pcm_e4_wire.h"
#endif

/* 並びは kdebug 部品・shell と共有 (os32_kapi_shared.h)。 */
STATIC_ASSERT(sizeof(KdebugPeek) == 40 + KDEBUG_PEEK_CHUNK, kdebug_peek_layout);

i32 kdebug_call(u32 op, void *buf, u32 size)
{
#ifdef INPUT_MEASURE_DIAG
    if (op == INPUT_MEASURE_USB || op == INPUT_MEASURE_FG1) return OS32_ERR_INVAL;
#endif
    if (op == GFX_TRACE_EVENT || op == GFX_TRACE_POLICY) return OS32_ERR_INVAL; /* kernel observation only */
#ifdef PCM_E4_DIAG
    if (op == PCM_E4_OP_RUN) {
        if (buf || size) return OS32_ERR_INVAL;
        return pcm_e4_run();
    }
#endif
#if PLATFORM_HWB_SESSION
    if (op == PLATFORM_HWB_ENTER || op == PLATFORM_HWB_LEAVE)
        return buf || size ? OS32_ERR_INVAL : exec_hwb_session(op);
#endif
    return kdebug.ops.dbg_call ? kdebug.ops.dbg_call(op, buf, size)
                               : OS32_ERR_NOSYS;
}
