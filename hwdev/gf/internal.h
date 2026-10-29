/* GF component contracts, GF_DRIVER_DESIGN §§2-3. */
#ifndef HWDEV_GF_INTERNAL_H
#define HWDEV_GF_INTERNAL_H
#include "../../include/video_service.h"
#include "../../kdebug/gfinit_exec.h" /* decoder types, limits, clock/wait types */
#include "../gf_load_state.h"
#include "scripts.h"

struct gf_init_context;
/* One instruction/PLL transaction, at most one save per PLL destination. */
#define GF_INIT_SAVE_MAX GF_PLL_WRITES_MAX
struct gf_init_saved {
    u32 bar, offset, width, old;
    /* RECOVER_NONE for raw MMIO; otherwise select this byte index before
     * writing old to offset (the data alias). Offsets include head stride. */
    u32 select_offset, select_value;
};
/* BAR-relative accesses; width is 1/2/4 bytes, as in the existing bus.
 * BAR0 VGA aliases only, no real 3Cxh port I/O. Each adapter checks width,
 * bounds and finite polling. read/write retain their distinct alias addresses.
 * timing.clock_us is checked monotonic us; timing.wait_us has finite polling.
 * point checks cancellation only; runner checks point + clock before/after
 * instructions and gf_init_wait checks both around every wait chunk.
 * restore consumes ctx->saved in reverse order, reselecting indexed entries
 * before data writes (skip that data write if selection fails, then continue
 * with older entries). Attempt every entry even after an I/O failure; no point,
 * deadline or wait-budget checks may suppress cleanup. Clear saved_count only
 * if all restores succeed, otherwise return -1 and retain it for diagnostics
 * (no automatic retry). An empty table needs no table writes. Adapter-owned
 * index/lock/window cleanup still follows, even on table failure; propagate
 * either failure as -1. No pending table/adapter saves means no cleanup I/O.
 * Display baseline is separate.
 * All callbacks return 0/-1. opaque and callbacks are borrowed for the run.
 */
struct gf_io {
    void *opaque;
    int (*read)(void *opaque, u32 bar, u32 offset, u32 width, u32 *value);
    int (*write)(void *opaque, u32 bar, u32 offset, u32 width, u32 value);
    struct gfinit_exec_ops timing;
    int (*point)(void *opaque);
    int (*restore)(void *opaque, struct gf_init_context *ctx);
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
    u32 saved_count;
    struct gf_init_saved saved[GF_INIT_SAVE_MAX];
};
/* P2 runner owns these implementations; runs never call timing.wait_us
 * directly or update the wait counters. gf_init_wait returns 0/-1:
 * reject us > GF_INIT_WAIT_MAX - wait_us before any callback (no accounting);
 * otherwise add the FULL request once, even if interrupted part way through.
 * Split into <= GF_INIT_WAIT_CHUNK_US pieces. Before AND after each piece:
 * point(io.opaque), clock_us(io.opaque, &now), elapsed_us=now-started_us,
 * reject elapsed_us >= GF_INIT_EXEC_US. Fail immediately on any callback
 * error; don't issue later pieces. us=0 has no callbacks/accounting change.
 * TIME (unless skip) passes insn->value in us; RESET (even skip) passes
 * GF_INIT_RESET_US. PLL waits, if any, use this same entry. started_us spans
 * ROM acquisition through baseline capture, never resets at main boundaries. */
int gf_init_wait(struct gf_init_context *ctx, u32 us);
/* Save helpers only append a record, no I/O. Return 0/-1; a full table is
 * unchanged on failure, and caller MUST NOT perform the pending overwrite.
 * raw save sets select_offset=RECOVER_NONE; indexed save has width=1.
 * Read old at execution time and save BEFORE first overwrite. For indexed
 * access first raw-save the original index alias, then select/read/save the
 * latch with save_indexed; cleanup therefore restores latch before index.
 * Save each distinct destination once per transaction; later writes reuse
 * its original snapshot. commit only clears saved_count, without I/O.
 * Entry requires saved_count=0; transactions don't nest. On any run error
 * leave saves intact and return -1: runner calls io.restore before quiesce.
 * RESET: restore 184C explicitly, finish the final 1850=0 write, then commit.
 * CR_INDEX_ADDRESS_LATCHED: restore latch AND original index, then commit.
 * PLL2/IO_RESTRICT_PLL2: snapshot every sequence destination before its first
 * write, including literal writes; commit only after the complete sequence
 * (including final gate restore). No other opcode commits or creates saves.
 * P3 PLL uses an operation-scoped gf_init_context borrowing display.io and
 * the same helpers: commit on full success, restore on failure before that
 * context expires. Normal stop never rolls back a committed PLL; cleanup
 * then quiesce only. This table is NOT the main4 display baseline. */
int gf_init_save(struct gf_init_context *ctx, u32 bar, u32 off, u32 width, u32 old);
int gf_init_save_indexed(struct gf_init_context *ctx, u32 bar, u32 select_off,
                         u32 data_off, u32 index, u32 old);
void gf_init_commit(struct gf_init_context *ctx);
/* Table restoration only; adapter restore calls this and always attempts its
 * own index/lock/window cleanup afterwards, even if this returns -1. */
int gf_init_restore(struct gf_init_context *ctx);
/* Caller-owned finite scratch, released after devinit. No expanded plan. */
struct gf_init_program {
    u32 count, main_pc[GF_INIT_MAINS];
    struct gfinit_instruction insns[GF_INIT_INSNS];
};
/* Decode/CRC-check known S1 spans, pair repeats before any I/O, then run
 * main0..4. started_us is supplied by the ROM acquisition owner. On error
 * call io.restore once before returning; owner then quiesces. ctx/work/ROM
 * and all io callbacks are supplied by the owner. */
int gf_init_run(struct gf_init_context *ctx, struct gf_init_program *work);
/* Read-only ROM/known-span validation, also used by P4 before A2 writes. */
int gf_init_prepare(struct gf_init_context *ctx, struct gf_init_program *work);
/* The design's "gf_instruction" is the existing gfinit_instruction tag:
 * share gfinit_decode_instruction and its .inc decoder, no second IR/type.
 * Run entry: pc/opcode/length and opcode-specific ref/count/port/index/mask/
 * shift/target/value are the successful decoder output; unused fields stay
 * zero. effect_first/effect_count are old plan metadata, NOT valid for runs.
 * Runner supplements REPEAT.end_repeat with its matching END_REPEAT PC
 * BEFORE dispatch (decoder alone returns RECOVER_NONE). Match nested pairs
 * by decoding sequentially within each script span, as gfinit_decode_rom
 * does; don't follow SUB or scan ROM bytes as opcodes. Reject unmatched/deep
 * pairs before running the script. Other opcodes keep end_repeat=RECOVER_NONE.
 * Runner sets pc to the sequential successor before run; control ops change
 * pc/stacks/head/skip. DONE returns to call.pc or sets pc=RECOVER_NONE.
 * REPEAT, even in skip, pushes {pc=successor, ctx=insn->end_repeat,
 * count=insn->count}; count is the total iterations, including the first.
 * END_REPEAT, even in skip, requires top.ctx==insn->pc, decrements count,
 * jumps to top.pc while count!=0, otherwise pops and keeps successor pc.
 * Each run returns 0/-1; never encode control flow in a new return-code class.
 * Preserve skip exceptions, CR44 head changes in skip, RESET forced execution
 * and 184C restoration. Reads for LIVE RMW happen at execution time.
 * head is 0/1. Indexed aliases are (port==CRTC ? PRMCIO : PRMVIO) +
 * head*HEAD_STRIDE + port, with data at alias+1 (SEQ/GFX use PRMVIO).
 * CRTC index CR44 itself ALWAYS uses head=0, including a latch's data
 * destination. CR/INDEX_IO/ZM_INDEX_IO set logical head AFTER successful
 * data write to (insn->value==CR44_HEAD1); the literal decoder value is used,
 * not the RMW result. COPY does not change logical head. ZM_CR and each
 * ZM_CR_GROUP element update head even in skip, without I/O then; GROUP's
 * next element uses the updated head. In skip every other opcode with
 * port==CRTC rejects index==CR44 (latched also rejects value==CR44), matching
 * the old expansion. Indexed readers do not update head. */
typedef int (*gf_op_run_fn)(struct gf_init_context *, const struct gfinit_instruction *);
struct gf_op_dispatch { u32 opcode; gf_op_run_fn run; };
extern const struct gf_op_dispatch gf_op_dispatch[];
extern const u32 gf_op_dispatch_count;
/* Shared indexed alias and PLL transaction, without alternate decoding. */
static inline u32 gf_init_index_alias(const struct gf_init_context *ctx, u32 port, u32 index)
{
    u32 head = port == RECOVER_CRTC && index == RECOVER_CR44 ? 0 : ctx->head;
    return (port == RECOVER_CRTC ? RECOVER_PRMCIO : RECOVER_PRMVIO) +
           head * RECOVER_HEAD_STRIDE + port;
}
int gf_init_pll(struct gf_init_context *, u32 reg, u32 khz);

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

enum gf_display_stage { GF_DISPLAY_INIT=GF_LOAD_COMMIT+1, GF_DISPLAY_BASELINE,
    GF_DISPLAY_CALC, GF_DISPLAY_CHECK, GF_DISPLAY_PALETTE, GF_DISPLAY_BLANK,
    GF_DISPLAY_STOP };
struct gf_display_context;
/* P4 owner: acquire performs resource/Command/ROM/A2 preparation and supplies
 * init/program + parsed limits. release drops borrowed ROM/scratch after
 * baseline (also on failure). validate rechecks identity/BAR/Command/legacy/
 * PMC without repeating devinit, and refuses unsafe MMIO after identity change.
 * io.restore owns local index/lock/window cleanup, even for an empty table.
 * A fresh context has reusable=true. Owner/table and bus outlive the device. */
struct gf_display_owner {
    int (*acquire)(struct gf_display_context *);
    void (*release)(struct gf_display_context *);
    int (*validate)(struct gf_display_context *);
};
struct gf_display_context {
    struct gf_io io;
    const struct gf_display_owner *owner;
    /* Borrowed from acquire through release only; load_context is stack-owned
     * by modeset and used for the operation-local PLL transaction/waits. */
    struct gf_init_context *init, *load_context;
    struct gf_init_program *program;
    struct recover_identity identity; /* metadata only, no recover executor */
    struct recover_bar_extent bars[RECOVER_BAR_COUNT]; /* raw/base/length */
    void *bar0_map, *bar1_map;
    u32 rom_crc, rom_size; /* identification only; no retained ROM pointer */
    struct gfinit_pll_limits limits;
    struct gfinit_mode_saved baseline;
    u32 baseline_c040, baseline_pmc;
    bool initialized, baseline_valid, active, blank, stop_ok, reusable, engaged;
    struct gfinit_mode mode;
    u8 palette[VIDEO_PALETTE_BYTES];
    /* Runtime row state: first C040 is kept, FP/LIVE predicates are not
     * precomputed. baseline_valid only after every main4 capture succeeds;
     * CR52 always derives from baseline, never from the last modeset. */
    u32 section, ordinal, target, pll_gate;
    bool pll_gate_valid, fp_on;
    int last_rc;
};
/* Connect via a thin void* adapter to gf_load_io.row; section/wait retain
 * gf_load_io's existing signatures and ms/fp_only semantics. gf_load_state
 * alone owns ordering/DPMS/waits; don't duplicate generated gf_mode_rows.
 * AR uses FF-reset/index/data; DAC is write-only auto-increment. */
int gf_load_row(struct gf_display_context *, const struct gfinit_mode *, u32 ordinal);
/* GF implementations of the eight video operations. */
int gf_display_query(void *, struct video_info *);
int gf_display_mode_info(void *, u32, struct video_mode *);
int gf_display_framebuffer(void *, struct video_fb *);
int gf_display_start(void *, u32);
int gf_display_palette(struct gf_display_context *, u32, u32, const u8 *);
int gf_display_blank(struct gf_display_context *, int);
int gf_display_modeset(struct gf_display_context *);
int gf_display_baseline(struct gf_display_context *);
int gf_display_checks(struct gf_display_context *);
int gf_display_stop(void *);
extern const struct video_ops gf_video_ops;
#endif
