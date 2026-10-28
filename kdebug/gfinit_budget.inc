#ifndef KDEBUG_GFINIT_BUDGET_INC
#define KDEBUG_GFINIT_BUDGET_INC

#include "gfinit_plan_c.h"

/* C has no gf_pre records. Count the actual record sites, including both
 * arms, READ arguments and first index saves. VRAM rows and DAC bytes are
 * internal iterations: each operation has just one BEGIN/END pair. */
#define GF_REC_PAIR_ROWS 2U
#define GF_REC_CELL_ROWS (2U * GF_REC_PAIR_ROWS) /* saved/read-restored only */
#define GF_REC_FIXED_ROWS 128U
#define GF_REC_NODE_ROWS 12U
#define GF_REC_ARG_ROWS 4U
static const struct recover_opcode_layout *gf_budget_layout(const struct recover_profile *p,
                                                            const struct recover_plan_node *n)
{
    for (u32 i = 0; i < p->layout_count; i++)
        if (p->layouts[i].opcode == n->opcode && p->layouts[i].substep == n->substep)
            return &p->layouts[i];
    return 0;
}
static u32 gf_budget_index_ports(const struct recover_profile *p, const struct recover_target *t)
{
    u32 ports = 0;
    for (u32 i = 0; i < p->index_save_count; i++) {
        const struct recover_target *s = &p->targets[p->index_save_targets[i]];
        if (s->bar == t->bar && s->port == t->port && s->head == t->head) ports |= 1U << i;
    }
    return ports;
}
static int gfinit_c_rows(const struct recover_profile *p,
                         const struct recover_plan *plan, u32 first, u32 *rows)
{
    unsigned long long total = 0;
    u32 saved_ports = 0;
    /* Only unconditional A2 SELECTs prove that a port is already saved. */
    for (u32 i = 0; i < first; i++) {
        const struct recover_plan_node *n = &plan->nodes[i];
        if (n->phase != REC_PREPARE) continue;
        const struct recover_opcode_layout *l = gf_budget_layout(p, n);
        if (l && l->dir_substep == REC_SELECT)
            saved_ports |= gf_budget_index_ports(p, &p->targets[plan->args[n->arg_first].value]);
    }
    for (u32 i = first; i < plan->node_count; i++) {
        const struct recover_plan_node *n = &plan->nodes[i];
        const struct recover_plan_arg *a = &plan->args[n->arg_first];
        const struct recover_opcode_layout *l = gf_budget_layout(p, n);
        const struct recover_rule *w = 0;
        for (u32 j = 0; j < p->rule_count; j++)
            if (p->rules[j].id == n->rule_id) w = &p->rules[j];
        if (!l) return -1;
        if (l->flags & (REC_LAYOUT_MODE_VRAM | REC_LAYOUT_MODE_COLOR |
                        REC_LAYOUT_MODE_WAIT | REC_LAYOUT_MODE_ROUTE)) {
            total += GF_REC_PAIR_ROWS;
        } else if (l->flags & REC_LAYOUT_MODE_CHECK) {
            total++; /* CHECK result, also on mismatch or bus failure */
            if (a[REC_CHECK_SOURCE].value == REC_CHECK_TARGET) total += GF_REC_PAIR_ROWS;
            if (a[REC_CHECK_SOURCE].value == REC_CHECK_CELL) total += GF_REC_CELL_ROWS;
        } else {
            for (u32 j = 0; j < n->arg_count; j++)
                if (a[j].kind == REC_ARG_READ) total += GF_REC_PAIR_ROWS;
            if (l->dir_substep == REC_READ) total += GF_REC_PAIR_ROWS;
            else if (l->dir_substep != RECOVER_NONE) {
                if (!w) return -1;
                const struct recover_target *t = &p->targets[a[0].value];
                u32 mask = t->width == 4 ? ~0U : (1U << (t->width * BITS_PER_BYTE)) - 1U;
                total += GF_REC_PAIR_ROWS; /* gf_bus write BEGIN/END */
                if (w->write_mask != mask || (t->read_safe && !(w->flags & REC_RUN_NO_PREREAD)))
                    total += GF_REC_PAIR_ROWS;
                if (w->verify_mask) total += GF_REC_PAIR_ROWS;
                for (u32 j = 0; j < w->guard_count; j++) {
                    if (w->guards[j].when & REC_BEFORE) total += GF_REC_PAIR_ROWS;
                    if (w->guards[j].when & REC_AFTER) total += GF_REC_PAIR_ROWS;
                }
                /* C saves authorized index ports once, never COLD DATA. */
                if (l->dir_substep == REC_SELECT) {
                    u32 ports = gf_budget_index_ports(p, t);
                    if (ports && !(saved_ports & ports)) total += GF_REC_PAIR_ROWS;
                    saved_ports |= ports;
                }
            }
        }
    }
    if (total > ~0U) return -1;
    *rows = (u32)total;
    return 0;
}
static int gfinit_log_rows(const struct recover_profile *p,
                           const struct recover_plan *plan, u32 first, u32 *rows)
{
    u32 nodes = plan->node_count, args = plan->arg_count, c_rows = 0;
    if (first != RECOVER_NONE) {
        if (first >= plan->node_count || gfinit_c_rows(p, plan, first, &c_rows)) return -1;
        nodes = first;
        args = plan->nodes[first].arg_first;
    }
    unsigned long long total = GF_REC_FIXED_ROWS + (unsigned long long)nodes * GF_REC_NODE_ROWS +
        (unsigned long long)args * GF_REC_ARG_ROWS + c_rows;
    if (total > ~0U) return -1;
    *rows = (u32)total;
    return 0;
}
#endif
