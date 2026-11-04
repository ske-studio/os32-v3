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
/* Bounded direct runner; control model: MIT nouveau bios/init.c (c03c1a1),
 * S1 spans/decoder remain shared with diagnostics. */
#include "internal.h"
#include "../../lib/crc32_core.inc"
#define GF_ROM_BIT_ONLY
#include "rom.inc"
#undef GF_ROM_BIT_ONLY

#define OP(name, code) {GF_INIT_##code, gf_op_##name##_run}
const struct gf_op_dispatch gf_op_dispatch[] = {
    OP(io_restrict_prog, IO_RESTRICT_PROG), OP(repeat, REPEAT),
    OP(end_repeat, END_REPEAT), OP(copy, COPY), OP(not, NOT),
    OP(io_flag_condition, IO_FLAG_CONDITION), OP(io_restrict_pll2, IO_RESTRICT_PLL2),
    OP(pll2, PLL2), OP(cr_index_address_latched, CR_INDEX_ADDRESS_LATCHED),
    OP(cr, CR), OP(zm_cr, ZM_CR), OP(zm_cr_group, ZM_CR_GROUP),
    OP(zm_reg_sequence, ZM_REG_SEQUENCE), OP(sub_direct, SUB_DIRECT),
    OP(copy_nv_reg, COPY_NV_REG), OP(zm_index_io, ZM_INDEX_IO),
    OP(compute_mem, COMPUTE_MEM), OP(reset, RESET), OP(sub, SUB),
    OP(nv_reg, NV_REG), OP(macro, MACRO), OP(done, DONE), OP(resume, RESUME),
    OP(tmds_reg, TMDS_REG), OP(zm_tmds_group, ZM_TMDS_GROUP),
    OP(time, TIME), OP(condition, CONDITION), OP(index_io, INDEX_IO), OP(zm_reg, ZM_REG)
};
#undef OP
const u32 gf_op_dispatch_count = sizeof(gf_op_dispatch) / sizeof(gf_op_dispatch[0]);

static int init_point(struct gf_init_context *ctx)
{
    u32 now;
    if (ctx->display_script) {
        if (ctx->io.timing.clock_us(ctx->io.opaque, &now)) return -1;
    } else if (ctx->io.point(ctx->io.opaque) ||
               ctx->io.timing.clock_us(ctx->io.opaque, &now)) return -1;
    ctx->elapsed_us = now - ctx->started_us;
    if (ctx->elapsed_us >= GF_INIT_EXEC_US ||
        (ctx->display_script && (i32)(now - ctx->deadline) >= 0)) {
        if (ctx->display_script) ctx->last_rc = GF_SCRIPT_ETIME;
        return -1;
    }
    if (!ctx->display_script) return 0;
    int rc = ctx->io.point(ctx->io.opaque);
    /* Production point reads the clock again and updates elapsed_us. */
    if (ctx->elapsed_us >= GF_INIT_EXEC_US) {
        ctx->last_rc = GF_SCRIPT_ETIME;
        return -1;
    }
    return rc;
}
int gf_init_wait(struct gf_init_context *ctx, u32 us)
{
    if (us > GF_INIT_WAIT_MAX - ctx->wait_us) {
        if (ctx->display_script) ctx->last_rc = GF_SCRIPT_ETIME;
        return -1;
    }
    ctx->wait_us += us;
    while (us) {
        u32 part = us > GF_INIT_WAIT_CHUNK_US ? GF_INIT_WAIT_CHUNK_US : us;
        if (init_point(ctx) || ctx->io.timing.wait_us(ctx->io.opaque, part) ||
            init_point(ctx)) return -1;
        us -= part;
    }
    return 0;
}
static int a2_write(struct gf_init_context *ctx,u32 off,u32 width,u32 value)
{
    return init_point(ctx) || ctx->io.write(ctx->io.opaque,0,off,width,value) ? -1 : 0;
}
int gf_init_a2(struct gf_init_context *ctx,int (*decode_safe)(void *))
{
    u32 value,cr=RECOVER_PRMCIO+RECOVER_CRTC;
    if (a2_write(ctx,RECOVER_PBUS1854,4,0) || a2_write(ctx,RECOVER_PBUS1850,4,0) ||
        a2_write(ctx,RECOVER_GF_PMC_IRQ,4,0) ||
        ctx->io.read(ctx->io.opaque,0,RECOVER_PMC_ENABLE,4,&value) ||
        a2_write(ctx,RECOVER_PMC_ENABLE,4,value|RECOVER_PMC_ENABLE_BIT0) ||
        decode_safe(ctx->io.opaque) ||
        a2_write(ctx,cr,1,RECOVER_CR44) || a2_write(ctx,cr+1,1,0) ||
        a2_write(ctx,cr,1,RECOVER_CR1F) || a2_write(ctx,cr+1,1,RECOVER_CR1F_UNLOCK) ||
        a2_write(ctx,cr,1,RECOVER_CR11) || ctx->io.read(ctx->io.opaque,0,cr+1,1,&value) ||
        a2_write(ctx,cr+1,1,value&~RECOVER_CR11_PROTECT)) return -1;
    return init_point(ctx);
}
/* One sequential span, never follow calls while matching REPEAT pairs. */
static int init_prepare_span(struct gf_init_context *ctx, struct gf_init_program *work,
                              const struct gf_script_span *s)
{
    u32 stack[GF_INIT_REPEAT_DEPTH], depth = 0, pc = s->pc;
    if (!s->length || pc > ctx->rom_size || s->length > ctx->rom_size - pc) return -1;
    u32 end = pc + s->length;
    while (pc < end) {
        ctx->fail_pc = pc;
        if (work->count == GF_INIT_INSNS) return -1;
        struct gfinit_instruction *v = &work->insns[work->count];
        if (gfinit_decode_instruction(ctx->rom, ctx->rom_size, pc, v) ||
            v->length > end - pc) return -1;
        if (v->opcode == GF_INIT_REPEAT) {
            if (depth == GF_INIT_REPEAT_DEPTH) return -1;
            stack[depth++] = work->count;
        } else if (v->opcode == GF_INIT_END_REPEAT) {
            if (!depth) return -1;
            work->insns[stack[--depth]].end_repeat = pc;
        }
        if (v->opcode == GF_INIT_DONE && pc + v->length != end) return -1;
        work->count++;
        pc += v->length;
    }
    return depth || ctx->rom[end - 1] != GF_INIT_DONE ? -1 : 0;
}
static int init_execute(struct gf_init_context *ctx, const struct gf_init_program *work)
{
    while (ctx->pc != RECOVER_NONE) {
        ctx->fail_pc = ctx->pc;
        if (ctx->steps == GF_INIT_SCHEDULE) {
            if (ctx->display_script) ctx->last_rc = GF_SCRIPT_ETIME;
            return -1;
        }
        if (init_point(ctx)) return -1;
        ctx->steps++;
        const struct gfinit_instruction *v = 0;
        for (u32 i = 0; i < work->count; i++)
            if (work->insns[i].pc == ctx->pc) { v = &work->insns[i]; break; }
        if (!v) return -1;
        ctx->pc += v->length;
        gf_op_run_fn run = 0;
        for (u32 i = 0; i < gf_op_dispatch_count; i++)
            if (gf_op_dispatch[i].opcode == v->opcode) { run = gf_op_dispatch[i].run; break; }
        if (!run || (ctx->io.trace && ctx->io.trace(ctx->io.opaque,ctx,true,0))) return -1;
        int rc = run(ctx,v);
        if (ctx->io.trace && ctx->io.trace(ctx->io.opaque,ctx,false,rc)) return -1;
        if (rc || init_point(ctx)) return -1;
    }
    return ctx->calls || ctx->repeats ? -1 : 0;
}
int gf_init_run_one(struct gf_init_context *ctx, const struct gf_init_program *work)
{
    ctx->display_script = false;
    ctx->pc = work->main_pc[ctx->main_id];
    ctx->head = ctx->calls = ctx->repeats = 0;
    ctx->skip = false;
    return init_execute(ctx, work);
}
/* A sequential column is decoded once, independently pairing its repeats.
 * Calls are discovered by a subsequent work scan, never expanded. Existing
 * work is both instruction storage and the finite graph's visited set. */
static int init_script_column(struct gf_init_context *ctx, struct gf_init_program *work,
                               u32 table, u32 pc)
{
    u32 stack[GF_INIT_REPEAT_DEPTH], depth = 0;
    for (;;) {
        ctx->fail_pc = pc;
        if (init_point(ctx)) return -1;
        u32 id = work->count;
        for (u32 i = 0; i < work->count; i++) {
            const struct gfinit_instruction *old = &work->insns[i];
            if (old->pc == pc) id = i;
            else if (pc > old->pc && pc - old->pc < old->length) return -1;
        }
        if (id == work->count) {
            if (work->count == GF_INIT_INSNS) return -1;
            struct gfinit_instruction *fresh = &work->insns[id];
            if (gfinit_decode_script_instruction(ctx->rom, ctx->rom_size, table, pc, fresh)) return -1;
            /* An alternate entry cannot turn a previous instruction's bytes
             * into operands of a different instruction. */
            for (u32 i = 0; i < work->count; i++)
                if (work->insns[i].pc > pc && work->insns[i].pc - pc < fresh->length) return -1;
            work->count++;
        }
        struct gfinit_instruction *v = &work->insns[id];
        if (v->opcode == GF_INIT_REPEAT) {
            if (depth == GF_INIT_REPEAT_DEPTH) return -1;
            stack[depth++] = id;
        } else if (v->opcode == GF_INIT_END_REPEAT) {
            if (!depth) return -1;
            work->insns[stack[--depth]].end_repeat = pc;
        }
        if (init_point(ctx)) return -1;
        if (v->opcode == GF_INIT_DONE) return depth ? -1 : 0;
        pc += v->length;
    }
}
int gf_init_run_script(struct gf_init_context *ctx, struct gf_init_program *work,
                       u32 pc, u8 head, u8 or_mask, u32 dl)
{
    /* Invalid entry never consumes an outstanding transaction. */
    if (!ctx || !work || !ctx->rom || !ctx->io.read || !ctx->io.write ||
        !ctx->io.point || !ctx->io.restore || !ctx->io.timing.clock_us ||
        !ctx->io.timing.wait_us || !pc || pc >= ctx->rom_size || ctx->saved_count)
        return GF_SCRIPT_EIO;
    ctx->display_script = true; ctx->deadline = dl; ctx->or_mask = or_mask;
    ctx->head = head; ctx->pc = pc;
    ctx->calls = ctx->repeats = ctx->steps = ctx->wait_us = ctx->elapsed_us = 0;
    ctx->skip = false; ctx->fail_pc = pc; ctx->last_rc = GF_SCRIPT_EIO;
    work->count = 0;
    u32 table, length;
    u8 version;
    if (init_point(ctx) ||
        gf_rom_bit_entry(ctx->rom, ctx->rom_size, 'I', &table, &length, &version) ||
        version != GF_INIT_BIT_VERSION || !table || length < GF_INIT_BIT_MIN_LENGTH ||
        init_script_column(ctx, work, table, pc)) goto fail;
    for (u32 i = 0; i < work->count; i++) {
        const struct gfinit_instruction *v = &work->insns[i];
        if ((v->opcode == GF_INIT_SUB || v->opcode == GF_INIT_SUB_DIRECT) &&
            !(v->opcode == GF_INIT_SUB && !v->ref)) {
            ctx->fail_pc = v->pc;
            u32 target = v->ref;
            if (!target || target >= ctx->rom_size ||
                init_script_column(ctx, work, table, target)) goto fail;
        }
    }
    if (init_point(ctx) || init_execute(ctx, work)) goto fail;
    ctx->last_rc = GF_SCRIPT_OK;
    return GF_SCRIPT_OK;
fail: {
        int rc = ctx->last_rc;
        /* Cleanup is unconditional and cannot replace the original reason. */
        (void)ctx->io.restore(ctx->io.opaque, ctx);
        ctx->last_rc = rc;
        return rc;
    }
}
int gf_init_prepare(struct gf_init_context *ctx, struct gf_init_program *work)
{
    ctx->display_script = false;
    u32 count;
    const struct gf_script_span *spans = gfinit_script_spans(&count);
    work->count = 0;
    for (u32 i = 0; i < count; i++) {
        const struct gf_script_span *s = &spans[i];
        if (init_point(ctx) || init_prepare_span(ctx, work, s)) return -1;
        u32 crc = crc32_core_update(CRC32_INIT, ctx->rom + s->pc, s->length);
        if (crc32_core_final(crc) != s->crc) return -1;
        if (s->id < GF_INIT_MAINS) work->main_pc[s->id] = s->pc;
    }
    return init_point(ctx);
}
int gf_init_run(struct gf_init_context *ctx, struct gf_init_program *work)
{
    ctx->display_script = false;
    ctx->steps = ctx->wait_us = ctx->elapsed_us = 0;
    ctx->fail_pc = RECOVER_NONE;
    ctx->last_rc = -1;
    if (ctx->saved_count || gf_init_prepare(ctx, work)) goto fail;
    for (ctx->main_id = 0; ctx->main_id < GF_INIT_MAINS; ctx->main_id++)
        if (gf_init_run_one(ctx, work)) goto fail;
    ctx->main_id = GF_INIT_MAINS - 1;
    ctx->last_rc = 0;
    return 0;
fail:
    ctx->io.restore(ctx->io.opaque, ctx);
    return -1;
}
