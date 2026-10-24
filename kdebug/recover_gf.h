#ifndef KDEBUG_RECOVER_GF_H
#define KDEBUG_RECOVER_GF_H
#include "recover.h"
#include "../include/memmap.h"
/* Stable target keys and rule IDs used by the fixed plans. The corresponding
 * table entries use these same designated indices in recover_gf.c. */
#define REC_GF_T_CFG50 0U
#define REC_GF_T_CFG54 1U
#define REC_GF_T_PBUS1850 2U
#define REC_GF_T_PBUS1854 3U
#define REC_GF_T_PMC_IRQ 4U
#define REC_GF_T_PMC_ENABLE 5U
#define REC_GF_T_COMMAND 6U
#define REC_GF_T_BAR0 7U
#define REC_GF_T_BAR1 8U
#define REC_GF_T_BAR2 9U
#define REC_GF_T_CR44 10U
#define REC_GF_T_CR1F 11U
#define REC_GF_T_CR11 12U
#define REC_GF_T_SR01_HEAD1 16U
#define REC_GF_T_SR01_HEAD0 17U
#define REC_GF_T_PLL_GATE 25U
#define REC_GF_T_PLL_CORE_CTRL 26U
#define REC_GF_T_PLL_VIDEO_STAGE 28U
#define REC_GF_T_PLL_MEMORY_CTRL 29U
#define REC_GF_T_CR21_HEAD1 111U
#define REC_GF_R_PREPARE_PBUS1854 0U
#define REC_GF_R_PREPARE_PBUS1850 1U
#define REC_GF_R_PREPARE_PMC_IRQ 2U
#define REC_GF_R_PREPARE_PMC_ENABLE 3U
#define REC_GF_R_QUIESCE_PBUS1850 4U
#define REC_GF_R_QUIESCE_PBUS1854 5U
#define REC_GF_R_QUIESCE_PMC_IRQ 6U
#define REC_GF_R_RESTORE_BAR0 7U
#define REC_GF_R_RESTORE_BAR1 8U
#define REC_GF_R_RESTORE_BAR2 9U
#define REC_GF_R_RESTORE_COMMAND 10U
#define REC_GF_R_SIZE_COMMAND_OFF 11U
#define REC_GF_R_SIZE_PROBE_BAR0 12U
#define REC_GF_R_SIZE_RESTORE_BAR0 13U
#define REC_GF_R_SIZE_PROBE_BAR1 14U
#define REC_GF_R_SIZE_RESTORE_BAR1 15U
#define REC_GF_R_SIZE_PROBE_BAR2 16U
#define REC_GF_R_SIZE_RESTORE_BAR2 17U
#define REC_GF_R_SIZE_RESTORE_COMMAND 18U
#define REC_GF_R_RESTORE_CR44_SELECT 19U
#define REC_GF_R_RESTORE_CR44_DATA 20U
#define REC_GF_R_PREPARE_CR44_SELECT 21U
#define REC_GF_R_PREPARE_CR44_DATA 22U
#define REC_GF_R_RESTORE_CR1F_SELECT 23U
#define REC_GF_R_RESTORE_CR1F_DATA 24U
#define REC_GF_R_PREPARE_CR1F_SELECT 25U
#define REC_GF_R_PREPARE_CR1F_DATA 26U
#define REC_GF_R_RESTORE_CR11_SELECT 27U
#define REC_GF_R_RESTORE_CR11_DATA 28U
#define REC_GF_R_PREPARE_CR11_SELECT 29U
#define REC_GF_R_PREPARE_CR11_UNLOCK 30U
#define REC_GF_VRAM_RESERVED 1024U
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
