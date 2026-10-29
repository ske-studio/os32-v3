/* NV44A init ZM_REG. Semantics: MIT nouveau c03c1a1
 * nvkm/subdev/bios/init.c:1892-1904; decoded PMC_ENABLE adjustment retained. */
#include "../internal.h"

#define GF_ZM_REG_BAR 0U
#define GF_ZM_REG_WIDTH 4U

int gf_op_zm_reg_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    if (ctx->skip) return 0;
    return ctx->io.write(ctx->io.opaque, GF_ZM_REG_BAR, insn->target, GF_ZM_REG_WIDTH, insn->value);
}
