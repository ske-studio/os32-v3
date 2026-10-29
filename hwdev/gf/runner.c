/* Bounded direct runner; control model: MIT nouveau bios/init.c (c03c1a1),
 * S1 spans/decoder remain shared with diagnostics. */
#include "internal.h"
#include "../../lib/crc32_core.inc"

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
    OP(time, TIME), OP(condition, CONDITION), OP(index_io, INDEX_IO), OP(zm_reg, ZM_REG)
};
#undef OP
const u32 gf_op_dispatch_count = sizeof(gf_op_dispatch) / sizeof(gf_op_dispatch[0]);

static int init_point(struct gf_init_context *ctx)
{
    u32 now;
    if (ctx->io.point(ctx->io.opaque) ||
        ctx->io.timing.clock_us(ctx->io.opaque, &now)) return -1;
    ctx->elapsed_us = now - ctx->started_us;
    return ctx->elapsed_us >= GF_INIT_EXEC_US ? -1 : 0;
}
int gf_init_wait(struct gf_init_context *ctx, u32 us)
{
    if (us > GF_INIT_WAIT_MAX - ctx->wait_us) return -1;
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
int gf_init_run_one(struct gf_init_context *ctx, const struct gf_init_program *work)
{
    ctx->pc = work->main_pc[ctx->main_id];
    ctx->head = ctx->calls = ctx->repeats = 0;
    ctx->skip = false;
    while (ctx->pc != RECOVER_NONE) {
        ctx->fail_pc = ctx->pc;
        if (ctx->steps == GF_INIT_SCHEDULE || init_point(ctx)) return -1;
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
int gf_init_prepare(struct gf_init_context *ctx, struct gf_init_program *work)
{
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
