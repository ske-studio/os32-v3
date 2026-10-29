/* MIT nouveau nvkm/subdev/bios/init.c:740-760 (INIT_COPY). */
#include "../internal.h"

int gf_op_copy_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    u32 data, source, shifted;
    u32 head = insn->port == RECOVER_CRTC && insn->index == RECOVER_CR44 ? 0 : ctx->head;
    u32 port = (insn->port == RECOVER_CRTC ? RECOVER_PRMCIO : RECOVER_PRMVIO) +
               head * RECOVER_HEAD_STRIDE + insn->port;
    if (ctx->skip)
        return insn->port == RECOVER_CRTC && insn->index == RECOVER_CR44 ? -1 : 0;
    if (ctx->io.write(ctx->io.opaque, 0, port, 1, insn->index) ||
        ctx->io.read(ctx->io.opaque, 0, port + 1, 1, &data) ||
        ctx->io.read(ctx->io.opaque, 0, insn->ref, 4, &source)) return -1;
    shifted = insn->shift < GF_SHIFT_LEFT ? source >> insn->shift :
              source << (GF_SHIFT_WRAP - insn->shift);
    data = ((data & insn->mask) | (shifted & insn->value)) & U8_MAX;
    return ctx->io.write(ctx->io.opaque, 0, port + 1, 1, data) ? -1 : 0;
}
