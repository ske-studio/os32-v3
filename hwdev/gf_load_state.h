#ifndef HWDEV_GF_LOAD_STATE_H
#define HWDEV_GF_LOAD_STATE_H
#include "gfinit_mode.h"
#include <stdbool.h>
#define GF_LOAD_FP_WAIT_MS 50U
#define GF_LOAD_DPMS_WAIT_MS 10U
#define GF_LOAD_FP_ON 1U
#define GF_LOAD_FP_ON_MASK 0x10000011U
#define GF_LOAD_NO_INDEX 0xffffffffU
#define GF_LOAD_PALETTE_BYTES (GF_MODE_PALETTE_SIZE * GF_MODE_RGB)
enum gf_load_section { GF_LOAD_Q, GF_LOAD_P, GF_LOAD_SD, GF_LOAD_L1,
    GF_LOAD_L2, GF_LOAD_L3, GF_LOAD_COMMIT };
enum gf_mode_kind { GF_C_MMIO, GF_C_CR, GF_C_SR, GF_C_GR,
    GF_C_AR, GF_C_MISC, GF_C_AR_GATE, GF_C_DAC_MASK, GF_C_DAC_INDEX,
    GF_C_DAC_DATA };
enum gf_mode_expr { GF_C_LITERAL, GF_C_STATE, GF_C_LIVE,
    GF_C_PLL, GF_C_PALETTE };
/* source is a byte offset in gfinit_mode for STATE, sequence write ordinal
 * for PLL (its source/keep/value are authoritative), zero otherwise.
 * LIVE uses (current target & keep) | bits; PLL's last C040 uses the capture
 * from the first C040, not a new read. Literal/STATE have keep=0.
 * All C data writes are COLD, including shared indexed keys. Existing
 * index/lock restoration grants no mode-data restore.
 * verify=0 for scratch, AR/DAC and explicit write-only operations.
 */
struct gf_mode_row {
    u32 kind, reg, index, width, expr, source;
    u32 keep, bits, verify, count, flags;
};
extern const struct gf_mode_row gf_mode_rows[];
extern const u32 gf_mode_row_count;
/* Consumers retain LIVE/FP predicates and the first PLL gate value at runtime.
 * saved is the caller's saved-state/capture context. A builder emits plan
 * operations here; a driver can consume the same descriptors using its bus.
 * Palette contents belong to the mode caller, not this hardware sequence. */
struct gf_load_io {
    int (*section)(void *saved, u32 section);
    int (*row)(void *saved, const struct gfinit_mode *mode, u32 ordinal);
    int (*wait)(void *saved, u32 ms, bool fp_only);
};
int gf_load_state(const struct gfinit_mode *mode, void *saved, const struct gf_load_io *io);
#endif
