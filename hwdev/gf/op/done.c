/* DONE control semantics: MIT nouveau bios/init.c:607-614.
 * No ROM x86 code or hardware access is performed here. */
#include "../internal.h"

int gf_op_done_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    (void)insn;
    u32 depth = ctx->calls ? ctx->call[ctx->calls - 1].ctx : 0;
    if (ctx->repeats != depth) return -1;
    if (ctx->calls)
        ctx->pc = ctx->call[--ctx->calls].pc;
    else
        ctx->pc = RECOVER_NONE;
    return 0;
}
