/* NV44A INIT_NOT: MIT nouveau c03c1a1 bios/init.c:763-773. */
#include "../internal.h"

int gf_op_not_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    (void)insn;
    ctx->skip = !ctx->skip;
    return 0;
}
