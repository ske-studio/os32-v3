/*
 * Copyright 2009 Red Hat Inc.
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

/* NV4a specialization of nouveau c03c1a1, nvkm/engine/gr/ctxnv40.c
 * and ctxnv40.h (MIT). GF_FEAT2_DESIGN section 5 owns the values below.
 * Keep all general/3D/shader state, including the load/save program.
 * Registers here describe context slots; this file does not write BAR0.
 * B5 owns zero allocation, ctx[0], publication, upload and binding. */
#include "command.h"

/* ctxnv40.c:66-111, context-program encodings. */
#define CP_FLAG_SWAP_DIRECTION        0
#define CP_FLAG_SWAP_DIRECTION_LOAD   0
#define CP_FLAG_SWAP_DIRECTION_SAVE   1
#define CP_FLAG_USER_SAVE             5
#define CP_FLAG_USER_SAVE_NOT_PENDING 0
#define CP_FLAG_USER_SAVE_PENDING     1
#define CP_FLAG_USER_LOAD             6
#define CP_FLAG_USER_LOAD_NOT_PENDING 0
#define CP_FLAG_USER_LOAD_PENDING     1
#define CP_FLAG_STATUS                96
#define CP_FLAG_STATUS_IDLE           0
#define CP_FLAG_AUTO_SAVE             100
#define CP_FLAG_AUTO_SAVE_PENDING     1
#define CP_FLAG_AUTO_LOAD             101
#define CP_FLAG_AUTO_LOAD_PENDING     1
#define CP_FLAG_UNK54                 102
#define CP_FLAG_UNK54_CLEAR           0
#define CP_FLAG_ALWAYS                104
#define CP_FLAG_ALWAYS_TRUE           1
#define CP_FLAG_UNK57                 105
#define CP_FLAG_UNK57_CLEAR           0

#define CP_CTX                       0x00100000U
#define CP_CTX_COUNT                 0x000fc000U
#define CP_CTX_COUNT_SHIFT           14
#define CP_LOAD_SR                   0x00200000U
#define CP_BRA                       0x00400000U
#define CP_BRA_IP                    0x0000ff00U
#define CP_BRA_IP_SHIFT              8
#define CP_BRA_IF_CLEAR              0x00000080U
#define CP_WAIT                      0x00500000U
#define CP_WAIT_SET                  0x00000080U
#define CP_SET                       0x00700000U
#define CP_SET_1                     0x00000080U
#define CP_NEXT_TO_SWAP              0x00600007U
#define CP_NEXT_TO_CURRENT           0x00600009U
#define CP_SET_CONTEXT_POINTER       0x0060000aU
#define CP_END                       0x0060000eU

/* ctxnv40.c:120-143: NV4a has three VS units, independently of 1540.
 * ctxnv40.c:569-591: offsets and lengths are in dwords. */
enum {
    VS_NR = 3, VS_NR_B0 = 363, VS_NR_B1 = 64,
    VS_B0_OFFSET = 0x1d40 / 4, VS_B1_OFFSET = 0x3f40 / 4,
    VS_LEN = 0x4980 / 4, STATE3D_3_LEN = 0x0084
};

/* ctxnv40.h:6-22. Code belongs to the caller; labels are pass-local.
 * No RAM copy of the context image is retained in the module. */
struct gf2_grctx {
    enum { GRCTX_PROG, GRCTX_VALS } mode;
    u32 *ucode;
    struct gf_foundation *f;
    const struct gf_inst *data;
    u32 ctxprog_len, ctxprog_reg;
    u32 ctxprog_label[GF2_GRCTX_LABEL_WORDS];
    u32 ctxvals_pos, ctxvals_base;
    int rc;
};

/* ctxnv40.h:25-129: retain program/slot accounting in both passes.
 * The upstream BUG_ON at 256 words is the specified GF2_EIO here. */
static void cp_out(struct gf2_grctx *ctx, u32 inst)
{
    if (ctx->mode != GRCTX_PROG || ctx->rc)
        return;
    if (ctx->ctxprog_len == GF2_GRCTX_PROGRAM_WORDS) {
        ctx->rc = GF2_EIO;
        return;
    }
    ctx->ucode[ctx->ctxprog_len++] = inst;
}

static void cp_lsr(struct gf2_grctx *ctx, u32 val)
{
    cp_out(ctx, CP_LOAD_SR | val);
}

static void cp_ctx(struct gf2_grctx *ctx, u32 reg, u32 length)
{
    ctx->ctxprog_reg = (reg - 0x00400000) >> 2;
    ctx->ctxvals_base = ctx->ctxvals_pos;
    ctx->ctxvals_pos = ctx->ctxvals_base + length;
    if (length > (CP_CTX_COUNT >> CP_CTX_COUNT_SHIFT)) {
        cp_lsr(ctx, length);
        length = 0;
    }
    cp_out(ctx, CP_CTX | (length << CP_CTX_COUNT_SHIFT) | ctx->ctxprog_reg);
}

static void cp_name(struct gf2_grctx *ctx, int name)
{
    u32 *code = ctx->ucode;
    if (ctx->mode != GRCTX_PROG || ctx->rc)
        return;
    ctx->ctxprog_label[name] = ctx->ctxprog_len;
    for (u32 i = 0; i < ctx->ctxprog_len; i++) {
        if ((code[i] & 0xfff00000U) != 0xff400000U)
            continue;
        if ((code[i] & CP_BRA_IP) != ((u32)name << CP_BRA_IP_SHIFT))
            continue;
        code[i] = (code[i] & 0x00ff00ffU) |
                  (ctx->ctxprog_len << CP_BRA_IP_SHIFT);
    }
}

static void _cp_bra(struct gf2_grctx *ctx, int flag, int state, int name)
{
    u32 ip = ctx->ctxprog_label[name] << CP_BRA_IP_SHIFT;
    if (ip == 0)
        ip = 0xff000000U | ((u32)name << CP_BRA_IP_SHIFT);
    cp_out(ctx, CP_BRA | ip | flag | (state ? 0 : CP_BRA_IF_CLEAR));
}
#define cp_bra(c, f, s, n) _cp_bra((c), CP_FLAG_##f, CP_FLAG_##f##_##s, (n))

static void _cp_wait(struct gf2_grctx *ctx, int flag, int state)
{
    cp_out(ctx, CP_WAIT | flag | (state ? CP_WAIT_SET : 0));
}
#define cp_wait(c, f, s) _cp_wait((c), CP_FLAG_##f, CP_FLAG_##f##_##s)

static void _cp_set(struct gf2_grctx *ctx, int flag, int state)
{
    cp_out(ctx, CP_SET | flag | (state ? CP_SET_1 : 0));
}
#define cp_set(c, f, s) _cp_set((c), CP_FLAG_##f, CP_FLAG_##f##_##s)

static void cp_pos(struct gf2_grctx *ctx, u32 offset)
{
    ctx->ctxvals_pos = offset;
    ctx->ctxvals_base = ctx->ctxvals_pos;
    cp_lsr(ctx, ctx->ctxvals_pos);
    cp_out(ctx, CP_SET_CONTEXT_POINTER);
}

/* Replace nvkm_wo32 with the foundation's checked PRAMIN writer. Stop
 * writes on the first failure; the caller must discard the failed fill. */
static void ctx_write(struct gf2_grctx *ctx, u32 offset, u32 val)
{
    if (!ctx->rc && gf_inst_write32(ctx->f, ctx->data, offset, val))
        ctx->rc = GF2_EIO;
}

static void gr_def(struct gf2_grctx *ctx, u32 reg, u32 val)
{
    if (ctx->mode != GRCTX_VALS)
        return;
    reg = (reg - 0x00400000) / 4;
    reg = (reg - ctx->ctxprog_reg) + ctx->ctxvals_base;
    ctx_write(ctx, reg * 4, val);
}

/* ctxnv40.c:146-156. */
enum cp_label {
    cp_check_load = 1,
    cp_setup_auto_load,
    cp_setup_load,
    cp_setup_save,
    cp_swap_state,
    cp_swap_state3d_3_is_save,
    cp_prepare_exit,
    cp_exit
};

/* ctxnv40.c:159-262, selecting chipset 4a / nv44_gr_class=true. */
static void construct_general(struct gf2_grctx *ctx)
{
    cp_ctx(ctx, 0x4000a4, 1);
    gr_def(ctx, 0x4000a4, 0x00000008);
    cp_ctx(ctx, 0x400144, 58);
    gr_def(ctx, 0x400144, 0x00000001);
    cp_ctx(ctx, 0x400314, 1);
    gr_def(ctx, 0x400314, 0x00000000);
    cp_ctx(ctx, 0x400400, 10);
    cp_ctx(ctx, 0x400480, 10);
    cp_ctx(ctx, 0x400500, 19);
    gr_def(ctx, 0x400514, 0x00040000);
    gr_def(ctx, 0x400524, 0x55555555);
    gr_def(ctx, 0x400528, 0x55555555);
    gr_def(ctx, 0x40052c, 0x55555555);
    gr_def(ctx, 0x400530, 0x55555555);
    cp_ctx(ctx, 0x400560, 6);
    gr_def(ctx, 0x400568, 0x0000ffff);
    gr_def(ctx, 0x40056c, 0x0000ffff);
    cp_ctx(ctx, 0x40057c, 5);
    cp_ctx(ctx, 0x400710, 3);
    gr_def(ctx, 0x400710, 0x20010001);
    gr_def(ctx, 0x400714, 0x0f73ef00);
    cp_ctx(ctx, 0x400724, 1);
    gr_def(ctx, 0x400724, 0x02008821);
    cp_ctx(ctx, 0x400770, 3);
    cp_ctx(ctx, 0x400840, 20);
    for (int i = 0; i < 8; i++)
        gr_def(ctx, 0x400860 + (i * 4), 0x00000001);
    gr_def(ctx, 0x400880, 0x00000040);
    gr_def(ctx, 0x400884, 0x00000040);
    gr_def(ctx, 0x400888, 0x00000040);
    cp_ctx(ctx, 0x400894, 11);
    gr_def(ctx, 0x400894, 0x00000040);
    cp_ctx(ctx, 0x4008e0, 2);
    cp_ctx(ctx, 0x4008f8, 2);
    cp_ctx(ctx, 0x400a00, 73);
    gr_def(ctx, 0x400b0c, 0x0b0b0b0c);
    cp_ctx(ctx, 0x401000, 4);
    cp_ctx(ctx, 0x405004, 1);
    cp_ctx(ctx, 0x403440, 1);
    gr_def(ctx, 0x403440, 0x00003010);
}

/* ctxnv40.c:265-367, the full NV4a 3D state block 1. */
static void construct_state3d(struct gf2_grctx *ctx)
{
    cp_ctx(ctx, 0x40194c, 18);
    gr_def(ctx, 0x401954, 0x00000111);
    gr_def(ctx, 0x401958, 0x00080060);
    gr_def(ctx, 0x401974, 0x00000080);
    gr_def(ctx, 0x401978, 0xffff0000);
    gr_def(ctx, 0x40197c, 0x00000001);
    gr_def(ctx, 0x401990, 0x46400000);
    cp_ctx(ctx, 0x4019a0, 1);
    cp_ctx(ctx, 0x4019b4, 3);
    gr_def(ctx, 0x4019bc, 0xffff0000);
    cp_ctx(ctx, 0x401a08, 8);
    gr_def(ctx, 0x401a10, 0x0fff0000);
    gr_def(ctx, 0x401a14, 0x0fff0000);
    gr_def(ctx, 0x401a1c, 0x00011100);
    cp_ctx(ctx, 0x401a2c, 4);
    cp_ctx(ctx, 0x401a44, 26);
    for (int i = 0; i < 16; i++)
        gr_def(ctx, 0x401a44 + (i * 4), 0x07ff0000);
    gr_def(ctx, 0x401a8c, 0x4b7fffff);
    cp_ctx(ctx, 0x401ab8, 1);
    cp_ctx(ctx, 0x401ac0, 1);
    cp_ctx(ctx, 0x401ad0, 8);
    gr_def(ctx, 0x401ad0, 0x30201000);
    gr_def(ctx, 0x401ad4, 0x70605040);
    gr_def(ctx, 0x401ad8, 0xb8a89888);
    gr_def(ctx, 0x401adc, 0xf8e8d8c8);
    cp_ctx(ctx, 0x401b10, 1);
    gr_def(ctx, 0x401b10, 0x40100000);
    cp_ctx(ctx, 0x401b18, 5);
    gr_def(ctx, 0x401b28, 0x00000000);
    cp_ctx(ctx, 0x401b30, 25);
    gr_def(ctx, 0x401b34, 0x0000ffff);
    gr_def(ctx, 0x401b68, 0x435185d6);
    gr_def(ctx, 0x401b6c, 0x2155b699);
    gr_def(ctx, 0x401b70, 0xfedcba98);
    gr_def(ctx, 0x401b74, 0x00000098);
    gr_def(ctx, 0x401b84, 0xffffffff);
    gr_def(ctx, 0x401b88, 0x00ff7000);
    gr_def(ctx, 0x401b8c, 0x0000ffff);
    cp_ctx(ctx, 0x401b98, 8);
    gr_def(ctx, 0x401b9c, 0x00ff0000);
    cp_ctx(ctx, 0x401bc0, 9);
    gr_def(ctx, 0x401be0, 0x00ffff00);
    cp_ctx(ctx, 0x401c00, 192);
    for (int i = 0; i < 16; i++) { /* fragment texture units */
        gr_def(ctx, 0x401c40 + (i * 4), 0x00018488);
        gr_def(ctx, 0x401c80 + (i * 4), 0x00028202);
        gr_def(ctx, 0x401d00 + (i * 4), 0x0000aae4);
        gr_def(ctx, 0x401d40 + (i * 4), 0x01012000);
        gr_def(ctx, 0x401d80 + (i * 4), 0x00080008);
        gr_def(ctx, 0x401e00 + (i * 4), 0x00100008);
    }
    for (int i = 0; i < 4; i++) { /* vertex texture units */
        gr_def(ctx, 0x401e90 + (i * 4), 0x0001bc80);
        gr_def(ctx, 0x401ea0 + (i * 4), 0x00000202);
        gr_def(ctx, 0x401ec0 + (i * 4), 0x00000008);
        gr_def(ctx, 0x401ee0 + (i * 4), 0x00080008);
    }
    cp_ctx(ctx, 0x400f5c, 3);
    gr_def(ctx, 0x400f5c, 0x00000002);
    cp_ctx(ctx, 0x400f84, 1);
}

/* ctxnv40.c:370-531, the full NV4a 3D state block 2. */
static void construct_state3d_2(struct gf2_grctx *ctx)
{
    cp_ctx(ctx, 0x402000, 1);
    cp_ctx(ctx, 0x402404, 2);
    gr_def(ctx, 0x402404, 0x00000021);
    gr_def(ctx, 0x402408, 0x030c30c3);
    cp_ctx(ctx, 0x402440, 1);
    gr_def(ctx, 0x402440, 0x00011001);
    cp_ctx(ctx, 0x402480, 9);
    gr_def(ctx, 0x402488, 0x3e020200);
    gr_def(ctx, 0x40248c, 0x00ffffff);
    gr_def(ctx, 0x402490, 0x0c103f00);
    gr_def(ctx, 0x40249c, 0x00040000);
    cp_ctx(ctx, 0x402500, 31);
    gr_def(ctx, 0x402530, 0x00008100);
    cp_ctx(ctx, 0x402594, 16);
    cp_ctx(ctx, 0x402800, 17);
    gr_def(ctx, 0x402800, 0x00000001);
    cp_ctx(ctx, 0x402844, 1);
    gr_def(ctx, 0x402844, 0x00001001);
    cp_ctx(ctx, 0x402850, 2);
    gr_def(ctx, 0x402854, 0x00000003);
    cp_ctx(ctx, 0x402c00, 4);
    gr_def(ctx, 0x402c00, 0x00888001);
    cp_ctx(ctx, 0x402c10, 4);
    cp_ctx(ctx, 0x402c20, 16);
    cp_ctx(ctx, 0x402cb0, 13);
    gr_def(ctx, 0x402cd4, 0x00000005);
    gr_def(ctx, 0x402ce0, 0x0000ffff);
    cp_ctx(ctx, 0x403400, 3);
    cp_ctx(ctx, 0x403410, 3);
    cp_ctx(ctx, 0x403420, VS_NR);
    for (int i = 0; i < VS_NR; i++)
        gr_def(ctx, 0x403420 + (i * 4), 0x00005555);
    cp_ctx(ctx, 0x403600, 1);
    gr_def(ctx, 0x403600, 0x00000001);
    cp_ctx(ctx, 0x403800, 1);
    cp_ctx(ctx, 0x403c18, 1);
    gr_def(ctx, 0x403c18, 0x00000001);
    cp_ctx(ctx, 0x405800, 11);
    cp_ctx(ctx, 0x407000, 1);
}

/* ctxnv40.c:534-546, nv44 class has 0x84 words in this block. */
static void construct_state3d_3(struct gf2_grctx *ctx)
{
    cp_out(ctx, 0x300000);
    cp_lsr(ctx, STATE3D_3_LEN - 4);
    cp_bra(ctx, SWAP_DIRECTION, SAVE, cp_swap_state3d_3_is_save);
    cp_lsr(ctx, STATE3D_3_LEN);
    cp_name(ctx, cp_swap_state3d_3_is_save);
    cp_out(ctx, 0x800001);
    ctx->ctxvals_pos += STATE3D_3_LEN;
}

/* ctxnv40.c:549-593, NV4a shader layout; float values are bit constants. */
static void construct_shader(struct gf2_grctx *ctx)
{
    u32 offset;
    cp_lsr(ctx, VS_LEN * VS_NR + 0x300 / 4);
    cp_out(ctx, 0x800029);
    offset = ctx->ctxvals_pos;
    ctx->ctxvals_pos += 0x0300 / 4 + VS_NR * VS_LEN;
    if (ctx->mode != GRCTX_VALS)
        return;

    offset += 0x0280 / 4;
    for (int i = 0; i < 16; i++, offset += 2)
        ctx_write(ctx, offset * 4, 0x3f800000);
    for (int vs = 0; vs < VS_NR; vs++, offset += VS_LEN) {
        for (int i = 0; i < VS_NR_B0 * 6; i += 6)
            ctx_write(ctx, (offset + VS_B0_OFFSET + i) * 4, 0x00000001);
        for (int i = 0; i < VS_NR_B1 * 4; i += 4)
            ctx_write(ctx, (offset + VS_B1_OFFSET + i) * 4, 0x3f800000);
    }
}

/* ctxnv40.c:596-657: same entry/exit and load/save branches. */
static void generate(struct gf2_grctx *ctx)
{
    cp_bra(ctx, AUTO_SAVE, PENDING, cp_setup_save);
    cp_bra(ctx, USER_SAVE, PENDING, cp_setup_save);
    cp_name(ctx, cp_check_load);
    cp_bra(ctx, AUTO_LOAD, PENDING, cp_setup_auto_load);
    cp_bra(ctx, USER_LOAD, PENDING, cp_setup_load);
    cp_bra(ctx, ALWAYS, TRUE, cp_exit);

    cp_name(ctx, cp_setup_auto_load);
    cp_wait(ctx, STATUS, IDLE);
    cp_out(ctx, CP_NEXT_TO_SWAP);
    cp_name(ctx, cp_setup_load);
    cp_wait(ctx, STATUS, IDLE);
    cp_set(ctx, SWAP_DIRECTION, LOAD);
    cp_out(ctx, 0x00910880);
    cp_out(ctx, 0x00901ffe);
    cp_out(ctx, 0x01940000);
    cp_lsr(ctx, 0x20);
    cp_out(ctx, 0x0060000b);
    cp_wait(ctx, UNK57, CLEAR);
    cp_out(ctx, 0x0060000c);
    cp_bra(ctx, ALWAYS, TRUE, cp_swap_state);

    cp_name(ctx, cp_setup_save);
    cp_set(ctx, SWAP_DIRECTION, SAVE);
    cp_name(ctx, cp_swap_state);
    cp_pos(ctx, 0x00020 / 4);
    construct_general(ctx);
    cp_wait(ctx, STATUS, IDLE);

    cp_bra(ctx, UNK54, CLEAR, cp_prepare_exit);
    construct_state3d(ctx);
    cp_wait(ctx, STATUS, IDLE);
    construct_state3d_2(ctx);
    construct_state3d_3(ctx);
    cp_pos(ctx, ctx->ctxvals_pos);
    construct_shader(ctx);

    cp_name(ctx, cp_prepare_exit);
    cp_bra(ctx, SWAP_DIRECTION, SAVE, cp_check_load);
    cp_bra(ctx, USER_SAVE, PENDING, cp_exit);
    cp_out(ctx, CP_NEXT_TO_CURRENT);
    cp_name(ctx, cp_exit);
    cp_set(ctx, USER_SAVE, NOT_PENDING);
    cp_set(ctx, USER_LOAD, NOT_PENDING);
    cp_out(ctx, CP_END);
}

int gf2_grctx_program(u32 code[GF2_GRCTX_PROGRAM_WORDS], u32 *words, u32 *bytes)
{
    struct gf2_grctx ctx = { .mode = GRCTX_PROG, .ucode = code };
    generate(&ctx);
    if (ctx.rc)
        return ctx.rc;
    *words = ctx.ctxprog_len;
    *bytes = ctx.ctxvals_pos * 4;
    return GF2_OK;
}

int gf2_grctx_fill(struct gf_foundation *f, const struct gf_inst *data)
{
    struct gf2_grctx ctx = { .mode = GRCTX_VALS, .f = f, .data = data };
    generate(&ctx);
    return ctx.rc;
}
