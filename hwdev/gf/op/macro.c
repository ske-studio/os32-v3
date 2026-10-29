/* INIT_MACRO: MIT nouveau nvkm/subdev/bios/init.c:1723-1744.
 * Decoded target and value follow kdebug/gfinit_op/condition_decode.inc. */
#include "../internal.h"

#define GF_MACRO_BAR 0U
#define GF_MACRO_WIDTH 4U

int gf_op_macro_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    if (ctx->skip)
        return 0;
    return ctx->io.write(ctx->io.opaque, GF_MACRO_BAR, insn->target,
                         GF_MACRO_WIDTH, insn->value);
}
