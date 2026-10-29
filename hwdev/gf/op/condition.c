/* CONDITION: nouveau nvkm/subdev/bios/init.c:479,1802 (MIT). */
#include "../internal.h"

#define GF_CONDITION_BAR 0U
#define GF_CONDITION_WIDTH 4U

int gf_op_condition_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    u32 value;
    if (ctx->skip) return 0;
    if (ctx->io.read(ctx->io.opaque, GF_CONDITION_BAR, insn->target,
                     GF_CONDITION_WIDTH, &value)) return -1;
    if ((value & insn->mask) != insn->value) ctx->skip = true;
    return 0;
}
