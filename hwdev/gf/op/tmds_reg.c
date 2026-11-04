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
/* nouveau c03c1a1 nvkm/subdev/bios/init.c:539-572,1093-1119.
 * OS32 passes the raw OR separately from the script's logical head. */
#include "../internal.h"
#define GF_TMDS_BASE 0x6808b0U
#define GF_TMDS_OTHER_BANK 8U
#define GF_TMDS_READ 0x10000U
#define GF_TMDS_DATA 4U
#define GF_TMDS_FROM_OR 0x80U
#define GF_TMDS_FROM_OR_XOR 0x81U
int gf_init_tmds_base(const struct gf_init_context *ctx, u32 selector, u32 *base)
{
    static const u32 offsets[] = {0,0,8,0,0x2000,0,0,0,0x2008,0,0,0,0x2000};
    static const u32 direct[] = {0x6808b0,0x6808b8,0x6828b0,0x6828b8};
    if (selector < sizeof(direct)/sizeof(direct[0])) {
        *base = direct[selector];
        return 0;
    }
    if ((selector != GF_TMDS_FROM_OR && selector != GF_TMDS_FROM_OR_XOR) || !ctx->display_script ||
        ctx->or_mask >= sizeof(offsets)/sizeof(offsets[0])) return -1;
    u32 offset = offsets[ctx->or_mask];
    if (selector == GF_TMDS_FROM_OR_XOR) offset ^= GF_TMDS_OTHER_BANK;
    *base = GF_TMDS_BASE + offset;
    return 0;
}
int gf_op_tmds_reg_run(struct gf_init_context *ctx, const struct gfinit_instruction *insn)
{
    u32 base, old;
    if (ctx->skip) return 0;
    if (gf_init_tmds_base(ctx, insn->index, &base) ||
        ctx->io.write(ctx->io.opaque, 0, base, 4, insn->target | GF_TMDS_READ) ||
        ctx->io.read(ctx->io.opaque, 0, base + GF_TMDS_DATA, 4, &old) ||
        ctx->io.write(ctx->io.opaque, 0, base + GF_TMDS_DATA, 4,
                       (old & insn->mask) | insn->value) ||
        ctx->io.write(ctx->io.opaque, 0, base, 4, insn->target)) return -1;
    return 0;
}
