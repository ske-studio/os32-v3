#ifndef KDEBUG_RECOVER_GF_H
#define KDEBUG_RECOVER_GF_H
#include "recover.h"
#include "../include/memmap.h"
#define REC_GF_OPCODE_TICKS (60U * PIT_HZ)
#define REC_GF_MAX_POLLS 65536U
/* Recover owns the rules; S2 supplies real opcode layouts and expanded plan.
 * A caller copies this immutable template, supplies checked tick budgets and
 * layouts, then calculates recover_profile_crc before bind. No default arm.
 * Fixed columns are ordered rule IDs; SIZE_BARS reads each mask between the
 * probe and saved-value write, feeding recover_bar_probe before continuing.
 */
#define REC_GF_PLL_SAVED 8U
#define REC_GF_RMW 5U
#define REC_GF_EXPR_COPY 6U
#define REC_GF_EXPR_COPY_NV 7U
#define REC_GF_SAVED_BAR 1U
#define REC_GF_COMMAND_OFF 2U
#define REC_GF_PMC_ENABLE 3U
#define REC_GF_CR11_UNLOCK 4U
#define REC_GF_PREPARE_COUNT 10U
#define REC_GF_SIZE_BARS_COUNT 8U
extern const struct recover_profile recover_gf_template;
extern const u32 recover_gf_prepare[REC_GF_PREPARE_COUNT];
extern const u32 recover_gf_size_bars[REC_GF_SIZE_BARS_COUNT];
/* Copy plan and set profile_crc after supplying checked budgets. PREPARE's
 * CR11 expression reads the target selected by its preceding SELECT node. */
extern const struct recover_plan recover_gf_size_bars_plan;
extern const struct recover_plan recover_gf_prepare_plan;
#endif
