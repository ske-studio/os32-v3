/* NV44A INDEX_IO; semantics checked against MIT nouveau
 * nvkm/subdev/bios/init.c:1853-1867 and OS32's indexed expansion. */
#include "../internal.h"

int gf_op_index_io_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    bool cr44 = insn->port == RECOVER_CRTC && insn->index == RECOVER_CR44;
    if (ctx->skip) return cr44 ? -1 : 0;
    u32 head = cr44 ? 0 : ctx->head;
    u32 select = (insn->port == RECOVER_CRTC ? RECOVER_PRMCIO : RECOVER_PRMVIO) +
                 head * RECOVER_HEAD_STRIDE + insn->port;
    u32 value;
    if (ctx->io.write(ctx->io.opaque, KPEEK_NV_BAR_MMIO, select, 1, insn->index) ||
        ctx->io.read(ctx->io.opaque, KPEEK_NV_BAR_MMIO, select + 1, 1, &value) ||
        ctx->io.write(ctx->io.opaque, KPEEK_NV_BAR_MMIO, select + 1, 1,
                      (value & insn->mask) | insn->value)) return -1;
    if (cr44) ctx->head = insn->value == RECOVER_CR44_HEAD1 ? 1 : 0;
    return 0;
}
