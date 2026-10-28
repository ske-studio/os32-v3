/* NV44A init DATA decoder. S2_LAYOUT.final (G1-G3), DESIGN.final §4-6.
 * Semantics checked against MIT nouveau c03c1a1 bios/init.c handlers;
 * no x86 instructions, hardware access, or vendor ROM bytes here. */
#include "gfinit_decode.h"
#include "gfinit_pll.h"
#include "../lib/crc32_core.inc"
#define GF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))
STATIC_ASSERT(GF_INIT_WORK_STEPS >= RECOVER_NODE_MAX, gf_schedule_scratch);
STATIC_ASSERT(GF_INIT_FIXED_CAPTURES <= RECOVER_CAPTURE_MAX, gf_fixed_captures);
struct gf_script_span { u32 id, pc, length, crc; };
static const struct gf_script_span scripts[] = {
    {0x0U, 0xdeb0U, 0x2e5U, 0xf8ece062U},
    {0x1U, 0xe195U, 0x48eU, 0x3cfd0867U},
    {0x2U, 0xe623U, 0x105U, 0x22a2d835U},
    {0x3U, 0xe780U, 0xb2U, 0x1600df6aU},
    {0xffU, 0xd336U, 0x3aU, 0x4d3305dcU},
    {0x4U, 0xe832U, 0x1U, 0xf500ae27U},
    {0x7U, 0xe754U, 0x2cU, 0x3e2e05c4U},
    {0x6U, 0xe728U, 0x2cU, 0x17baf815U},
    {0x8U, 0x5df2U, 0xb2U, 0x71b7e46cU},
};
STATIC_ASSERT(GF_ARRAY(scripts) == GF_INIT_SCRIPTS, gf_script_count);
struct gf_effect_span { u32 pc, first, count; };
static const struct gf_effect_span effect_spans[] = {
    {0xdeb0U, 0U, 1U},
    {0xdebdU, 1U, 1U},
    {0xdecaU, 2U, 1U},
    {0xded7U, 3U, 1U},
    {0xdee4U, 4U, 1U},
    {0xdef1U, 5U, 1U},
    {0xdefeU, 6U, 2U},
    {0xdf01U, 8U, 2U},
    {0xdf07U, 10U, 2U},
    {0xdf0aU, 12U, 2U},
    {0xdf10U, 14U, 5U},
    {0xdf1dU, 19U, 1U},
    {0xdf2aU, 20U, 1U},
    {0xdf37U, 21U, 1U},
    {0xdf40U, 22U, 1U},
    {0xdf4dU, 23U, 1U},
    {0xdf56U, 24U, 1U},
    {0xdf5fU, 25U, 1U},
    {0xdf68U, 0U, 0U},
    {0xdf6bU, 26U, 1U},
    {0xdf74U, 27U, 1U},
    {0xdf7dU, 28U, 1U},
    {0xdf86U, 29U, 1U},
    {0xdf8fU, 30U, 1U},
    {0xdf98U, 31U, 1U},
    {0xdfa1U, 0U, 0U},
    {0xdfa3U, 32U, 2U},
    {0xdfa8U, 0U, 0U},
    {0xdfa9U, 34U, 2U},
    {0xdfaeU, 0U, 0U},
    {0xdfafU, 36U, 2U},
    {0xdfbaU, 38U, 1U},
    {0xdfc3U, 39U, 1U},
    {0xdfd0U, 40U, 1U},
    {0xdfd9U, 41U, 1U},
    {0xdfe6U, 42U, 1U},
    {0xdfefU, 43U, 1U},
    {0xdff8U, 44U, 1U},
    {0xe005U, 45U, 1U},
    {0xe00eU, 46U, 1U},
    {0xe01bU, 47U, 1U},
    {0xe024U, 48U, 1U},
    {0xe02dU, 49U, 1U},
    {0xe036U, 50U, 1U},
    {0xe03fU, 0U, 0U},
    {0xe041U, 0U, 0U},
    {0xe043U, 51U, 1U},
    {0xe04cU, 52U, 1U},
    {0xe055U, 53U, 1U},
    {0xe05eU, 54U, 1U},
    {0xe067U, 55U, 9U},
    {0xe092U, 64U, 9U},
    {0xe0bdU, 73U, 9U},
    {0xe0e8U, 82U, 9U},
    {0xe113U, 91U, 9U},
    {0xe13eU, 100U, 9U},
    {0xe169U, 109U, 9U},
    {0xe194U, 0U, 0U},
    {0xe195U, 118U, 9U},
    {0xe1c0U, 127U, 9U},
    {0xe1ebU, 136U, 9U},
    {0xe216U, 145U, 9U},
    {0xe241U, 154U, 1U},
    {0xe24aU, 155U, 1U},
    {0xe253U, 156U, 1U},
    {0xe25cU, 157U, 1U},
    {0xe265U, 158U, 1U},
    {0xe26eU, 159U, 9U},
    {0xe299U, 168U, 3U},
    {0xe2abU, 171U, 4U},
    {0xe2c1U, 175U, 9U},
    {0xe2ecU, 184U, 9U},
    {0xe317U, 193U, 1U},
    {0xe320U, 194U, 9U},
    {0xe34bU, 203U, 9U},
    {0xe376U, 212U, 9U},
    {0xe3a1U, 221U, 9U},
    {0xe3ccU, 230U, 9U},
    {0xe3f7U, 239U, 9U},
    {0xe422U, 248U, 9U},
    {0xe44dU, 0U, 0U},
    {0xe450U, 257U, 9U},
    {0xe47bU, 0U, 0U},
    {0xe47eU, 266U, 1U},
    {0xe480U, 0U, 0U},
    {0xe483U, 0U, 0U},
    {0xe485U, 267U, 9U},
    {0xe4b0U, 0U, 0U},
    {0xe4b1U, 276U, 9U},
    {0xe4dcU, 0U, 0U},
    {0xe4ddU, 0U, 0U},
    {0xe4e0U, 285U, 9U},
    {0xe50bU, 294U, 9U},
    {0xe536U, 303U, 1U},
    {0xe53fU, 0U, 0U},
    {0xe542U, 304U, 9U},
    {0xe56dU, 0U, 0U},
    {0xe570U, 313U, 9U},
    {0xe59bU, 0U, 0U},
    {0xe59eU, 322U, 1U},
    {0xe5a0U, 323U, 1U},
    {0xe5a9U, 324U, 1U},
    {0xe5b2U, 0U, 0U},
    {0xe5b3U, 0U, 0U},
    {0xe5b6U, 325U, 1U},
    {0xe5b8U, 326U, 1U},
    {0xe5baU, 327U, 9U},
    {0xe5e5U, 336U, 9U},
    {0xe610U, 345U, 1U},
    {0xe619U, 346U, 1U},
    {0xe622U, 0U, 0U},
    {0xe623U, 347U, 5U},
    {0xe62cU, 352U, 1U},
    {0xe635U, 353U, 1U},
    {0xe63eU, 354U, 1U},
    {0xe64bU, 355U, 1U},
    {0xe658U, 356U, 1U},
    {0xe661U, 357U, 10U},
    {0xe66dU, 367U, 2U},
    {0xe671U, 369U, 12U},
    {0xe67fU, 381U, 30U},
    {0xe68bU, 411U, 2U},
    {0xe68eU, 413U, 2U},
    {0xe694U, 415U, 2U},
    {0xe69aU, 417U, 2U},
    {0xe69dU, 419U, 2U},
    {0xe6a0U, 421U, 2U},
    {0xe6a6U, 423U, 2U},
    {0xe6acU, 425U, 2U},
    {0xe6b0U, 427U, 14U},
    {0xe6c0U, 441U, 14U},
    {0xe6c8U, 455U, 2U},
    {0xe6cbU, 457U, 2U},
    {0xe6ceU, 459U, 2U},
    {0xe6d1U, 461U, 2U},
    {0xe6d4U, 463U, 1U},
    {0xe6e1U, 464U, 1U},
    {0xe6eaU, 465U, 1U},
    {0xe6f7U, 466U, 1U},
    {0xe704U, 467U, 1U},
    {0xe711U, 468U, 1U},
    {0xe71eU, 469U, 1U},
    {0xe727U, 0U, 0U},
    {0xe780U, 0U, 0U},
    {0xe783U, 470U, 9U},
    {0xe7aeU, 479U, 9U},
    {0xe7d9U, 0U, 0U},
    {0xe7dcU, 488U, 1U},
    {0xe7deU, 0U, 0U},
    {0xe7e0U, 489U, 1U},
    {0xe7e2U, 0U, 0U},
    {0xe7e3U, 0U, 0U},
    {0xe7e5U, 0U, 0U},
    {0xe7e8U, 490U, 1U},
    {0xe7f1U, 491U, 1U},
    {0xe7faU, 0U, 0U},
    {0xe7fbU, 492U, 1U},
    {0xe811U, 493U, 1U},
    {0xe81eU, 494U, 2U},
    {0xe824U, 496U, 1U},
    {0xe831U, 0U, 0U},
    {0xd336U, 497U, 1U},
    {0xd33fU, 498U, 4U},
    {0xd355U, 502U, 1U},
    {0xd362U, 503U, 1U},
    {0xd36fU, 0U, 0U},
    {0xe832U, 0U, 0U},
    {0xe754U, 504U, 49U},
    {0xe77fU, 0U, 0U},
    {0xe728U, 553U, 49U},
    {0xe753U, 0U, 0U},
    {0x5df2U, 602U, 2U},
    {0x5df8U, 604U, 1U},
    {0x5dfaU, 605U, 1U},
    {0x5e07U, 606U, 1U},
    {0x5e14U, 607U, 1U},
    {0x5e21U, 608U, 1U},
    {0x5e2eU, 0U, 0U},
    {0x5e31U, 0U, 0U},
    {0x5e32U, 609U, 1U},
    {0x5e34U, 610U, 1U},
    {0x5e41U, 611U, 1U},
    {0x5e4eU, 612U, 1U},
    {0x5e5bU, 613U, 1U},
    {0x5e68U, 0U, 0U},
    {0x5e6bU, 0U, 0U},
    {0x5e6cU, 614U, 1U},
    {0x5e6eU, 615U, 1U},
    {0x5e7bU, 616U, 1U},
    {0x5e88U, 617U, 1U},
    {0x5e95U, 618U, 1U},
    {0x5ea2U, 0U, 0U},
    {0x5ea3U, 0U, 0U},
};
static int span(u32 size, u32 at, u32 len)
{ return at <= size && len <= size - at ? 0 : -1; }
static u32 le(const u8 *r, u32 at, u32 n)
{
    u32 v = 0;
    for (u32 i = 0; i < n; i++) v |= (u32)r[at + i] << (8 * i);
    return v;
}
static int shift_ok(u32 s)
{ return (s < GF_SHIFT_LEFT ? s : GF_SHIFT_WRAP - s) < GF_SHIFT_BITS; }
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
    case GF_INIT_IO_RESTRICT_PROG: case GF_INIT_IO_RESTRICT_PLL2:
        o->port = le(r, pc + 1, 2); o->index = r[pc + 3];
        o->mask = r[pc + 4]; o->shift = r[pc + 5];
        o->target = le(r, pc + 7, 4); o->ref = pc + 11;
        if (o->shift >= GF_SHIFT_BITS || !o->count || (o->mask >> o->shift) >= o->count) return -1;
        break;
    case GF_INIT_REPEAT:
        o->count = r[pc + 1];
        if (!o->count || o->count >= 255) return -1;
        break;
    case GF_INIT_COPY: case GF_INIT_COPY_NV_REG:
        o->ref = le(r, pc + 1, 4); o->shift = r[pc + 5];
        if (!shift_ok(o->shift) || o->ref & 3U) return -1;
        if (o->opcode == GF_INIT_COPY) {
            o->mask = r[pc + 10]; o->value = r[pc + 6];
            o->port = le(r, pc + 7, 2); o->index = r[pc + 9];
        } else { o->target = le(r, pc + 14, 4); o->mask = le(r, pc + 18, 4); }
        break;
    case GF_INIT_IO_FLAG_CONDITION: case GF_INIT_CONDITION: case GF_INIT_MACRO: {
        u32 entry = r[pc + 1];
        u32 base = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_IO_FLAG_TABLE : o->opcode == GF_INIT_CONDITION ? GF_INIT_CONDITION_TABLE : GF_INIT_MACRO_TABLE;
        u32 width = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_IO_FLAG_ENTRY : o->opcode == GF_INIT_CONDITION ? GF_INIT_CONDITION_ENTRY : GF_INIT_MACRO_ENTRY;
        u32 count = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_IO_FLAG_COUNT : o->opcode == GF_INIT_CONDITION ? GF_INIT_CONDITION_COUNT : GF_INIT_MACRO_COUNT;
        if (entry >= count) return -1;
        o->ref = base + entry * width; o->index = entry;
        if (span(size, o->ref, width)) return -1;
        if (o->opcode == GF_INIT_IO_FLAG_CONDITION) {
            o->port = le(r, o->ref, 2); o->index = r[o->ref + GF_INIT_IO_FLAG_INDEX];
            o->mask = r[o->ref + GF_INIT_IO_FLAG_REG_MASK]; o->shift = r[o->ref + GF_INIT_IO_FLAG_SHIFT];
            if (o->shift >= GF_SHIFT_BITS || span(size, le(r, o->ref + GF_INIT_IO_FLAG_DATA, 2),
                                      (o->mask >> o->shift) + 1)) return -1;
        } else {
            o->target = le(r, o->ref, 4);
            o->mask = le(r, o->ref + (o->opcode == GF_INIT_CONDITION ?
                GF_INIT_CONDITION_MASK : GF_INIT_MACRO_VALUE), 4);
            o->value = o->opcode == GF_INIT_CONDITION ? le(r, o->ref + GF_INIT_CONDITION_VALUE, 4) : o->mask;
        }
        break;
    }
    case GF_INIT_CR_INDEX_ADDRESS_LATCHED:
        o->index = r[pc + 1]; o->value = r[pc + 2]; o->ref = pc + 5;
        o->mask = r[pc + 3]; o->port = RECOVER_CRTC;
        if (!o->count || o->count - 1 > 255 - o->mask) return -1;
        break;
    case GF_INIT_CR: case GF_INIT_ZM_CR:
        o->port = RECOVER_CRTC; o->index = r[pc + 1];
        o->mask = o->opcode == GF_INIT_CR ? r[pc + 2] : 0;
        o->value = r[pc + len - 1]; break;
    case GF_INIT_ZM_REG_SEQUENCE:
        o->target = le(r, pc + 1, 4); o->ref = pc + 6;
        if (!o->count || o->count - 1 > (0xffffffffU - o->target) / 4) return -1;
        break;
    case GF_INIT_SUB_DIRECT: o->ref = le(r, pc + 1, 2); break;
    case GF_INIT_SUB:
        o->index = r[pc + 1];
        if (o->index != GF_INIT_SUB_FIRST && o->index != GF_INIT_SUB_FIRST + 1 && o->index != GF_INIT_SUB_LAST) return -1;
        if (span(size, GF_INIT_SCRIPT_TABLE + o->index * 2, 2)) return -1;
        o->ref = le(r, GF_INIT_SCRIPT_TABLE + o->index * 2, 2); break;
    case GF_INIT_ZM_INDEX_IO: case GF_INIT_INDEX_IO:
        o->port = le(r, pc + 1, 2); o->index = r[pc + 3];
        o->mask = o->opcode == GF_INIT_INDEX_IO ? r[pc + 4] : 0;
        o->value = r[pc + len - 1]; break;
    case GF_INIT_RESET: case GF_INIT_NV_REG:
        o->target = le(r, pc + 1, 4); o->mask = le(r, pc + 5, 4);
        o->value = le(r, pc + 9, 4); break;
    case GF_INIT_PLL2: case GF_INIT_ZM_REG:
        o->target = le(r, pc + 1, 4); o->value = le(r, pc + 5, 4);
        if (o->opcode == GF_INIT_ZM_REG && o->target == RECOVER_PMC_ENABLE) o->value |= RECOVER_PMC_ENABLE_BIT0;
        break;
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
static u32 instruction_at(const struct gfinit_decode *d, u32 pc)
{
    for (u32 i = 0; i < d->instruction_count; i++) if (d->insns[i].pc == pc) return i;
    return RECOVER_NONE;
}
static int schedule_add(struct gfinit_decode *d, u32 ins, u32 ctx, u32 main,
                        u32 kind, u32 k, u32 count)
{
    if (d->schedule_count == GF_INIT_WORK_STEPS) return -1;
    d->schedule[d->schedule_count++] = (struct gfinit_scheduled){ins, ctx, main, kind, k, count, 0};
    return 0;
}
/* kind 0 instruction, 1 call return, 2 repeat iteration, 3 repeat exit. */
static int schedule_block(struct gfinit_decode *d, u32 pc, u32 end, u32 ctx,
                          u32 main, u32 calls, u32 repeats, u32 *active)
{
    if (calls > GF_INIT_CALL_DEPTH || repeats > GF_INIT_REPEAT_DEPTH) return -1;
    if (calls > d->max_call) d->max_call = calls;
    if (repeats > d->max_repeat) d->max_repeat = repeats;
    for (;;) {
        u32 i = instruction_at(d, pc);
        if (i == RECOVER_NONE || active[i]) return -1;
        struct gfinit_instruction *v = &d->insns[i];
        u32 at = d->schedule_count;
        if (schedule_add(d, i, ctx, main, 0, 0, 0)) return -1;
        if (v->opcode == GF_INIT_DONE || v->opcode == GF_INIT_END_REPEAT) return v->pc == end ? 0 : -1;
        active[i] = 1;
        if (v->opcode == GF_INIT_SUB_DIRECT || v->opcode == GF_INIT_SUB) {
            u32 target_end = RECOVER_NONE;
            for (u32 j = 0; j < GF_ARRAY(scripts); j++)
                if (scripts[j].pc == v->ref && (v->opcode != GF_INIT_SUB || scripts[j].id == v->index)) target_end = scripts[j].pc + scripts[j].length - 1;
            if (target_end == RECOVER_NONE || d->plan.ctx_count == RECOVER_CTX_MAX ||
                schedule_block(d, v->ref, target_end, d->plan.ctx_count++, main, calls + 1, repeats, active)) return -1;
            if (schedule_add(d, i, ctx, main, 1, 0, 0)) return -1;
            d->schedule[at].jump = d->schedule_count;
        } else if (v->opcode == GF_INIT_REPEAT) {
            for (u32 k = 0; k < v->count; k++) {
                if (d->plan.ctx_count == RECOVER_CTX_MAX) return -1;
                u32 child = d->plan.ctx_count++;
                if (schedule_add(d, i, child, main, 2, k, v->count) ||
                    schedule_block(d, pc + v->length, v->end_repeat, child, main, calls, repeats + 1, active)) return -1;
                d->schedule[d->schedule_count - 1].k = k;
                d->schedule[d->schedule_count - 1].count = v->count;
                d->schedule[d->schedule_count - 1].jump = pc;
            }
            if (schedule_add(d, i, ctx, main, 3, 0, v->count)) return -1;
            pc = v->end_repeat + 1;
            active[i] = 0; continue;
        }
        active[i] = 0; pc += v->length;
    }
}
static u32 key_for(const struct recover_profile *p, u32 reg, u32 port, u32 index, u32 head)
{
    if (port == RECOVER_CRTC && index == RECOVER_CR44) head = 0;
    for (u32 i = 0; i < p->target_count; i++) {
        const struct recover_target *t = &p->targets[i];
        if (port ? t->space == REC_VGA_INDEXED && t->port == port && t->index == index && t->head == head :
                   t->space == REC_BAR_MMIO && !t->bar && t->offset == reg) return i;
    }
    return RECOVER_NONE;
}
#define GF_PENDING 0x80000000U
static u32 destination(u32 step, u32 state) { return GF_PENDING | (step * GF_STATE_COUNT + state); }
static u32 emit(struct gfinit_decode *d, const struct recover_profile *p,
                const struct gfinit_scheduled *s, u32 state, u32 sub, u32 dir,
                const struct recover_plan_arg *a, u32 argc)
{
    if (d->plan.node_count >= RECOVER_NODE_MAX || argc > GF_INIT_ARGS - d->plan.arg_count) {
        d->error = 1; return RECOVER_NONE;
    }
    const struct gfinit_instruction *v = &d->insns[s->instruction];
    u32 id = d->plan.node_count++;
    struct recover_plan_node *n = &d->nodes[id];
    *n = (struct recover_plan_node){.phase = REC_RUN, .main_id = s->main_id,
        .pc = v->pc, .opcode = v->opcode, .substep = sub, .ctx = s->ctx,
        .head = GF_STATE_HEAD(state), .skip = state & GF_STATE_SKIP, .rule_id = RECOVER_NONE,
        .expr_id = RECOVER_NONE, .arg_first = d->plan.arg_count, .arg_count = argc,
        .next = {id + 1, id + 1}};
    for (u32 j = 0; j < argc; j++) d->args[d->plan.arg_count++] = a[j];
    if (dir != RECOVER_NONE && dir != REC_READ) {
        for (u32 j = 0; j < p->rule_count; j++) {
            const struct recover_rule *w = &p->rules[j];
            if (w->phase == REC_RUN && w->main_id == s->main_id && w->pc == v->pc && w->substep == sub) {
                if (n->rule_id != RECOVER_NONE || !argc || w->key != a[0].value) d->error = 2;
                n->rule_id = w->id; n->expr_id = w->value.expr_id;
            }
        }
    }
    if (dir != RECOVER_NONE && dir != REC_READ && n->rule_id == RECOVER_NONE)
        d->plan.unresolved_count++;
    return id;
}
#define IMM(v) ((struct recover_plan_arg){REC_IMM, (v)})
#define READ(v) ((struct recover_plan_arg){REC_ARG_READ, (v)})
#define SAVED(v) ((struct recover_plan_arg){REC_ARG_SAVED, (v)})
static u32 control(struct gfinit_decode *d, const struct recover_profile *p,
                   const struct gfinit_scheduled *s, u32 state, u32 sub,
                   u32 a, u32 b, u32 c, u32 count)
{
    struct recover_plan_arg args[] = {IMM(a), IMM(b), IMM(c)};
    return emit(d, p, s, state, sub, RECOVER_NONE, args, count);
}
static u32 write_node(struct gfinit_decode *d, const struct recover_profile *p,
                      const struct gfinit_scheduled *s, u32 state, u32 e,
                      u32 dir, u32 key, u32 mask, u32 value, bool rmw)
{
    struct recover_plan_arg a[] = {IMM(key), rmw ? READ(key) : IMM(value), IMM(mask), IMM(value)};
    if (key == RECOVER_NONE) d->error = 3;
    return emit(d, p, s, state, e, dir, a, rmw ? 4 : 2);
}
static u32 read_node(struct gfinit_decode *d, const struct recover_profile *p,
                     const struct gfinit_scheduled *s, u32 state, u32 key)
{
    struct recover_plan_arg a = IMM(key);
    if (key == RECOVER_NONE || !p->targets[key].read_safe) d->error = 4;
    return emit(d, p, s, state, GF_INIT_Q, REC_READ, &a, 1);
}
/* Solver runs before any bus access. All runtime values come from checked
 * captures and every write uses the recover-owned rule for this pc/substep. */
static int expand_pll(struct gfinit_decode *d, const u8 *r, const struct recover_profile *p,
                      const struct gfinit_scheduled *s, u32 state, u32 khz)
{
    const struct gfinit_instruction *v = &d->insns[s->instruction];
    struct gfinit_pll_limits limits;
    struct gfinit_pll_solution solution;
    struct gfinit_pll_sequence q;
    if (gfinit_pll_parse(r, d->plan.rom_size, GF_INIT_PLL_TABLE, v->target, &limits) ||
        gfinit_pll_solve(&limits, khz, &solution) ||
        gfinit_pll_sequence(v->target, &limits, &solution, &q)) return -1;
    u32 sources[GF_PLL_WRITES_MAX], producers[GF_PLL_WRITES_MAX], count = 0;
    for (u32 i = 0; i < q.count; i++) if (q.writes[i].source) {
        u32 source = q.writes[i].source, j = 0;
        for (; j < count; j++) if (sources[j] == source) break;
        if (j == count) {
            sources[count] = source;
            producers[count++] = read_node(d, p, s, state, key_for(p, source, 0, 0, 0));
        }
    }
    u32 base = GF_PLL_SUBSTEP + GF_PLL_SUBSTEP_STRIDE *
        (v->target == GF_PLL_CORE ? 0 : v->target == GF_PLL_MEMORY ? 1 : 2);
    for (u32 i = 0; i < q.count; i++) {
        const struct gfinit_pll_write *w = &q.writes[i];
        u32 key = key_for(p, w->reg, 0, 0, 0);
        if (key == RECOVER_NONE) return -1;
        if (!w->source) write_node(d, p, s, state, base + i, REC_WRITE, key, 0, w->value, false);
        else {
            u32 c = d->plan.capture_count, j = 0;
            if (c == RECOVER_CAPTURE_MAX) return -1;
            for (; j < count; j++) if (sources[j] == w->source) break;
            if (j == count) return -1;
            struct recover_plan_arg a[] = {IMM(key), SAVED(p->count + c), IMM(w->keep), IMM(w->value)};
            u32 consumer = emit(d, p, s, state, base + i, REC_WRITE, a, 4);
            d->captures[c] = (struct recover_capture){p->count + c, producers[j], consumer,
                key_for(p, w->source, 0, 0, 0), 0};
            d->plan.capture_count++;
        }
    }
#if GF_VPLL_PROBE
    if (s->main_id == RECOVER_VPLL_MAIN && v->pc == RECOVER_VPLL_PC && v->target == GF_PLL_VIDEO) {
        u32 switch_key = key_for(p, RECOVER_VPLL_SWITCH_OFF, 0, 0, 0);
        if (switch_key == RECOVER_NONE || q.count != 5) return -1;
        write_node(d, p, s, state, RECOVER_VPLL_STAGE2_FIRST, REC_WRITE, switch_key, 0, RECOVER_VPLL_SWITCH_ON, false);
        for (u32 i = 0; i < q.count; i++) {
            const struct gfinit_pll_write *w = &q.writes[i];
            u32 c = d->plan.capture_count, j = 0;
            if (!w->source || c == RECOVER_CAPTURE_MAX) return -1;
            for (; j < count; j++) if (sources[j] == w->source) break;
            if (j == count) return -1;
            u32 key = key_for(p, w->reg, 0, 0, 0);
            struct recover_plan_arg a[] = {IMM(key), SAVED(p->count + c), IMM(w->keep), IMM(w->value)};
            u32 consumer = emit(d, p, s, state, RECOVER_VPLL_REPEAT_FIRST + i, REC_WRITE, a, 4);
            d->captures[c] = (struct recover_capture){p->count + c, producers[j], consumer,
                key_for(p, w->source, 0, 0, 0), 0};
            d->plan.capture_count++;
        }
        write_node(d, p, s, state, RECOVER_VPLL_STAGE2_LAST, REC_WRITE, switch_key, 0, RECOVER_VPLL_SWITCH_DEFAULT, false);
    }
#endif
    return d->error ? -1 : 0;
}
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
    case GF_INIT_CONDITION: case GF_INIT_IO_FLAG_CONDITION: {
        if (op == GF_INIT_IO_FLAG_CONDITION) write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        read_node(d, p, s, state, t);
        u32 branch;
        if (op == GF_INIT_CONDITION) branch = control(d, p, s, state, GF_INIT_C, r[pc+1], v->mask, v->value, 3);
        else {
            struct recover_plan_arg a[] = {IMM(r[pc+1]), IMM(v->mask), IMM(v->shift),
                IMM(le(r, v->ref+GF_INIT_IO_FLAG_DATA, 2)), IMM(r[v->ref+GF_INIT_IO_FLAG_MASK]), IMM(r[v->ref+GF_INIT_IO_FLAG_VALUE])};
            branch = emit(d, p, s, state, GF_INIT_C, RECOVER_NONE, a, 6);
        }
        if (branch == RECOVER_NONE) return -1;
        d->nodes[branch].next[0] = destination(next_step, state|GF_STATE_SKIP);
        d->nodes[branch].next[1] = destination(next_step, state);
        d->reachable[next_step] |= (1U << (state|GF_STATE_SKIP)) | (1U << state);
        return d->error ? -1 : 0;
    }
    case GF_INIT_IO_RESTRICT_PROG: case GF_INIT_IO_RESTRICT_PLL2: {
        u32 src = key_for(p, 0, v->port, v->index, head);
        write_node(d, p, s, state, e++, REC_SELECT, src, 0, v->index, false);
        read_node(d, p, s, state, src);
        u32 branches[GF_INIT_COUNT_MAX], ends[GF_INIT_COUNT_MAX];
        for (u32 k = 0; k < v->count; k++) {
            struct recover_plan_arg a[] = {IMM(v->mask), IMM(v->shift), IMM(k), IMM(v->count)};
            branches[k] = emit(d, p, s, state, GF_INIT_C+k, RECOVER_NONE, a, 4);
            if (branches[k] == RECOVER_NONE) return -1;
        }
        for (u32 k = 0; k < v->count; k++) {
            d->nodes[branches[k]].next[0] = k+1 < v->count ? branches[k+1] : destination(next_step, state);
            if (op == GF_INIT_IO_RESTRICT_PLL2 && k && le(r, v->ref+4*k, 4) == le(r, v->ref, 4)) {
                d->nodes[branches[k]].next[1] = ends[0]; continue;
            }
            ends[k] = d->plan.node_count;
            d->nodes[branches[k]].next[1] = ends[k];
            if (op == GF_INIT_IO_RESTRICT_PROG) write_node(d, p, s, state, e+k, REC_WRITE, key_for(p, v->target, 0, 0, head), 0, le(r, v->ref+4*k, 4), false);
            else if (expand_pll(d, r, p, s, state, le(r, v->ref+4*k, 4))) return -1;
            if (d->error) return -1;
            d->nodes[d->plan.node_count-1].next[0] = d->nodes[d->plan.node_count-1].next[1] = destination(next_step, state);
        }
        d->reachable[next_step] |= 1U << state;
        return d->error ? -1 : 0;
    }
    case GF_INIT_PLL2: if (expand_pll(d, r, p, s, state, v->value)) return -1; break;
    case GF_INIT_NV_REG: write_node(d, p, s, state, e, REC_WRITE, t, v->mask, v->value, true); break;
    case GF_INIT_ZM_REG: case GF_INIT_MACRO: write_node(d, p, s, state, e, REC_WRITE, t, 0, v->value, false); break;
    case GF_INIT_CR: case GF_INIT_ZM_CR: case GF_INIT_ZM_INDEX_IO: case GF_INIT_INDEX_IO:
        write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        write_node(d, p, s, state, e, REC_DATA, t, v->mask, v->value, op == GF_INIT_CR || op == GF_INIT_INDEX_IO);
        if (v->port == RECOVER_CRTC && v->index == RECOVER_CR44) next_state = (v->value == RECOVER_CR44_HEAD1 ? GF_STATE_HEAD1 : 0) | (state&GF_STATE_SKIP);
        break;
    case GF_INIT_ZM_CR_GROUP:
        for (u32 k = 0; k < v->count; k++) {
            u32 index = r[pc+2+2*k], value = r[pc+3+2*k];
            t = key_for(p, 0, RECOVER_CRTC, index, GF_STATE_HEAD(next_state));
            write_node(d, p, s, next_state, e++, REC_SELECT, t, 0, index, false);
            write_node(d, p, s, next_state, e++, REC_DATA, t, 0, value, false);
            if (index == RECOVER_CR44) next_state = (value == RECOVER_CR44_HEAD1 ? GF_STATE_HEAD1 : 0)|(state&GF_STATE_SKIP);
        }
        break;
    case GF_INIT_ZM_REG_SEQUENCE:
        for (u32 k = 0; k < v->count; k++) write_node(d, p, s, state, e++, REC_WRITE,
            key_for(p, v->target+4*k, 0, 0, head), 0, le(r, v->ref+4*k, 4), false);
        break;
    case GF_INIT_COPY: case GF_INIT_COPY_NV_REG: {
        if (op == GF_INIT_COPY) write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        u32 src = key_for(p, v->ref, 0, 0, head);
        struct recover_plan_arg a[] = {IMM(t), READ(op == GF_INIT_COPY ? t : src), READ(op == GF_INIT_COPY ? src : t),
            IMM(v->shift), IMM(op == GF_INIT_COPY ? v->value : le(r, pc+6, 4)),
            IMM(op == GF_INIT_COPY ? v->mask : le(r, pc+10, 4)), IMM(v->mask)};
        if (src == RECOVER_NONE) return -1;
        emit(d, p, s, state, e, op == GF_INIT_COPY ? REC_DATA : REC_WRITE, a, op == GF_INIT_COPY ? 6 : 7); break;
    }
    case GF_INIT_RESET: {
        u32 saved = key_for(p, RECOVER_PBUS184C, 0, 0, head), producer = d->plan.node_count;
        write_node(d, p, s, state, e++, REC_WRITE, saved, GF_INIT_RESET_KEEP, 0, true);
        write_node(d, p, s, state, e++, REC_WRITE, t, 0, v->mask, false);
        control(d, p, s, state, GF_INIT_C, GF_INIT_RESET_US, 0, 0, 1);
        write_node(d, p, s, state, e++, REC_WRITE, t, 0, v->value, false);
        struct recover_plan_arg a[] = {IMM(saved), SAVED(p->count)};
        u32 consumer = emit(d, p, s, state, e++, REC_WRITE, a, 2);
        d->captures[0] = (struct recover_capture){p->count, producer, consumer, saved, 0};
        write_node(d, p, s, state, e, REC_WRITE, key_for(p, RECOVER_PBUS1850, 0, 0, head), 0, 0, false); break;
    }
    case GF_INIT_CR_INDEX_ADDRESS_LATCHED: {
        write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        u32 producer = read_node(d, p, s, state, t);
        u32 other = key_for(p, 0, RECOVER_CRTC, v->value, head);
        for (u32 k = 0; k < v->count; k++) {
            if (k) write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
            write_node(d, p, s, state, e++, REC_DATA, t, 0, v->mask+k, false);
            write_node(d, p, s, state, e++, REC_SELECT, other, 0, v->value, false);
            write_node(d, p, s, state, e++, REC_DATA, other, 0, r[v->ref+k], false);
        }
        write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        struct recover_plan_arg a[] = {IMM(t), SAVED(p->count+1+head)};
        u32 consumer = emit(d, p, s, state, e, REC_DATA, a, 2);
        d->captures[1+head] = (struct recover_capture){p->count+1+head, producer, consumer, t, 0}; break;
    }
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
