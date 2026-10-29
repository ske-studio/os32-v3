#ifndef KDEBUG_GFINIT_PLAN_C_H
#define KDEBUG_GFINIT_PLAN_C_H
#include "gfinit_plan.h"
#define GF_C_OPCODE 0x201U
#define GF_C_EXPR_LIVE 0x210U
#define GF_C_EXPR_SAVED 0x211U
#define GF_C_EXPR_FP_ON 0x212U
#define GF_C_EXPR_BASE 0x213U
/* Substeps below the stage-1 row IDs belong to composition, not data rows. */
enum gf_c_substep { GF_C_SELECT, GF_C_READ, GF_C_SECTION, GF_C_UNBOUND,
    GF_C_WAIT, GF_C_BRANCH, GF_C_VRAM_WRITE, GF_C_VRAM_COMPARE, GF_C_COLOR,
    GF_C_CHECK, GF_C_CHECK_SAVED, GF_C_ROUTE_SET };
enum gf_c_section { GF_C_CHECK_IN, GF_C_Q, GF_C_P, GF_C_VRAM, GF_C_SD,
    GF_C_L1, GF_C_L2, GF_C_L3, GF_C_COMMIT, GF_C_R, GF_C_CHECK_OUT,
    GF_C_SECTION_COUNT };
enum gf_c_pending { GF_C_PENDING_CHECK = 1, GF_C_PENDING_VRAM,
    GF_C_PENDING_ROW, GF_C_PENDING_CELL, GF_C_PENDING_EXIT };
#define GF_C_FP_WAIT_MS 50U
#define GF_C_DPMS_WAIT_MS 10U
#define GF_C_EXIT_ROUTE_SLOT (GF_C_CHECK_MAX - 2U)
#define GF_C_EXIT_CELL_SLOT (GF_C_CHECK_MAX - 1U)
/* No op13 or cleanup extension. */
int gfinit_plan_build_c(struct gfinit_plan *out, struct gfinit_decode *d,
                        struct recover_profile *p, u32 bar1_length,
                        const u8 *rom, u32 rom_size);
struct gf_stage_state;
int gfinit_plan_rebuild_c(struct gfinit_plan *, struct gfinit_decode *,
                          struct recover_profile *, const struct gf_stage_state *);
int gfinit_plan_baseline(struct gfinit_plan *, struct recover_profile *,
                         struct gf_stage_state *, const u8 *, u32);
int gfinit_plan_c_vram(void *bus, struct recover *r, bool write,
                       u32 *bad_offset, u32 *expected, u32 *actual);
int gfinit_plan_c_color(void *bus, struct recover *r, u32 row, u32 value);
/* Execute only an already-entered C control; C dispatch/op13 is later. */
int gfinit_plan_c_control(struct gfinit_plan *out, struct recover *r);
#endif
