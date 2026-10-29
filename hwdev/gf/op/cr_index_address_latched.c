/* NV44A init 0x51; MIT nouveau c03c1a1 bios/init.c:1142-1173. */
#include "../internal.h"

int gf_op_cr_index_address_latched_run(struct gf_init_context *ctx,
                                       const struct gfinit_instruction *insn)
{
    u32 index_off = gf_init_index_alias(ctx, RECOVER_CRTC, insn->index);
    u32 other = gf_init_index_alias(ctx, RECOVER_CRTC, insn->value);
    u32 data_off = index_off + 1, old_index, old_latch;
    struct gf_io *io = &ctx->io;
    if (ctx->skip)
        return insn->index == RECOVER_CR44 || insn->value == RECOVER_CR44 ? -1 : 0;
    if (io->read(io->opaque, 0, index_off, 1, &old_index) ||
        gf_init_save(ctx, 0, index_off, 1, old_index) ||
        io->write(io->opaque, 0, index_off, 1, insn->index) ||
        io->read(io->opaque, 0, data_off, 1, &old_latch) ||
        gf_init_save_indexed(ctx, 0, index_off, data_off, insn->index, old_latch)) return -1;
    for (u32 k = 0; k < insn->count; k++) {
        if ((k && io->write(io->opaque, 0, index_off, 1, insn->index)) ||
            io->write(io->opaque, 0, data_off, 1, insn->mask + k) ||
            io->write(io->opaque, 0, other, 1, insn->value) ||
            io->write(io->opaque, 0, other + 1, 1, ctx->rom[insn->ref + k])) return -1;
    }
    if (io->write(io->opaque, 0, index_off, 1, insn->index) ||
        io->write(io->opaque, 0, data_off, 1, old_latch) ||
        io->write(io->opaque, 0, index_off, 1, old_index)) return -1;
    gf_init_commit(ctx);
    return 0;
}
