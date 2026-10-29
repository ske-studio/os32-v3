/* NV44A init semantics: MIT nouveau c03c1a1 bios/init.c:1213-1230. */
#include "../internal.h"

#define GF_ZM_CR_GROUP_PAIRS 2U
#define GF_ZM_CR_GROUP_PAIR_SIZE 2U
#define GF_ZM_CR_GROUP_DATA 1U
#define GF_ZM_CR_GROUP_BAR 0U
#define GF_ZM_CR_GROUP_WIDTH 1U

int gf_op_zm_cr_group_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    for (u32 i = 0; i < insn->count; i++) {
        u32 pos = insn->pc + GF_ZM_CR_GROUP_PAIRS + GF_ZM_CR_GROUP_PAIR_SIZE * i;
        u32 index = ctx->rom[pos], value = ctx->rom[pos + GF_ZM_CR_GROUP_DATA];
        u32 head = index == RECOVER_CR44 ? 0 : ctx->head;
        u32 off = RECOVER_PRMCIO + head * RECOVER_HEAD_STRIDE + RECOVER_CRTC;
        if (!ctx->skip &&
            (ctx->io.write(ctx->io.opaque, GF_ZM_CR_GROUP_BAR, off, GF_ZM_CR_GROUP_WIDTH, index) ||
             ctx->io.write(ctx->io.opaque, GF_ZM_CR_GROUP_BAR, off + GF_ZM_CR_GROUP_DATA, GF_ZM_CR_GROUP_WIDTH, value)))
            return -1;
        if (index == RECOVER_CR44) ctx->head = value == RECOVER_CR44_HEAD1;
    }
    return 0;
}
