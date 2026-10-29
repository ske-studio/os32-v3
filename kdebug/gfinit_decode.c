/* NV44A init DATA decoder. S2_LAYOUT.final (G1-G3), DESIGN.final §4-6.
 * Semantics checked against MIT nouveau c03c1a1 bios/init.c handlers;
 * no x86 instructions, hardware access, or vendor ROM bytes here. */
#include "gfinit_decode.h"
#include "gfinit_pll.h"
#include "../lib/crc32_core.inc"
#define GF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))
STATIC_ASSERT(GF_INIT_WORK_STEPS >= RECOVER_NODE_MAX, gf_schedule_scratch);
STATIC_ASSERT(GF_INIT_FIXED_CAPTURES <= RECOVER_CAPTURE_MAX, gf_fixed_captures);
#include "gfinit_decode/tables.inc"
#include "gfinit_decode/bytes.inc"
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
#include "gfinit_decode/schedule.inc"
#include "gfinit_decode/emit.inc"
#include "gfinit_op/pll_expand.inc"
static int expand_step(struct gfinit_decode *d, const u8 *r, const struct recover_profile *p,
                       u32 step, u32 state)
{
    const struct gfinit_scheduled *s = &d->schedule[step];
    const struct gfinit_instruction *v = &d->insns[s->instruction];
    u32 op = v->opcode, pc = v->pc, e = v->effect_first, head = GF_STATE_HEAD(state);
    u32 next_state = state, next_step = step + 1, first = d->plan.node_count;
    u32 t = key_for(p, v->target, v->port, v->index, head);
    if (s->kind) {
        if (s->kind == 1) control(d, p, s, state, GF_INIT_C+1, pc+v->length, 0, 0, 1);
        if (s->kind == 2) control(d, p, s, state, GF_INIT_C+1+s->k, s->k, 0, 0, 1);
        if (s->kind == 3) control(d, p, s, state, GF_INIT_C+1+s->count, s->count, 0, 0, 1);
    } else if ((state & GF_STATE_SKIP) && op != GF_INIT_REPEAT && op != GF_INIT_END_REPEAT && op != GF_INIT_NOT &&
               op != GF_INIT_DONE && op != GF_INIT_RESUME && op != GF_INIT_RESET && op != GF_INIT_COMPUTE_MEM) {
        if (op != GF_INIT_ZM_CR && op != GF_INIT_ZM_CR_GROUP && v->port == RECOVER_CRTC &&
            (v->index == RECOVER_CR44 || (op == GF_INIT_CR_INDEX_ADDRESS_LATCHED && v->value == RECOVER_CR44))) return -1;
        control(d, p, s, state, GF_INIT_K+pc, v->length, 0, 0, 1);
        if (op == GF_INIT_SUB_DIRECT || op == GF_INIT_SUB) next_step = s->jump;
        if (op == GF_INIT_ZM_CR && v->index == RECOVER_CR44) next_state = (v->value == RECOVER_CR44_HEAD1 ? GF_STATE_HEAD1 : 0) | GF_STATE_SKIP;
        if (op == GF_INIT_ZM_CR_GROUP) for (u32 j = 0; j < v->count; j++)
            if (r[pc+2+2*j] == RECOVER_CR44) next_state = (r[pc+3+2*j] == RECOVER_CR44_HEAD1 ? GF_STATE_HEAD1 : 0) | GF_STATE_SKIP;
    } else switch (op) {
    case GF_INIT_REPEAT: control(d, p, s, state, GF_INIT_C, v->count, v->end_repeat, 0, 2); break;
    case GF_INIT_END_REPEAT: control(d, p, s, state, GF_INIT_C, s->jump, s->k, s->count, 3); break;
    case GF_INIT_NOT: control(d, p, s, state, GF_INIT_C, 0, 0, 0, 0); next_state ^= GF_STATE_SKIP; break;
    case GF_INIT_RESUME: control(d, p, s, state, GF_INIT_C, 0, 0, 0, 0); next_state &= ~GF_STATE_SKIP; break;
    case GF_INIT_COMPUTE_MEM: case GF_INIT_DONE: control(d, p, s, state, GF_INIT_C, 0, 0, 0, 0); break;
    case GF_INIT_TIME: control(d, p, s, state, GF_INIT_C, v->value, 0, 0, 1); break;
    case GF_INIT_SUB_DIRECT: control(d, p, s, state, GF_INIT_C, v->ref, pc+v->length, 0, 2); break;
    case GF_INIT_SUB: control(d, p, s, state, GF_INIT_C, v->index, v->ref, pc+v->length, 3); break;
    #include "gfinit_op/condition_expand.inc"
    #include "gfinit_op/io_restrict_expand.inc"
    case GF_INIT_PLL2: if (expand_pll(d, r, p, s, state, v->value)) return -1; break;
    case GF_INIT_NV_REG: write_node(d, p, s, state, e, REC_WRITE, t, v->mask, v->value, true); break;
    case GF_INIT_ZM_REG: case GF_INIT_MACRO: write_node(d, p, s, state, e, REC_WRITE, t, 0, v->value, false); break;
    #include "gfinit_op/cr_index_io_expand.inc"
    #include "gfinit_op/zm_cr_group_expand.inc"
    #include "gfinit_op/zm_reg_sequence_expand.inc"
    #include "gfinit_op/copy_expand.inc"
    #include "gfinit_op/reset_expand.inc"
    #include "gfinit_op/cr_index_address_latched_expand.inc"
    default: return -1;
    }
    if (d->error || first == d->plan.node_count || next_step > d->schedule_count) return -1;
    d->nodes[d->plan.node_count-1].next[0] = d->nodes[d->plan.node_count-1].next[1] = destination(next_step, next_state);
    if (next_step < d->schedule_count) d->reachable[next_step] |= 1U << next_state;
    return 0;
}
int gfinit_decode_rom(const u8 *r, u32 size, const struct recover_profile *p,
                      struct gfinit_decode *d)
{
    if (!r || !p || !d) return -1;
    d->plan = (struct recover_plan){.version = 1, .profile_crc = p->crc, .rom_size = size,
        .nodes = d->nodes, .args = d->args, .entries = d->entries, .entry_count = GF_INIT_MAINS,
        .captures = d->captures, .capture_count = GF_INIT_FIXED_CAPTURES};
    d->instruction_count = d->schedule_count = d->error = d->max_call = d->max_repeat = 0;
    u32 active[GF_INIT_INSNS] = {0}, main_start[GF_INIT_MAINS];
    for (u32 i = 0; i < GF_ARRAY(scripts); i++) {
        const struct gf_script_span *s = &scripts[i];
        if (span(size, s->pc, s->length)) return -1;
        u32 crc = CRC32_INIT;
        crc = crc32_core_update(crc, r + s->pc, s->length);
        if (crc32_core_final(crc) != s->crc) return -1;
        u32 pc = s->pc, stack[GF_INIT_REPEAT_DEPTH], depth = 0;
        while (pc < s->pc + s->length) {
            d->error_pc = pc;
            if (d->instruction_count == GF_INIT_INSNS) return -1;
            u32 id = d->instruction_count++;
            struct gfinit_instruction *v = &d->insns[id];
            if (gfinit_decode_instruction(r, size, pc, v) || v->length > s->pc + s->length - pc) return -1;
            for (u32 j = 0; j < GF_ARRAY(effect_spans); j++) if (effect_spans[j].pc == pc) {
                v->effect_first = effect_spans[j].first; v->effect_count = effect_spans[j].count;
            }
            if (v->opcode == GF_INIT_REPEAT) {
                if (depth == GF_INIT_REPEAT_DEPTH) return -1;
                stack[depth++] = id;
            }
            if (v->opcode == GF_INIT_END_REPEAT) {
                if (!depth) return -1;
                d->insns[stack[--depth]].end_repeat = pc;
            }
            if (v->opcode == GF_INIT_DONE && pc + 1 != s->pc + s->length) return -1;
            pc += v->length;
        }
        if (depth || r[pc - 1] != GF_INIT_DONE) return -1;
    }
    if (d->instruction_count != GF_INIT_INSNS) return -1;
    u32 crc = CRC32_INIT;
    crc = crc32_core_update(crc, r, size);
    d->plan.rom_crc = crc32_core_final(crc);
    for (u32 main = 0; main < GF_INIT_MAINS; main++) {
        main_start[main] = d->schedule_count;
        for (u32 i = 0; i < GF_ARRAY(scripts); i++) if (scripts[i].id == main)
            if (schedule_block(d, scripts[i].pc, scripts[i].pc + scripts[i].length - 1,
                               d->plan.ctx_count++, main, 0, 0, active)) return -1;
    }
    for (u32 i = 0; i < d->schedule_count; i++) {
        d->reachable[i] = 0;
        for (u32 j = 0; j < GF_STATE_COUNT; j++) d->heads[i][j] = RECOVER_NONE;
    }
    for (u32 main = 0; main < GF_INIT_MAINS; main++) {
        u32 end = main == GF_INIT_MAINS - 1 ? d->schedule_count : main_start[main + 1];
        d->reachable[main_start[main]] = 1;
        d->entries[main] = (struct recover_plan_entry){REC_RUN, main, d->plan.node_count};
        for (u32 step = main_start[main]; step < end; step++) {
            for (u32 state = 0; state < GF_STATE_COUNT; state++) if (d->reachable[step] & (1U << state)) {
                d->heads[step][state] = d->plan.node_count;
                d->error_pc = d->insns[d->schedule[step].instruction].pc;
                if (expand_step(d, r, p, step, state)) return -1;
            }
        }
        d->main_nodes[main] = d->plan.node_count - d->entries[main].first;
    }
    for (u32 i = 0; i < d->plan.node_count; i++) for (u32 edge = 0; edge < 2; edge++) {
        u32 dest = d->nodes[i].next[edge];
        if (dest & GF_PENDING) {
            u32 step = (dest & ~GF_PENDING) / GF_STATE_COUNT, state = dest & (GF_STATE_COUNT - 1);
            if (step == d->schedule_count) dest = RECOVER_NONE;
            else if (step > d->schedule_count) return -1;
            else {
                if (d->schedule[step].main_id != d->nodes[i].main_id) state = 0;
                dest = d->heads[step][state];
            }
            d->nodes[i].next[edge] = dest;
        }
        if (dest != RECOVER_NONE && (dest <= i || dest >= d->plan.node_count)) return -1;
    }
    /* Longest path, not sum of mutually exclusive branches. Schedule scratch
     * is no longer live; reuse it for step/wait costs to avoid another arena. */
    for (u32 i = d->plan.node_count; i-- > 0;) {
        const struct recover_plan_node *n = &d->nodes[i];
        u32 max_steps = 0, max_wait = 0;
        for (u32 edge = 0; edge < 2; edge++) {
            u32 next = n->next[edge];
            if (next == RECOVER_NONE || d->nodes[next].main_id != n->main_id) continue;
            if (d->schedule[next].k > max_steps) max_steps = d->schedule[next].k;
            if (d->schedule[next].count > max_wait) max_wait = d->schedule[next].count;
        }
        u32 delay = (n->opcode == GF_INIT_TIME || n->opcode == GF_INIT_RESET) && n->substep == GF_INIT_C ?
            d->args[n->arg_first].value : 0;
        u32 cost = 1;
        for (u32 j = 0; j < n->arg_count; j++)
            if (d->args[n->arg_first+j].kind == REC_ARG_READ) cost++;
        if (cost > GF_INIT_SCHEDULE - max_steps || delay > GF_INIT_WAIT_MAX - max_wait) return -1;
        d->schedule[i].k = max_steps + cost; d->schedule[i].count = max_wait + delay;
    }
    for (u32 main = 0; main < GF_INIT_MAINS; main++) {
        d->main_steps[main] = d->schedule[d->entries[main].first].k;
        d->main_wait[main] = d->schedule[d->entries[main].first].count;
    }
    return 0;
}
