/* NV44A init: semantics checked against MIT nouveau c03c1a1,
 * nvkm/subdev/bios/init.c:1281-1299 (init_zm_reg_sequence). */
#include "../internal.h"

#define GF_SEQUENCE_WORD_BYTES 4U
#define GF_SEQUENCE_BYTE_BITS 8U

int gf_op_zm_reg_sequence_run(struct gf_init_context *ctx,
                              const struct gfinit_instruction *insn)
{
    if (ctx->skip) return 0;
    for (u32 i = 0; i < insn->count; i++) {
        u32 at = insn->ref + GF_SEQUENCE_WORD_BYTES * i;
        u32 value = 0;
        for (u32 j = 0; j < GF_SEQUENCE_WORD_BYTES; j++)
            value |= (u32)ctx->rom[at + j] << (GF_SEQUENCE_BYTE_BITS * j);
        if (ctx->io.write(ctx->io.opaque, KPEEK_NV_BAR_MMIO,
                          insn->target + GF_SEQUENCE_WORD_BYTES * i,
                          GF_SEQUENCE_WORD_BYTES, value)) return -1;
    }
    return 0;
}
