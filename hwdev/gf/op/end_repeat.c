/* END_REPEAT control flow; semantics checked against MIT nouveau c03c1a1,
 * nvkm/subdev/bios/init.c:658-677,720-733. */
#include "../internal.h"

int gf_op_end_repeat_run(struct gf_init_context *ctx,
                         const struct gfinit_instruction *insn)
{
    if (!ctx->repeats) return -1;
    struct gfinit_exec_frame *frame = &ctx->repeat[ctx->repeats - 1];
    if (frame->ctx != insn->pc) return -1;
    if (--frame->count) ctx->pc = frame->pc;
    else ctx->repeats--;
    return 0;
}
