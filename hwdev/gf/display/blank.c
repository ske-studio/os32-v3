/* [HW3] nouveau dispnv04/hw.c:110-124, hw.h:243-253, crtc.c:721.
 * Head A is fixed by the GF display design; modeset owns DPMS separately. */
#include "hwdev/gf/internal.h"

#define GF_SEQ_INDEX_PORT 0x0c03c4U
#define GF_SEQ_DATA_PORT  (GF_SEQ_INDEX_PORT + 1U)
#define GF_SEQ_RESET_INDEX 0U
#define GF_SEQ_CLOCK_INDEX 1U
#define GF_SEQ_RESET_START 1U
#define GF_SEQ_RESET_END 3U
#define GF_SEQ_SCREEN_OFF 0x20U
#define GF_BAR_REGISTERS 0U
#define GF_SEQ_WIDTH 1U

int gf_display_blank(struct gf_display_context *ctx, int blank)
{
    u32 seq;
    struct gf_io *io;

    if (!ctx || (blank != 0 && blank != 1)) return OS32_ERR_INVAL;
    if (ctx->blank == (bool)blank) return 0;
    io = &ctx->io;
    if (io->write(io->opaque, GF_BAR_REGISTERS, GF_SEQ_INDEX_PORT, GF_SEQ_WIDTH, GF_SEQ_CLOCK_INDEX) ||
        io->read(io->opaque, GF_BAR_REGISTERS, GF_SEQ_DATA_PORT, GF_SEQ_WIDTH, &seq) ||
        io->write(io->opaque, GF_BAR_REGISTERS, GF_SEQ_INDEX_PORT, GF_SEQ_WIDTH, GF_SEQ_RESET_INDEX) ||
        io->write(io->opaque, GF_BAR_REGISTERS, GF_SEQ_DATA_PORT, GF_SEQ_WIDTH, GF_SEQ_RESET_START) ||
        io->write(io->opaque, GF_BAR_REGISTERS, GF_SEQ_INDEX_PORT, GF_SEQ_WIDTH, GF_SEQ_CLOCK_INDEX) ||
        io->write(io->opaque, GF_BAR_REGISTERS, GF_SEQ_DATA_PORT, GF_SEQ_WIDTH,
                  blank ? seq | GF_SEQ_SCREEN_OFF : seq & ~GF_SEQ_SCREEN_OFF) ||
        io->write(io->opaque, GF_BAR_REGISTERS, GF_SEQ_INDEX_PORT, GF_SEQ_WIDTH, GF_SEQ_RESET_INDEX) ||
        io->write(io->opaque, GF_BAR_REGISTERS, GF_SEQ_DATA_PORT, GF_SEQ_WIDTH, GF_SEQ_RESET_END)) {
        ctx->reusable = false;
        return -1;
    }
    ctx->blank = (bool)blank;
    return 0;
}
