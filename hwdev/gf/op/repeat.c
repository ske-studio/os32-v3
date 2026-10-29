/* REPEAT: MIT nouveau nvkm/subdev/bios/init.c:658-676. */
#include "../internal.h"

int gf_op_repeat_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    if (ctx->repeats == GF_INIT_REPEAT_DEPTH) return -1;
    ctx->repeat[ctx->repeats++] = (struct gfinit_exec_frame){
        .pc = ctx->pc, .ctx = insn->end_repeat, .count = insn->count
    };
    return 0;
}
