/* NV44A 5.44.a2.10.00 recover-owned write rules. No ROM/code bytes.
 * Evidence: DESIGN.final sections 3-6; fixture gf_effects ordinal in comments.
 * NVIDIA xf86-video-nv 8234bce nv_hw.c:33-48 (locks);
 * nouveau c03c1a1 MIT bios/init.c handlers (RMW read safety only);
 * envytools f102b82 display/nv3_prmvio.xml:11-15, nv3_pcrtc.xml:23-30.
 * Literal/trigger data has no speculative readback; PLL/PFB is never SAVED.
 * S2 real opcode layouts follow the fixed expressions. Budgets/CRC are set by owner.
 */
#include "recover_gf.h"
#include "recover_gf_expand.h"
#include "gfinit_plan.h"
#include "gfinit_pll.h"
static const struct recover_guard n1_guards[] = {
    {REC_GF_T_CFG50, 0xffffffffU, 0, REC_BEFORE | REC_AFTER},
    {REC_GF_T_CFG54, 0xffffffffU, 0, REC_BEFORE | REC_AFTER},
    {REC_GF_T_PBUS1850, 0xffffffffU, 0, REC_BEFORE | REC_AFTER},
    {REC_GF_T_PBUS1854, 0xffffffffU, 0, REC_BEFORE | REC_AFTER},
};
static const u32 dep_1[] = {REC_GF_R_PREPARE_PBUS1854};
static const u32 dep_2[] = {REC_GF_R_PREPARE_PBUS1850};
static const u32 dep_3[] = {REC_GF_R_PREPARE_PMC_IRQ};
static const u32 dep_12[] = {REC_GF_R_SIZE_COMMAND_OFF};
static const u32 dep_13[] = {REC_GF_R_SIZE_PROBE_BAR0};
static const u32 dep_14[] = {REC_GF_R_SIZE_RESTORE_BAR0};
static const u32 dep_15[] = {REC_GF_R_SIZE_PROBE_BAR1};
static const u32 dep_16[] = {REC_GF_R_SIZE_RESTORE_BAR1};
static const u32 dep_17[] = {REC_GF_R_SIZE_PROBE_BAR2};
static const u32 dep_18[] = {REC_GF_R_SIZE_RESTORE_BAR2};
static const u32 dep_21[] = {REC_GF_R_PREPARE_PMC_ENABLE};
static const u32 dep_22[] = {REC_GF_R_PREPARE_CR44_SELECT};
static const u32 dep_25[] = {REC_GF_R_PREPARE_CR44_DATA};
static const u32 dep_26[] = {REC_GF_R_PREPARE_CR1F_SELECT};
static const u32 dep_29[] = {REC_GF_R_PREPARE_CR1F_DATA};
static const u32 dep_30[] = {REC_GF_R_PREPARE_CR11_SELECT};
static const u32 dep_37[] = {36};
static const u32 dep_39[] = {38};
static const u32 dep_41[] = {40};
static const u32 dep_43[] = {42};
static const u32 dep_62[] = {61};
static const u32 dep_64[] = {63};
static const u32 dep_381[] = {380};
static const u32 dep_383[] = {382};
static const u32 dep_385[] = {384};
static const u32 dep_387[] = {386};
static const u32 dep_389[] = {388};
static const u32 dep_391[] = {390};
static const u32 dep_393[] = {392};
static const u32 dep_395[] = {394};
static const u32 dep_397[] = {396};
static const u32 dep_399[] = {398};
static const u32 dep_401[] = {400};
static const u32 dep_403[] = {402};
static const u32 dep_405[] = {404};
static const u32 dep_407[] = {406};
static const u32 dep_409[] = {408};
static const u32 dep_411[] = {410};
static const u32 dep_413[] = {412};
static const u32 dep_415[] = {414};
static const u32 dep_417[] = {416};
static const u32 dep_419[] = {418};
static const u32 dep_421[] = {420};
static const u32 dep_423[] = {422};
static const u32 dep_425[] = {424};
static const u32 dep_427[] = {426};
static const u32 dep_429[] = {428};
static const u32 dep_431[] = {430};
static const u32 dep_434[] = {433};
static const u32 dep_436[] = {435};
static const u32 dep_438[] = {437};
static const u32 dep_440[] = {439};
static const u32 dep_442[] = {441};
static const u32 dep_444[] = {443};
static const u32 dep_446[] = {445};
static const u32 dep_448[] = {447};
static const u32 dep_450[] = {449};
static const u32 dep_452[] = {451};
static const u32 dep_454[] = {453};
static const u32 dep_456[] = {455};
static const u32 dep_458[] = {457};
static const u32 dep_460[] = {459};
static const u32 dep_462[] = {461};
static const u32 dep_464[] = {463};
static const u32 dep_466[] = {465};
static const u32 dep_468[] = {467};
static const u32 dep_470[] = {469};
static const u32 dep_472[] = {471};
static const u32 dep_474[] = {473};
static const u32 dep_477[] = {476};
static const u32 dep_479[] = {478};
static const u32 dep_481[] = {480};
static const u32 dep_483[] = {482};
static const u32 dep_515[] = {514, 0, 1};
static const u32 dep_527[] = {526};
static const u32 pll_dep_545[] = {544};
static const u32 pll_dep_546[] = {545};
static const u32 pll_dep_547[] = {546};
static const u32 pll_dep_548[] = {547};
static const u32 pll_dep_549[] = {548};
static const u32 pll_dep_551[] = {550};
static const u32 pll_dep_552[] = {551};
static const u32 pll_dep_553[] = {552};
static const u32 pll_dep_554[] = {553};
static const u32 pll_dep_555[] = {554};
static const u32 pll_dep_556[] = {555};
static const u32 pll_dep_557[] = {556};
static const u32 pll_dep_558[] = {557};
static const u32 pll_dep_559[] = {558};
static const u32 pll_dep_560[] = {559};
static const u32 pll_dep_561[] = {560};
static const u32 pll_dep_562[] = {561};
static const u32 pll_dep_563[] = {562};
static const u32 pll_dep_564[] = {563};
static const u32 pll_dep_566[] = {565};
static const u32 pll_dep_567[] = {566};
static const u32 pll_dep_568[] = {567};
static const u32 pll_dep_569[] = {568};
static const struct recover_reg regs[] = {
    {REC_GF_T_BAR0, 4, 0xffffffffU, 0xffffffffU, REC_RW, true, REC_GF_R_RESTORE_BAR0},
    {REC_GF_T_BAR1, 4, 0xffffffffU, 0xffffffffU, REC_RW, true, REC_GF_R_RESTORE_BAR1},
    {REC_GF_T_BAR2, 4, 0xffffffffU, 0xffffffffU, REC_RW, true, REC_GF_R_RESTORE_BAR2},
};
static const struct recover_write gf_quiesce[] = {
    {REC_GF_T_PBUS1850, 4, 0, 0xffffffffU, REC_GF_R_QUIESCE_PBUS1850},
    {REC_GF_T_PBUS1854, 4, 0, 0xffffffffU, REC_GF_R_QUIESCE_PBUS1854},
    {REC_GF_T_PMC_IRQ, 4, 0, 0xffffffffU, REC_GF_R_QUIESCE_PMC_IRQ},
};
static const u32 saved_kind[] = {REC_ARG_SAVED};
static int saved_bar(const u32 *a, u32 n, u32 *v)
{
    if (n != 1) return -1;
    *v = a[0]; return 0;
}
static const u32 read_kind[] = {REC_ARG_READ};
static int command_off(const u32 *a, u32 n, u32 *v)
{
    if (n != 1 || a[0] > 0xffffU) return -1;
    *v = a[0] & ~(PCI_CMD_IO_ENABLE | PCI_CMD_MEM_ENABLE | PCI_CMD_BUS_MASTER); return 0;
}
static int pmc_enable(const u32 *a, u32 n, u32 *v)
{
    if (n != 1) return -1;
    *v = a[0] | RECOVER_PMC_ENABLE_BIT0; return 0;
}
static int cr11_unlock(const u32 *a, u32 n, u32 *v)
{
    if (n != 1 || a[0] > 0xffU) return -1;
    *v = a[0] & ~RECOVER_CR11_PROTECT; return 0;
}
static const u32 rmw_kinds[] = {REC_ARG_READ, REC_IMM, REC_IMM};
static int gf_rmw(const u32 *a, u32 n, u32 *v)
{
    if (n != 3) return -1;
    *v = (a[0] & a[1]) | a[2]; return 0;
}
static const u32 copy_kinds[] = {REC_ARG_READ, REC_ARG_READ, REC_IMM, REC_IMM, REC_IMM};
static const u32 copy_nv_kinds[] = {REC_ARG_READ, REC_ARG_READ, REC_IMM, REC_IMM, REC_IMM, REC_IMM};
static int gf_shift(u32 value, u32 shift, u32 *result)
{
    if (shift > GF_SHIFT_WRAP - 1 || (shift < GF_SHIFT_LEFT ? shift : GF_SHIFT_WRAP - shift) >= GF_SHIFT_BITS) return -1;
    *result = shift < GF_SHIFT_LEFT ? value >> shift : value << (GF_SHIFT_WRAP - shift);
    return 0;
}
static int gf_copy(const u32 *a, u32 n, u32 *v)
{
    u32 shifted;
    if (n != 5 || gf_shift(a[1], a[2], &shifted)) return -1;
    *v = ((a[0] & a[4]) | (shifted & a[3])) & 255U; return 0;
}
static int gf_copy_nv(const u32 *a, u32 n, u32 *v)
{
    u32 shifted;
    if (n != 6 || gf_shift(a[0], a[2], &shifted)) return -1;
    *v = (a[1] & a[5]) | ((shifted & a[3]) ^ a[4]); return 0;
}
static const u32 pll_saved_kinds[] = {REC_ARG_SAVED, REC_IMM, REC_IMM};
static const struct recover_expr exprs[] = {
    {REC_GF_PLL_SAVED, 3, pll_saved_kinds, gf_rmw},
    {REC_GF_EXPR_COPY, 5, copy_kinds, gf_copy},
    {REC_GF_EXPR_COPY_NV, 6, copy_nv_kinds, gf_copy_nv},
    {REC_GF_RMW, 3, rmw_kinds, gf_rmw},
    {REC_GF_SAVED_BAR, 1, saved_kind, saved_bar},
    {REC_GF_COMMAND_OFF, 1, read_kind, command_off},
    {REC_GF_PMC_ENABLE, 1, read_kind, pmc_enable},
    {REC_GF_CR11_UNLOCK, 1, read_kind, cr11_unlock},
};
/* These are fixed-operation layouts, never the 27 ROM opcode layouts. */
static const u32 fixed_literal[] = {REC_IMM, REC_IMM};
static const u32 fixed_read[] = {REC_IMM};
static const u32 fixed_saved[] = {REC_IMM, REC_ARG_SAVED};
static const u32 fixed_rmw[] = {REC_IMM, REC_ARG_READ};
static const u32 rmw_layout[] = {REC_IMM, REC_ARG_READ, REC_IMM, REC_IMM};
static const u32 copy_layout[] = {REC_IMM, REC_ARG_READ, REC_ARG_READ, REC_IMM, REC_IMM, REC_IMM};
static const u32 copy_nv_layout[] = {REC_IMM, REC_ARG_READ, REC_ARG_READ, REC_IMM, REC_IMM, REC_IMM, REC_IMM};
static const u32 control_kinds[] = {REC_IMM, REC_IMM, REC_IMM, REC_IMM, REC_IMM, REC_IMM};
static const u32 pll_saved_layout[] = {REC_IMM, REC_ARG_SAVED, REC_IMM, REC_IMM};
/* Classified index ports only; gfinit_bus restores these after local locks.
 * This grants no COLD data restore. */
static const u32 index_saves[] = {REC_GF_T_CR44, REC_GF_T_SR01_HEAD1, REC_GF_T_SR01_HEAD0, REC_GF_T_CR21_HEAD1};
const struct recover_profile recover_gf_template = {
    .version = RECOVER_PROFILE_VERSION, .cold_required = true,
    .opcode_limit = REC_GF_OPCODE_TICKS, .max_polls = REC_GF_MAX_POLLS,
    .regs = regs, .count = sizeof(regs) / sizeof(regs[0]),
    .quiesce = gf_quiesce, .quiesce_count = sizeof(gf_quiesce) / sizeof(gf_quiesce[0]),
    .exprs = exprs, .expr_count = sizeof(exprs) / sizeof(exprs[0]),
    .vram_reserved = {0, REC_GF_VRAM_RESERVED},
    .index_save_targets = index_saves, .index_save_count = sizeof(index_saves) / sizeof(index_saves[0]),
};
/* Private storage types: logical/public layouts and their consumers stay intact. */
#define REC_TABLE_NONE8  0xffU
#define REC_TABLE_NONE16 0xffffU
struct recover_rule_small {
    u32 substep, write_mask, verify_mask, mask, bits, min, max;
    const u32 *allowed, *requires;
    const struct recover_guard *guards;
    u16 id, key, pc, allowed_count;
    u8 phase, main_id, mode, expr_id, flags, requires_count, guard_count, reserved;
};
struct recover_target_small {
    u32 offset, index;
    u16 port;
    u8 space, bar, head, width, access, read_safe;
};
struct recover_layout_range {
    u32 first, last;
    const u32 *kinds;
    u16 opcode, ordinal;
    u8 argc, dir_substep, flags, reserved;
};
#ifdef __i386__
STATIC_ASSERT(sizeof(struct recover_rule_small) == 56, gf_rule_storage_size);
STATIC_ASSERT(sizeof(struct recover_target_small) == 16, gf_target_storage_size);
STATIC_ASSERT(sizeof(struct recover_layout_range) == 20, gf_layout_storage_size);
STATIC_ASSERT(__alignof__(struct recover_rule_small) == 4, gf_rule_storage_align);
STATIC_ASSERT(__alignof__(struct recover_target_small) == 4, gf_target_storage_align);
STATIC_ASSERT(__alignof__(struct recover_layout_range) == 4, gf_layout_storage_align);
#endif
#include "generated/recover_gf_tables.inc"

/* Subtraction bounds the inclusive length without overflowing at UINT32_MAX. */
static int gf_layout_ranges_valid(const struct recover_layout_range *ranges,
                                  u32 range_count, u32 total, u32 capacity)
{
    u32 count = 0;
    if (total > capacity) return -1;
    for (u32 i = 0; i < range_count; i++) {
        const struct recover_layout_range *r = &ranges[i];
        if (r->ordinal != count || r->last < r->first || count >= total ||
            r->last - r->first >= total - count) return -1;
        count += r->last - r->first + 1;
    }
    return count == total ? 0 : -1;
}

/* Validate ranges before writes; publish the profile only after all expansion. */
int recover_gf_profile_expand(struct recover_profile *p, struct gfinit_plan *buf)
{
    struct recover_profile next = recover_gf_template;
    u32 count;
    if (!buf) { *p = next; return 0; }
    if (REC_GF_RULE_COUNT > GF_PLAN_RULES ||
        REC_GF_TARGET_COUNT > GF_PLAN_TARGETS ||
        gf_layout_ranges_valid(layout_ranges, REC_GF_RANGE_COUNT,
                               REC_GF_LAYOUT_COUNT, GF_PLAN_LAYOUTS)) return -1;
    for (u32 i = 0; i < REC_GF_RULE_COUNT; i++) {
        const struct recover_rule_small *s = &rules_small[i];
        struct recover_rule *d = &buf->rules[i];
        d->id = s->id; d->key = s->key; d->phase = s->phase;
        d->main_id = s->main_id == REC_TABLE_NONE8 ? RECOVER_NONE : s->main_id;
        d->pc = s->pc == REC_TABLE_NONE16 ? RECOVER_NONE : s->pc;
        d->substep = s->substep;
        d->write_mask = s->write_mask; d->verify_mask = s->verify_mask;
        d->value.mode = s->mode; d->value.mask = s->mask; d->value.bits = s->bits;
        d->value.min = s->min; d->value.max = s->max;
        d->value.expr_id = s->expr_id == REC_TABLE_NONE8 ? RECOVER_NONE : s->expr_id;
        d->value.allowed = s->allowed; d->value.allowed_count = s->allowed_count;
        d->requires = s->requires; d->requires_count = s->requires_count;
        d->guards = s->guards; d->guard_count = s->guard_count; d->flags = s->flags;
    }
    for (u32 i = 0; i < REC_GF_TARGET_COUNT; i++) {
        const struct recover_target_small *s = &targets_small[i];
        struct recover_target *d = &buf->targets[i];
        d->offset = s->offset; d->index = s->index; d->port = s->port;
        d->space = s->space; d->bar = s->bar; d->head = s->head;
        d->width = s->width; d->access = s->access; d->read_safe = s->read_safe;
    }
    count = 0;
    for (u32 i = 0; i < REC_GF_RANGE_COUNT; i++) {
        const struct recover_layout_range *s = &layout_ranges[i];
        for (u32 substep = s->first; ; substep++) {
            struct recover_opcode_layout *d = &buf->layouts[count++];
            d->opcode = s->opcode; d->substep = substep;
            d->argc = s->argc; d->kinds = s->kinds; d->flags = s->flags;
            d->dir_substep = s->dir_substep == REC_TABLE_NONE8 ? RECOVER_NONE : s->dir_substep;
            if (substep == s->last) break;
        }
    }
    next.rules = buf->rules; next.rule_count = REC_GF_RULE_COUNT;
    next.targets = buf->targets; next.target_count = REC_GF_TARGET_COUNT;
    next.layouts = buf->layouts; next.layout_count = REC_GF_LAYOUT_COUNT;
    *p = next;
    return 0;
}
const u32 recover_gf_prepare[] = {
    REC_GF_R_PREPARE_PBUS1854,
    REC_GF_R_PREPARE_PBUS1850,
    REC_GF_R_PREPARE_PMC_IRQ,
    REC_GF_R_PREPARE_PMC_ENABLE,
    REC_GF_R_PREPARE_CR44_SELECT,
    REC_GF_R_PREPARE_CR44_DATA,
    REC_GF_R_PREPARE_CR1F_SELECT,
    REC_GF_R_PREPARE_CR1F_DATA,
    REC_GF_R_PREPARE_CR11_SELECT,
    REC_GF_R_PREPARE_CR11_UNLOCK,
};
const u32 recover_gf_size_bars[] = {
    REC_GF_R_SIZE_COMMAND_OFF,
    REC_GF_R_SIZE_PROBE_BAR0,
    REC_GF_R_SIZE_RESTORE_BAR0,
    REC_GF_R_SIZE_PROBE_BAR1,
    REC_GF_R_SIZE_RESTORE_BAR1,
    REC_GF_R_SIZE_PROBE_BAR2,
    REC_GF_R_SIZE_RESTORE_BAR2,
    REC_GF_R_SIZE_RESTORE_COMMAND,
};

static const struct recover_plan_node size_bars_nodes[] = {
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_COMMAND_OFF, 0, 0, 0, 0, REC_GF_R_SIZE_COMMAND_OFF, 2, 0, 2, {1, 1}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_PROBE, 0, 0, 0, 0, REC_GF_R_SIZE_PROBE_BAR0, RECOVER_NONE, 2, 2, {2, 2}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_READ, 0, 0, 0, 0, RECOVER_NONE, RECOVER_NONE, 4, 1, {3, 3}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_RESTORE_BAR, 1, 0, 0, 0, REC_GF_R_SIZE_RESTORE_BAR0, 1, 5, 2, {4, 4}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_PROBE, 0, 0, 0, 0, REC_GF_R_SIZE_PROBE_BAR1, RECOVER_NONE, 7, 2, {5, 5}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_READ, 0, 0, 0, 0, RECOVER_NONE, RECOVER_NONE, 9, 1, {6, 6}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_RESTORE_BAR, 1, 0, 0, 0, REC_GF_R_SIZE_RESTORE_BAR1, 1, 10, 2, {7, 7}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_PROBE, 0, 0, 0, 0, REC_GF_R_SIZE_PROBE_BAR2, RECOVER_NONE, 12, 2, {8, 8}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_READ, 0, 0, 0, 0, RECOVER_NONE, RECOVER_NONE, 14, 1, {9, 9}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_RESTORE_BAR, 1, 0, 0, 0, REC_GF_R_SIZE_RESTORE_BAR2, 1, 15, 2, {10, 10}},
    {REC_SIZE_BARS, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SIZE_RESTORE_COMMAND, 1, 0, 0, 0, REC_GF_R_SIZE_RESTORE_COMMAND, RECOVER_NONE, 17, 2, {RECOVER_NONE, RECOVER_NONE}},
};
static const struct recover_plan_arg size_bars_args[] = {
    {REC_IMM, REC_GF_T_COMMAND},
    {REC_ARG_READ, REC_GF_T_COMMAND},
    {REC_IMM, REC_GF_T_BAR0},
    {REC_IMM, 0xffffffffU},
    {REC_IMM, REC_GF_T_BAR0},
    {REC_IMM, REC_GF_T_BAR0},
    {REC_ARG_SAVED, 0x0U},
    {REC_IMM, REC_GF_T_BAR1},
    {REC_IMM, 0xffffffffU},
    {REC_IMM, REC_GF_T_BAR1},
    {REC_IMM, REC_GF_T_BAR1},
    {REC_ARG_SAVED, 0x1U},
    {REC_IMM, REC_GF_T_BAR2},
    {REC_IMM, 0xffffffffU},
    {REC_IMM, REC_GF_T_BAR2},
    {REC_IMM, REC_GF_T_BAR2},
    {REC_ARG_SAVED, 0x2U},
    {REC_IMM, REC_GF_T_COMMAND},
    {REC_IMM, RECOVER_COMMAND_OVERRIDE},
};
static const struct recover_plan_entry size_bars_entry[] = {{REC_SIZE_BARS, RECOVER_NONE, 0}};
const struct recover_plan recover_gf_size_bars_plan = {
    .version = 1, .nodes = size_bars_nodes, .node_count = sizeof(size_bars_nodes) / sizeof(size_bars_nodes[0]),
    .args = size_bars_args, .arg_count = sizeof(size_bars_args) / sizeof(size_bars_args[0]), .ctx_count = 1,
    .entries = size_bars_entry, .entry_count = sizeof(size_bars_entry) / sizeof(size_bars_entry[0]),
};

static const struct recover_plan_node prepare_nodes[] = {
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_PREPARE_LITERAL, 1, 0, 0, 0, REC_GF_R_PREPARE_PBUS1854, RECOVER_NONE, 0, 2, {1, 1}},
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_PREPARE_LITERAL, 1, 0, 0, 0, REC_GF_R_PREPARE_PBUS1850, RECOVER_NONE, 2, 2, {2, 2}},
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_PREPARE_LITERAL, 1, 0, 0, 0, REC_GF_R_PREPARE_PMC_IRQ, RECOVER_NONE, 4, 2, {3, 3}},
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_PREPARE_RMW, 1, 0, 0, 0, REC_GF_R_PREPARE_PMC_ENABLE, 3, 6, 2, {4, 4}},
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SELECT, 2, 0, 0, 0, REC_GF_R_PREPARE_CR44_SELECT, RECOVER_NONE, 8, 2, {5, 5}},
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_DATA_LITERAL, 3, 0, 0, 0, REC_GF_R_PREPARE_CR44_DATA, RECOVER_NONE, 10, 2, {6, 6}},
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SELECT, 2, 0, 0, 0, REC_GF_R_PREPARE_CR1F_SELECT, RECOVER_NONE, 12, 2, {7, 7}},
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_DATA_LITERAL, 3, 0, 0, 0, REC_GF_R_PREPARE_CR1F_DATA, RECOVER_NONE, 14, 2, {8, 8}},
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_SELECT, 2, 0, 0, 0, REC_GF_R_PREPARE_CR11_SELECT, RECOVER_NONE, 16, 2, {9, 9}},
    {REC_PREPARE, RECOVER_NONE, RECOVER_NONE, REC_GF_OP_DATA_RMW, 3, 0, 0, 0, REC_GF_R_PREPARE_CR11_UNLOCK, 4, 18, 2, {RECOVER_NONE, RECOVER_NONE}},
};
static const struct recover_plan_arg prepare_args[] = {
    {REC_IMM, REC_GF_T_PBUS1854},
    {REC_IMM, 0x0U},
    {REC_IMM, REC_GF_T_PBUS1850},
    {REC_IMM, 0x0U},
    {REC_IMM, REC_GF_T_PMC_IRQ},
    {REC_IMM, 0x0U},
    {REC_IMM, REC_GF_T_PMC_ENABLE},
    {REC_ARG_READ, REC_GF_T_PMC_ENABLE},
    {REC_IMM, REC_GF_T_CR44},
    {REC_IMM, RECOVER_CR44},
    {REC_IMM, REC_GF_T_CR44},
    {REC_IMM, 0x0U},
    {REC_IMM, REC_GF_T_CR1F},
    {REC_IMM, RECOVER_CR1F},
    {REC_IMM, REC_GF_T_CR1F},
    {REC_IMM, RECOVER_CR1F_UNLOCK},
    {REC_IMM, REC_GF_T_CR11},
    {REC_IMM, RECOVER_CR11},
    {REC_IMM, REC_GF_T_CR11},
    {REC_ARG_READ, REC_GF_T_CR11},
};
static const struct recover_plan_entry prepare_entry[] = {{REC_PREPARE, RECOVER_NONE, 0}};
const struct recover_plan recover_gf_prepare_plan = {
    .version = 1, .nodes = prepare_nodes, .node_count = sizeof(prepare_nodes) / sizeof(prepare_nodes[0]),
    .args = prepare_args, .arg_count = sizeof(prepare_args) / sizeof(prepare_args[0]), .ctx_count = 1,
    .entries = prepare_entry, .entry_count = sizeof(prepare_entry) / sizeof(prepare_entry[0]),
};
