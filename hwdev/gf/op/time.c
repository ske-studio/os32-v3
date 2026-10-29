/* TIME semantics: MIT nouveau c03c1a1 bios/init.c:1777-1794. */
#include "../internal.h"

int gf_op_time_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    if (ctx->skip)
        return 0;
    return gf_init_wait(ctx, insn->value);
}
