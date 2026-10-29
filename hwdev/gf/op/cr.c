/* NV44A init CR; semantics checked against MIT nouveau c03c1a1
 * nvkm/subdev/bios/init.c:1172-1188 and OS32's existing expansion. */
#include "../internal.h"

int gf_op_cr_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    u32 old, head = insn->index == RECOVER_CR44 ? 0 : ctx->head;
    u32 select = RECOVER_PRMCIO + head * RECOVER_HEAD_STRIDE + RECOVER_CRTC;
    if (ctx->skip) return insn->index == RECOVER_CR44 ? -1 : 0;
    if (ctx->io.write(ctx->io.opaque, 0, select, 1, insn->index) ||
        ctx->io.read(ctx->io.opaque, 0, select + 1, 1, &old) ||
        ctx->io.write(ctx->io.opaque, 0, select + 1, 1,
                      (old & insn->mask) | insn->value)) return -1;
    if (insn->index == RECOVER_CR44)
        ctx->head = insn->value == RECOVER_CR44_HEAD1;
    return 0;
}
