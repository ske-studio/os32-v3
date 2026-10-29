/* NV44A init COPY_NV_REG: MIT nouveau c03c1a1
 * nvkm/subdev/bios/init.c:1411-1434. */
#include "../internal.h"

#define COPY_NV_SRC_MASK 6U
#define COPY_NV_SRC_XOR 10U
#define COPY_NV_WORD_BYTES 4U
#define COPY_NV_BAR 0U

int gf_op_copy_nv_reg_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    u32 src, dst, mask = 0, x = 0;
    if (ctx->skip) return 0;
    for (u32 i = 0; i < COPY_NV_WORD_BYTES; i++) {
        mask |= (u32)ctx->rom[insn->pc + COPY_NV_SRC_MASK + i] << (i * 8U);
        x |= (u32)ctx->rom[insn->pc + COPY_NV_SRC_XOR + i] << (i * 8U);
    }
    if (ctx->io.read(ctx->io.opaque, COPY_NV_BAR, insn->ref, COPY_NV_WORD_BYTES, &src)) return -1;
    if (ctx->io.read(ctx->io.opaque, COPY_NV_BAR, insn->target, COPY_NV_WORD_BYTES, &dst)) return -1;
    src = insn->shift < GF_SHIFT_LEFT ? src >> insn->shift : src << (GF_SHIFT_WRAP - insn->shift);
    return ctx->io.write(ctx->io.opaque, COPY_NV_BAR, insn->target, COPY_NV_WORD_BYTES,
                         (dst & insn->mask) | ((src & mask) ^ x));
}
