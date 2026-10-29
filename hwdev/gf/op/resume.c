/* RESUME control semantics: MIT nouveau nvkm/subdev/bios/init.c:1751. */
#include "../internal.h"

int gf_op_resume_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    (void)insn;
    ctx->skip = false;
    return 0;
}
