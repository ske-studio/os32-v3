#ifndef KDEBUG_RECOVER_GF_C_H
#define KDEBUG_RECOVER_GF_C_H
#include "../hwdev/gfinit_mode.h"
/* Stage-1 allow-table contract only: NOT a recover plan/profile. IDs are in
 * the normal profile's append-only namespace; the separate VPLL diagnostic
 * profile is not a C profile. No default rules/targets/layouts are expanded.
 * Indexed rows mean index/data pairs; AR rows require FF reset before EACH
 * index/data pair; DAC stream is write-only, 256 RGB triples. These types
 * must be lowered to dedicated bus operations in stage 2, never MMIO ranges.
 */
#define REC_GF_C_RULE_FIRST 578U
#define REC_GF_C_TARGET_FIRST 154U
#define REC_GF_C_PHASE 9U
#define REC_GF_C_NO_INDEX 0xffffffffU
#define REC_GF_C_FP_ON 1U
/* Compare coefficients only in CHECK after 68050Ch selects VPLL_PROG.
 * With bit9 clear, readback exposes the fixed source, not these writes. */
#define REC_GF_C_CHECK_508_MASK 0x0007ffffU
#define REC_GF_C_CHECK_578_MASK 0x8000ffffU
#define REC_GF_C_PALETTE_BYTES (GF_MODE_PALETTE_SIZE * GF_MODE_RGB)
enum recover_gf_c_kind { GF_C_MMIO, GF_C_CR, GF_C_SR, GF_C_GR,
    GF_C_AR, GF_C_MISC, GF_C_AR_GATE, GF_C_DAC_MASK, GF_C_DAC_INDEX,
    GF_C_DAC_DATA, GF_C_ROUTE };
enum recover_gf_c_expr { GF_C_LITERAL, GF_C_STATE, GF_C_LIVE,
    GF_C_PLL, GF_C_PALETTE };
/* source is a byte offset in gfinit_mode for STATE, sequence write ordinal
 * for PLL (its source/keep/value are authoritative), zero otherwise.
 * LIVE uses (current target & keep) | bits; PLL's last C040 uses the capture
 * from the first C040, not a new read. Literal/STATE have keep=0.
 * All C data writes are COLD, including shared indexed keys. Existing
 * index/lock restoration grants no mode-data restore.
 * verify=0 for scratch, AR/DAC and explicit write-only operations.
 */
struct recover_gf_c_row {
    u32 id, key, phase, kind, reg, index, width, expr, source;
    u32 keep, bits, verify, count, flags;
};
struct recover_gf_c_target {
    u32 key, kind, reg, index, width, access, read_safe;
};
extern const struct recover_gf_c_row recover_gf_c_rows[];
extern const struct recover_gf_c_target recover_gf_c_targets[];
extern const u32 recover_gf_c_row_count, recover_gf_c_target_count;
#endif
