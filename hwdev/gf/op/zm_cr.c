/* ZM_CR: nouveau nvkm/subdev/bios/init.c:1196-1208 (MIT). */
#include "../internal.h"

int gf_op_zm_cr_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    u32 head = insn->index == RECOVER_CR44 ? 0 : ctx->head;
    u32 off = RECOVER_PRMCIO + head * RECOVER_HEAD_STRIDE + RECOVER_CRTC;

    if (!ctx->skip &&
        (ctx->io.write(ctx->io.opaque, 0, off, 1, insn->index) ||
         ctx->io.write(ctx->io.opaque, 0, off + 1, 1, insn->value)))
        return -1;
    if (insn->index == RECOVER_CR44)
        ctx->head = insn->value == RECOVER_CR44_HEAD1;
    return 0;
}
