/* GF component contracts, GF_DRIVER_DESIGN §§2-3. No hardware implementation. */
#ifndef HWDEV_GF_INTERNAL_H
#define HWDEV_GF_INTERNAL_H
#include "../../include/video_service.h"
#include "../../kdebug/gfinit_exec.h" /* decoder types, limits, clock/wait types */
#include "../gf_load_state.h"

/* BAR-relative accesses; width is 1/2/4 bytes, as in the existing bus.
 * BAR0 VGA aliases only, no real 3Cxh port I/O. Each adapter checks width,
 * bounds and finite polling. read/write retain their distinct alias addresses.
 * timing.clock_us is checked monotonic us; timing.wait_us has finite polling.
 * point checks cancellation; the runner calls it at instructions/wait chunks.
 * restore restores adapter-owned local index/lock/window/partial PLL saves,
 * even after failure; these saves are separate from the display baseline.
 * All callbacks return 0/-1. opaque and callbacks are borrowed for the run.
 */
struct gf_io {
    void *opaque;
    int (*read)(void *opaque, u32 bar, u32 offset, u32 width, u32 *value);
    int (*write)(void *opaque, u32 bar, u32 offset, u32 width, u32 value);
    struct gfinit_exec_ops timing;
    int (*point)(void *opaque);
    int (*restore)(void *opaque);
};
struct gf_init_context {
    struct gf_io io;
    /* Borrowed until runner returns; never kept by display. The existing
     * GF_ROM_MAX ceiling is KPEEK_NV_PROM_LEN, not a new ROM size limit. */
    const u8 *rom;
    u32 rom_size, main_id, pc, head;
    bool skip;
    u32 calls, repeats;
    /* call: pc=return PC, ctx=entry repeat depth, count=0.
     * repeat: pc=body PC, ctx=END_REPEAT PC, count=remaining iterations. */
    struct gfinit_exec_frame call[GF_INIT_CALL_DEPTH];
    struct gfinit_exec_frame repeat[GF_INIT_REPEAT_DEPTH];
    /* Existing ceilings: GF_INIT_SCHEDULE steps, GF_INIT_WAIT_MAX wait us,
     * GF_INIT_EXEC_US elapsed us; waits split at GF_INIT_WAIT_CHUNK_US.
     * started_us covers ROM acquisition through main4 baseline capture. */
    u32 steps, wait_us, started_us, elapsed_us, fail_pc;
    int last_rc;
};
/* The design's "gf_instruction" is the existing gfinit_instruction tag:
 * share gfinit_decode_instruction and its .inc decoder, no second IR/type.
 * Runner sets pc to the sequential successor before run; control ops change
 * pc/stacks/head/skip. DONE returns to call.pc or sets pc=RECOVER_NONE.
 * Each run returns 0/-1; never encode control flow in a new return-code class.
 * Preserve skip exceptions, CR44 head changes in skip, RESET forced execution
 * and 184C restoration. Reads for LIVE RMW happen at execution time. */
typedef int (*gf_op_run_fn)(struct gf_init_context *, const struct gfinit_instruction *);
struct gf_op_dispatch { u32 opcode; gf_op_run_fn run; };
extern const struct gf_op_dispatch gf_op_dispatch[];
extern const u32 gf_op_dispatch_count;

int gf_op_io_restrict_prog_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_repeat_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_end_repeat_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_copy_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_not_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_io_flag_condition_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_io_restrict_pll2_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_pll2_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_cr_index_address_latched_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_cr_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_zm_cr_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_zm_cr_group_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_zm_reg_sequence_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_sub_direct_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_copy_nv_reg_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_zm_index_io_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_compute_mem_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_reset_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_sub_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_nv_reg_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_macro_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_done_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_resume_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_time_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_condition_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_index_io_run(struct gf_init_context *, const struct gfinit_instruction *);
int gf_op_zm_reg_run(struct gf_init_context *, const struct gfinit_instruction *);

struct gf_display_context {
    struct gf_io io;
    struct recover_identity identity; /* metadata only, no recover executor */
    struct recover_bar_extent bars[RECOVER_BAR_COUNT]; /* raw/base/length */
    void *bar0_map, *bar1_map;
    u32 rom_crc, rom_size; /* identification only; no retained ROM pointer */
    struct gfinit_pll_limits limits;
    struct gfinit_mode_saved baseline;
    u32 baseline_c040, baseline_pmc;
    bool initialized, baseline_valid, active, blank, stop_ok, reusable;
    struct gfinit_mode mode;
    u8 palette[VIDEO_PALETTE_BYTES];
    /* Runtime row state: first C040 is kept, FP/LIVE predicates are not
     * precomputed. baseline_valid only after every main4 capture succeeds;
     * CR52 always derives from baseline, never from the last modeset. */
    u32 section, ordinal, pll_gate;
    bool pll_gate_valid, fp_on;
    int last_rc;
};
/* Connect via a thin void* adapter to gf_load_io.row; section/wait retain
 * gf_load_io's existing signatures and ms/fp_only semantics. gf_load_state
 * alone owns ordering/DPMS/waits; don't duplicate generated gf_mode_rows.
 * AR uses FF-reset/index/data; DAC is write-only auto-increment. */
int gf_load_row(struct gf_display_context *, const struct gfinit_mode *, u32 ordinal);
#endif
