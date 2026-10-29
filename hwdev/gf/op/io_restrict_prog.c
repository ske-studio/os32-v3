/* IO_RESTRICT_PROG: MIT nouveau nvkm/subdev/bios/init.c:621-650.
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 * Authors: Ben Skeggs
 */
#include "../internal.h"

int gf_op_io_restrict_prog_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    if (ctx->skip)
        return insn->port == RECOVER_CRTC && insn->index == RECOVER_CR44 ? -1 : 0;
    u32 off = gf_init_index_alias(ctx, insn->port, insn->index);
    u32 value;
    if (ctx->io.write(ctx->io.opaque, 0, off, 1, insn->index) ||
        ctx->io.read(ctx->io.opaque, 0, off + 1, 1, &value)) return -1;
    u32 pos = insn->ref + 4 * ((value & insn->mask) >> insn->shift);
    u32 data = (u32)ctx->rom[pos] | (u32)ctx->rom[pos + 1] << 8 |
               (u32)ctx->rom[pos + 2] << 16 | (u32)ctx->rom[pos + 3] << 24;
    return ctx->io.write(ctx->io.opaque, 0, insn->target, 4, data) ? -1 : 0;
}
