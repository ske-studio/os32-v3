/* NV44A init DATA decoder. S2_LAYOUT.final (G1-G3), DESIGN.final §4-6.
 * Semantics checked against MIT nouveau c03c1a1 bios/init.c handlers;
 * no x86 instructions, hardware access, or vendor ROM bytes here. */
#include "gfinit_decode.h"
#include "gfinit_pll.h"
#include "../lib/crc32_core.inc"
#define GF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))
#include "gfinit_decode/tables.inc"
#include "gfinit_decode/bytes.inc"
const struct gf_script_span *gfinit_script_spans(u32 *count)
{
    *count = GF_ARRAY(scripts);
    return scripts;
}
int gfinit_decode_instruction(const u8 *r, u32 size, u32 pc,
                             struct gfinit_instruction *o)
{
    if (!r || !o || span(size, pc, 1)) return -1;
    *o = (struct gfinit_instruction){.pc = pc, .opcode = r[pc], .end_repeat = RECOVER_NONE};
    u32 len, fixed, off = 0, unit = 0;
    switch (o->opcode) {
    case GF_INIT_IO_RESTRICT_PROG: case GF_INIT_IO_RESTRICT_PLL2: fixed = 11; off = 6; unit = 4; break;
    case GF_INIT_CR_INDEX_ADDRESS_LATCHED: fixed = 5; off = 4; unit = 1; break;
    case GF_INIT_ZM_CR_GROUP: fixed = 2; off = 1; unit = 2; break;
    case GF_INIT_ZM_REG_SEQUENCE: fixed = 6; off = 5; unit = 4; break;
    case GF_INIT_REPEAT: case GF_INIT_IO_FLAG_CONDITION: case GF_INIT_SUB: case GF_INIT_MACRO: case GF_INIT_CONDITION: fixed = 2; break;
    case GF_INIT_END_REPEAT: case GF_INIT_NOT: case GF_INIT_COMPUTE_MEM: case GF_INIT_DONE: case GF_INIT_RESUME: fixed = 1; break;
    case GF_INIT_COPY: fixed = 11; break;
    case GF_INIT_PLL2: case GF_INIT_ZM_REG: fixed = 9; break;
    case GF_INIT_CR: fixed = 4; break;
    case GF_INIT_ZM_CR: case GF_INIT_SUB_DIRECT: case GF_INIT_TIME: fixed = 3; break;
    case GF_INIT_COPY_NV_REG: fixed = 22; break;
    case GF_INIT_ZM_INDEX_IO: fixed = 5; break;
    case GF_INIT_RESET: case GF_INIT_NV_REG: fixed = 13; break;
    case GF_INIT_INDEX_IO: fixed = 6; break;
    default: return -1;
    }
    if (span(size, pc, fixed)) return -1;
    o->count = unit ? r[pc + off] : 0;
    len = fixed + unit * o->count; /* count is u8, bounded before multiplication */
    if (span(size, pc, len)) return -1;
    o->length = len;
    switch (o->opcode) {
    #include "gfinit_op/io_restrict_decode.inc"
    #include "gfinit_op/repeat_decode.inc"
    #include "gfinit_op/copy_decode.inc"
    #include "gfinit_op/condition_decode.inc"
    #include "gfinit_op/cr_index_address_latched_decode.inc"
    #include "gfinit_op/cr_decode.inc"
    #include "gfinit_op/zm_reg_sequence_decode.inc"
    case GF_INIT_SUB_DIRECT: o->ref = le(r, pc + 1, 2); break;
    #include "gfinit_op/sub_decode.inc"
    #include "gfinit_op/index_io_decode.inc"
    #include "gfinit_op/reset_nv_reg_decode.inc"
    #include "gfinit_op/pll2_zm_reg_decode.inc"
    case GF_INIT_TIME: o->value = le(r, pc + 1, 2); break;
    default: break;
    }
    if (o->target & 3U) return -1;
    if (o->port && o->port != RECOVER_CRTC && o->port != RECOVER_SEQ && o->port != RECOVER_GFX) return -1;
    if (o->opcode == GF_INIT_IO_RESTRICT_PLL2 || o->opcode == GF_INIT_PLL2) {
        struct gfinit_pll_limits limits;
        if (gfinit_pll_parse(r, size, GF_INIT_PLL_TABLE, o->target, &limits)) return -1;
    }
    return 0;
}
#ifndef GF_PRODUCTION_DECODE
int gfinit_decode_rom(const u8 *r, u32 size, const struct recover_profile *p,
                      struct gfinit_decode *d)
{
    if (!r || !p || !d) return -1;
    d->plan = (struct recover_plan){.version = 1, .profile_crc = p->crc, .rom_size = size,
        .nodes = d->nodes, .args = d->args, .entries = d->entries, .entry_count = GF_INIT_MAINS,
        .captures = d->captures, .ctx_count = 1};
    d->program.count = d->error = 0;
    for (u32 i = 0; i < GF_ARRAY(scripts); i++) {
        const struct gf_script_span *s = &scripts[i];
        if (span(size, s->pc, s->length)) return -1;
        u32 crc = CRC32_INIT;
        crc = crc32_core_update(crc, r + s->pc, s->length);
        if (crc32_core_final(crc) != s->crc) return -1;
        u32 pc = s->pc, stack[GF_INIT_REPEAT_DEPTH], depth = 0;
        while (pc < s->pc + s->length) {
            d->error_pc = pc;
            if (d->program.count == GF_INIT_INSNS) return -1;
            u32 id = d->program.count++;
            struct gfinit_instruction *v = &d->program.insns[id];
            if (gfinit_decode_instruction(r, size, pc, v) || v->length > s->pc + s->length - pc) return -1;
            if (v->opcode == GF_INIT_REPEAT) {
                if (depth == GF_INIT_REPEAT_DEPTH) return -1;
                stack[depth++] = id;
            }
            if (v->opcode == GF_INIT_END_REPEAT) {
                if (!depth) return -1;
                d->program.insns[stack[--depth]].end_repeat = pc;
            }
            if (v->opcode == GF_INIT_DONE && pc + 1 != s->pc + s->length) return -1;
            pc += v->length;
        }
        if (depth || r[pc - 1] != GF_INIT_DONE) return -1;
    }
    if (d->program.count != GF_INIT_INSNS) return -1;
    u32 crc = CRC32_INIT;
    crc = crc32_core_update(crc, r, size);
    d->plan.rom_crc = crc32_core_final(crc);
    for (u32 i = 0; i < GF_ARRAY(scripts); i++) if (scripts[i].id < GF_INIT_MAINS) {
        u32 main = scripts[i].id, next = main + 1;
        d->program.main_pc[main] = scripts[i].pc;
        d->entries[main] = (struct recover_plan_entry){REC_RUN, main, main};
        if (next == GF_INIT_MAINS) next = RECOVER_NONE;
        d->nodes[main] = (struct recover_plan_node){.phase=REC_RUN, .main_id=main,
            .pc=scripts[i].pc, .opcode=GF_INIT_RUNNER, .rule_id=RECOVER_NONE,
            .expr_id=RECOVER_NONE, .next={next,next}};
    }
    d->plan.node_count = GF_INIT_MAINS;
    return 0;
}
#endif /* diagnostic stage boundaries */
