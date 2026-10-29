/* IO_FLAG_CONDITION: MIT nouveau bios/init.c:512-529, 779-788.
 * Semantics follow the existing gfinit decoder and execution plan. */
#include "../internal.h"

int gf_op_io_flag_condition_run(struct gf_init_context *ctx,
                                const struct gfinit_instruction *insn)
{
    if (ctx->skip)
        return insn->port == RECOVER_CRTC && insn->index == RECOVER_CR44 ? -1 : 0;
    u32 head = insn->port == RECOVER_CRTC && insn->index == RECOVER_CR44 ? 0 : ctx->head;
    u32 off = (insn->port == RECOVER_CRTC ? RECOVER_PRMCIO : RECOVER_PRMVIO) +
              head * RECOVER_HEAD_STRIDE + insn->port;
    u32 value;
    if (ctx->io.write(ctx->io.opaque, 0, off, 1, insn->index) ||
        ctx->io.read(ctx->io.opaque, 0, off + 1, 1, &value)) return -1;
    const u8 *entry = ctx->rom + insn->ref;
    u32 data = entry[GF_INIT_IO_FLAG_DATA] |
               ((u32)entry[GF_INIT_IO_FLAG_DATA + 1] << 8);
    u32 index = (value & insn->mask) >> insn->shift;
    if ((ctx->rom[data + index] & entry[GF_INIT_IO_FLAG_MASK]) !=
        entry[GF_INIT_IO_FLAG_VALUE]) ctx->skip = true;
    return 0;
}
