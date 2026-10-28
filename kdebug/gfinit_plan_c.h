#ifndef KDEBUG_GFINIT_PLAN_C_H
#define KDEBUG_GFINIT_PLAN_C_H
#include "gfinit_plan.h"
#define GF_C_OPCODE 0x201U
#define GF_C_EXPR_LIVE 0x210U
#define GF_C_EXPR_SAVED 0x211U
#define GF_C_EXPR_FP_ON 0x212U
/* Substeps below the stage-1 row IDs belong to composition, not data rows. */
enum gf_c_substep { GF_C_SELECT, GF_C_READ, GF_C_SECTION, GF_C_UNBOUND,
    GF_C_WAIT, GF_C_BRANCH };
enum gf_c_section { GF_C_CHECK_IN, GF_C_Q, GF_C_P, GF_C_VRAM, GF_C_SD,
    GF_C_L1, GF_C_L2, GF_C_L3, GF_C_COMMIT, GF_C_R, GF_C_CHECK_OUT,
    GF_C_SECTION_COUNT };
enum gf_c_pending { GF_C_PENDING_CHECK = 1, GF_C_PENDING_VRAM,
    GF_C_PENDING_ROW, GF_C_PENDING_CELL, GF_C_PENDING_EXIT };
#define GF_C_FP_WAIT_MS 50U
#define GF_C_DPMS_WAIT_MS 10U
/* Initial bind only. No op13, VRAM/AR/DAC/route/cell or cleanup extension.
 * Unbound nodes contain the complete typed row and reject execution. */
int gfinit_plan_build_c(struct gfinit_plan *out, struct gfinit_decode *d,
                        struct recover_profile *p, u32 bar1_length,
                        const u8 *rom, u32 rom_size);
#endif
