#ifndef KDEBUG_GFINIT_PLAN_H
#define KDEBUG_GFINIT_PLAN_H
#include "gfinit_decode.h"
#define GF_PLAN_VRAM_WORDS 256U
#define GF_PLAN_CHECK_WORDS 8U
struct gfinit_plan {
    struct recover_plan plan;
    struct recover_plan_entry entries[RECOVER_ENTRY_MAX];
    struct recover_plan_dep deps[RECOVER_DEP_MAX];
    struct recover_target targets[RECOVER_TARGET_MAX];
    struct recover_rule rules[RECOVER_RULE_MAX];
    struct recover_opcode_layout layouts[RECOVER_RULE_MAX];
    struct recover_expr exprs[RECOVER_RULE_MAX];
    u32 saved_vram[GF_PLAN_VRAM_WORDS];
    u32 vram_first, vram_node, saved_count;
    u32 pfb_key, bar1_length, capacity_bytes, tested_bytes, capacity_known, full_capacity;
};
/* Consumes fresh decoded arrays in place; out.plan borrows their lifetime.
 * Profile tables become owned by out. Recompute CRC after owner budgets. */
int gfinit_plan_build(struct gfinit_plan *out, struct gfinit_decode *decoded,
                      struct recover_profile *profile, u32 bar1_length);
void gfinit_plan_read(struct gfinit_plan *out, const struct recover *r,
                      const struct recover_bus_access *access, u32 value);
int gfinit_plan_verify(struct recover *r, void *arg);
#endif
