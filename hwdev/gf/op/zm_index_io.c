/* NV44A init: MIT nouveau c03c1a1 nvkm/subdev/bios/init.c:1441-1451. */
#include "../internal.h"

int gf_op_zm_index_io_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    bool cr44 = insn->port == RECOVER_CRTC && insn->index == RECOVER_CR44;
    if (ctx->skip) return cr44 ? -1 : 0;
    u32 head = cr44 ? 0 : ctx->head;
    u32 off = (insn->port == RECOVER_CRTC ? RECOVER_PRMCIO : RECOVER_PRMVIO) +
              head * RECOVER_HEAD_STRIDE + insn->port;
    if (ctx->io.write(ctx->io.opaque, 0, off, 1, insn->index) ||
        ctx->io.write(ctx->io.opaque, 0, off + 1, 1, insn->value)) return -1;
    if (cr44) ctx->head = insn->value == RECOVER_CR44_HEAD1 ? 1 : 0;
    return 0;
}
