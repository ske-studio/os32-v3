#ifndef KDEBUG_GFINIT_PLAN_H
#define KDEBUG_GFINIT_PLAN_H
#include "gfinit_decode.h"
/* Synthetic opcodes, separate from ROM instruction bytes. */
#define REC_GF_OP_SIZE_COMMAND_OFF 0x100U
#define REC_GF_OP_SIZE_PROBE 0x101U
#define REC_GF_OP_SIZE_READ 0x102U
#define REC_GF_OP_SIZE_RESTORE_BAR 0x103U
#define REC_GF_OP_SIZE_RESTORE_COMMAND 0x104U
#define REC_GF_OP_PREPARE_LITERAL 0x105U
#define REC_GF_OP_PREPARE_RMW 0x106U
#define REC_GF_OP_SELECT 0x107U
#define REC_GF_OP_DATA_LITERAL 0x108U
#define REC_GF_OP_DATA_RMW 0x109U
#define GF_PLAN_OPCODE 0x200U
#define GF_PLAN_PFB_AMOUNT 0x10020cU
#define GF_PLAN_SUB_SAVE 0
#define GF_PLAN_SUB_LITERAL 1
#define GF_PLAN_SUB_CHECK 2
#define GF_PLAN_SUB_ALIAS_WRITE 3
#define GF_PLAN_SUB_BRANCH 4
#define GF_PLAN_SUB_EQUAL 5
#define GF_PLAN_SUB_COUNT 6
#define GF_PLAN_TARGETS 768U
#define GF_PLAN_RULES 1280U
#define GF_PLAN_LAYOUTS 1056U
#define GF_PLAN_EXPRS 16U
#define GF_PLAN_VRAM_WORDS 256U
#define GF_PLAN_CHECK_WORDS 8U
struct gfinit_plan {
    struct recover_plan plan;
    struct recover_plan_entry entries[RECOVER_ENTRY_MAX];
    struct recover_plan_dep deps[RECOVER_DEP_MAX];
    struct recover_target targets[GF_PLAN_TARGETS];
    struct recover_rule rules[GF_PLAN_RULES];
    struct recover_opcode_layout layouts[GF_PLAN_LAYOUTS];
    struct recover_expr exprs[GF_PLAN_EXPRS];
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
