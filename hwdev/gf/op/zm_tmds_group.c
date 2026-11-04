/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
/* nouveau c03c1a1 nvkm/subdev/bios/init.c:1121-1143. */
#include "../internal.h"
#define GF_TMDS_DATA 4U
#define GF_TMDS_PAIR 2U
int gf_op_zm_tmds_group_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    u32 base;
    if (ctx->skip) return 0;
    if (gf_init_tmds_base(ctx, insn->index, &base)) return -1;
    for (u32 i = 0; i < insn->count; i++) {
        const u8 *pair = ctx->rom + insn->ref + i * GF_TMDS_PAIR;
        if (ctx->io.write(ctx->io.opaque, 0, base + GF_TMDS_DATA, 4, pair[1]) ||
            ctx->io.write(ctx->io.opaque, 0, base, 4, pair[0])) return -1;
    }
    return 0;
}
