/* Shared dispatch storage; I2 supplies the drawing implementations. */
#include "gfx2_sw.h"
const gfx2_sw_fn gfx2_sw_table[GFX2_OP_MAX + 1]
    __attribute__((section(".rodata.gfx2_sw_table"), used)) = {
    [GFX2_OP_FILL] = gfx2_sw_fill,
    [GFX2_OP_COPY] = gfx2_sw_copy,
    [GFX2_OP_STRETCH] = gfx2_sw_stretch,
    [GFX2_OP_MONO] = gfx2_sw_mono,
    [GFX2_OP_HOSTDATA] = gfx2_sw_hostdata,
    [GFX2_OP_ROP4] = gfx2_sw_rop4,
    [GFX2_OP_ALPHA] = gfx2_sw_alpha,
    [GFX2_OP_LINE] = gfx2_sw_line,
};

u8 gfx2_sw_rowbuf[2][GFX2_SW_ROW_BYTES];
