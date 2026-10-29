/* MIT nouveau c03c1a1 bios/init.c:950-978; shared OS32 PLL sequence. */
#include "../internal.h"

int gf_op_io_restrict_pll2_run(struct gf_init_context *ctx,
                               const struct gfinit_instruction *insn)
{
    if (ctx->skip)
        return insn->port == RECOVER_CRTC && insn->index == RECOVER_CR44 ? -1 : 0;
    u32 select = gf_init_index_alias(ctx, insn->port, insn->index), value;
    if (ctx->io.write(ctx->io.opaque, 0, select, 1, insn->index) ||
        ctx->io.read(ctx->io.opaque, 0, select + 1, 1, &value)) return -1;
    u32 at = insn->ref + sizeof(u32) * ((value & insn->mask) >> insn->shift);
    u32 khz = 0;
    for (u32 i = 0; i < sizeof(u32); i++) khz |= (u32)ctx->rom[at + i] << (8 * i);
    return gf_init_pll(ctx, insn->target, khz);
}
