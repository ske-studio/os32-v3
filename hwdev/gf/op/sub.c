/* NV44A SUB semantics checked against MIT nouveau bios/init.c:1663-1682. */
#include "../internal.h"

int gf_op_sub_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    if (ctx->skip) return 0;
    if (ctx->calls == GF_INIT_CALL_DEPTH) return -1;
    ctx->call[ctx->calls++] = (struct gfinit_exec_frame){ctx->pc, ctx->repeats, 0};
    ctx->pc = insn->ref;
    return 0;
}
