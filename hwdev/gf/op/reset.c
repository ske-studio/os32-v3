/* INIT_RESET behavior: MIT nouveau c03c1a1, bios/init.c:1477-1500.
 * OS32 keeps the existing literal zero write to PBUS1850. */
#include "../internal.h"

int gf_op_reset_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    u32 saved;
    if (ctx->io.read(ctx->io.opaque, 0, RECOVER_PBUS184C, 4, &saved) ||
        gf_init_save(ctx, 0, RECOVER_PBUS184C, 4, saved) ||
        ctx->io.write(ctx->io.opaque, 0, RECOVER_PBUS184C, 4, saved & GF_INIT_RESET_KEEP) ||
        ctx->io.write(ctx->io.opaque, 0, insn->target, 4, insn->mask) ||
        gf_init_wait(ctx, GF_INIT_RESET_US) ||
        ctx->io.write(ctx->io.opaque, 0, insn->target, 4, insn->value) ||
        ctx->io.write(ctx->io.opaque, 0, RECOVER_PBUS184C, 4, saved) ||
        ctx->io.write(ctx->io.opaque, 0, RECOVER_PBUS1850, 4, 0))
        return -1;
    gf_init_commit(ctx);
    return 0;
}
