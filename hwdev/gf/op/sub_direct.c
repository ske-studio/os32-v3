/* SUB_DIRECT: MIT nouveau c03c1a1, nvkm/subdev/bios/init.c:1340-1363. */
#include "../internal.h"

int gf_op_sub_direct_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    if (ctx->skip) return 0;
    if (ctx->calls == GF_INIT_CALL_DEPTH) return -1;
    ctx->call[ctx->calls++] = (struct gfinit_exec_frame){ctx->pc, ctx->repeats, 0};
    ctx->pc = insn->ref;
    return 0;
}
