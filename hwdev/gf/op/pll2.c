/* NV44A PLL2: MIT nouveau nvkm/subdev/bios/init.c:985-995;
 * sequence model: nvkm/subdev/devinit/nv04.c:173-331 (see gfinit_pll.h). */
#include "../internal.h"

int gf_op_pll2_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    return ctx->skip ? 0 : gf_init_pll(ctx, insn->target, insn->value);
}
