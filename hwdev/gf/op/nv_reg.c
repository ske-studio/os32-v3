/* NV44A INIT_NV_REG: MIT nouveau c03c1a1 bios/init.c:1709-1720.
 * The existing decoder/plan supplies the register, keep mask and OR value. */
#include "../internal.h"

#define GF_NV_REG_WIDTH 4U

int gf_op_nv_reg_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    u32 old;
    if (ctx->skip) return 0;
    if (ctx->io.read(ctx->io.opaque, KPEEK_NV_BAR_MMIO, insn->target,
                     GF_NV_REG_WIDTH, &old)) return -1;
    if (ctx->io.write(ctx->io.opaque, KPEEK_NV_BAR_MMIO, insn->target,
                      GF_NV_REG_WIDTH, (old & insn->mask) | insn->value)) return -1;
    return 0;
}
