#ifndef KDEBUG_RECOVER_H
#define KDEBUG_RECOVER_H

#include "types.h"
#include <stdbool.h>

/* DESIGN.final §3–5, §7 T1. Host-only until T2 wires the imports.
 * All callbacks are synchronous, bounded, and return 0 on success. No callback
 * may dispatch shell commands. A step must call recover_safe_point at each
 * poll/delay/instruction boundary. The owner initializes once per boot; FAILED
 * and cold-required cannot be cleared by starting a new session.
 */
#define RECOVER_MAX_REGS 32U
#define RECOVER_PROFILE_VERSION 2U
#define RECOVER_HALF_TICKS 0x80000000UL
#define RECOVER_ROUTE_PORT 0x0FACU
#define RECOVER_ROUTE_MASK 0x03U
#define RECOVER_ROUTE_98 0U
#define RECOVER_COMMAND_MEM 0x0002U
#define RECOVER_COMMAND_DMA_MEM 0x0006U

enum recover_state {
    REC_IDLE, REC_SNAPSHOTTING, REC_ARMED, REC_RUNNING, REC_PAUSED,
    REC_RESTORING, REC_VERIFIED, REC_FAILED
};
enum recover_reason {
    REC_NORMAL, REC_ABORT, REC_TIMEOUT, REC_OPERATION, REC_CONTRACT,
    REC_SNAPSHOT, REC_CONFIG, REC_BUS, REC_VERIFY, REC_LOG
};
enum recover_kind { REC_RW, REC_COLD, REC_W1C, REC_UNCLASSIFIED };
enum recover_phase { REC_PHASE_NONE, REC_PHASE_SAVE, REC_PHASE_QUIESCE,
    REC_PHASE_RESTORE, REC_PHASE_ROUTE, REC_PHASE_SCREEN, REC_PHASE_VERIFY };
enum recover_request { REC_REQUEST_NOW, REC_REQUEST_STATUS };

/* Target IDs are shared by snapshot, plan, guards and cleanup. */
#define RECOVER_NONE 0xffffffffU
#define RECOVER_TARGET_MAX 1024U
#define RECOVER_RULE_MAX 2048U
#define RECOVER_LINK_MAX 8U
#define RECOVER_NODE_MAX 2048U
#define RECOVER_ARG_MAX 8192U
#define RECOVER_CTX_MAX 2048U
#define RECOVER_DEP_MAX 64U
#define RECOVER_ENTRY_MAX 32U
#define RECOVER_BITS_WORDS (RECOVER_NODE_MAX / 32U)
#define RECOVER_RANGE_MAX 256U
#define RECOVER_EXPR_ARGS 32U
#define RECOVER_PRMVIO 0x0c0000U
#define RECOVER_PRMCIO 0x601000U
#define RECOVER_HEAD_STRIDE 0x2000U
#define RECOVER_CFG_SIZE 256U
#define RECOVER_COMMAND 0x04U
#define RECOVER_COMMAND_OVERRIDE 0x0102U
#define RECOVER_CFG50 0x50U
#define RECOVER_CFG54 0x54U
#define RECOVER_PBUS1850 0x1850U
#define RECOVER_PBUS1854 0x1854U
#define RECOVER_SEQ 0x3c4U
#define RECOVER_GFX 0x3ceU
#define RECOVER_CRTC 0x3d4U
#define RECOVER_CR44 0x44U
#define RECOVER_SR01 1U
#define RECOVER_SR_BLANK 0x20U
#define RECOVER_N1_PC 0xe81eU
#define RECOVER_BAR_MASK 0xfffffff0U
#define RECOVER_ADDRESS_END (1ULL << 32)

enum recover_space { REC_PCI_CFG, REC_BAR_MMIO, REC_BAR_VRAM,
    REC_PC98_IO, REC_VGA_DIRECT, REC_VGA_INDEXED };
enum recover_auth_phase { REC_SIZE_BARS, REC_PREPARE, REC_RUN, REC_VRAM_TEST,
    REC_QUIESCE, REC_RESTORE, REC_ROUTE, REC_SCREEN, REC_VERIFY_PHASE };
enum recover_direction { REC_READ, REC_WRITE, REC_SELECT, REC_DATA };
enum recover_value_mode { REC_LITERAL, REC_SET, REC_RANGE, REC_EXPR, REC_SAVED };
enum recover_arg_kind { REC_IMM, REC_ARG_READ, REC_ARG_SAVED };
enum recover_guard_when { REC_BEFORE = 1, REC_AFTER = 2 };
enum recover_rule_flag { REC_N1 = 1, REC_LOCAL = 2, REC_FORCE = 4 };
struct recover_target {
    u32 space, bar, offset, head, port, index, width, access, read_safe;
};
struct recover_value {
    u32 mode, mask, bits, min, max, expr_id;
    const u32 *allowed; u32 allowed_count;
};
struct recover_guard { u32 key, mask, expected, when; };
struct recover_rule {
    u32 id, key, phase, main_id, pc, substep, write_mask, verify_mask;
    struct recover_value value;
    const u32 *requires; u32 requires_count;
    const struct recover_guard *guards; u32 guard_count;
    u32 flags;
};
struct recover_expr {
    u32 id, argc; const u32 *kinds;
    int (*eval)(const u32 *args, u32 argc, u32 *value);
};
struct recover_opcode_layout {
    u32 opcode, substep, argc; const u32 *kinds; u32 dir_substep;
};
struct recover_plan_arg { u32 kind, value; };
struct recover_plan_node {
    u32 phase, main_id, pc, opcode, substep, ctx, head, skip;
    u32 rule_id, expr_id, arg_first, arg_count, next[2];
};
struct recover_plan_dep { u32 consumer, required_rule, producer; };
struct recover_plan_entry { u32 phase, main_id, first; };
struct recover_plan {
    u32 version, profile_crc, rom_crc, rom_size;
    const struct recover_plan_node *nodes; u32 node_count;
    const struct recover_plan_arg *args; u32 arg_count;
    u32 ctx_count;
    const struct recover_plan_dep *deps; u32 dep_count;
    const struct recover_plan_entry *entries; u32 entry_count;
};
struct recover_plan_buffers { u32 *flow_bits, *valid_bits; };
struct recover_bar_extent { u32 raw, base, length, valid; };
struct recover_range { u32 base, len; };
/* key remains a target ID. Resolved offset is BAR-relative (or cfg/I/O).
 * Bus must use this address, never infer SELECT/DATA from previous calls. */
struct recover_bus_access { u32 key, dir_substep, space, bar, offset, width; };
struct recover_reg {
    u32 key, width, write_mask, verify_mask;
    enum recover_kind kind;
    bool read_safe;
    u32 restore_rule_id;
};
struct recover_write { u32 key, width, value, verify_mask, rule_id; };
struct recover_identity {
    u32 boot, image_version, kdebug_version, bdf, device, subsys, revision, bar0;
};
struct recover_bar_measurement {
    struct recover_identity identity;
    u32 measurement_session;
    struct recover_bar_extent bar[3];
};
struct recover_profile {
    u32 version, crc;
    const struct recover_reg *regs; /* restoration order, not reverse order */
    u32 count;
    const struct recover_write *quiesce;
    u32 quiesce_count;
    u32 trial_limit, opcode_limit, restore_limit, max_steps, max_polls;
    bool cold_required;
    const struct recover_target *targets; u32 target_count;
    const struct recover_rule *rules; u32 rule_count;
    const struct recover_expr *exprs; u32 expr_count;
    const struct recover_opcode_layout *layouts; u32 layout_count;
    u32 restore_max_polls, s0_reserve_ticks, s0_reserve_polls;
    /* VRAM reservation is a BAR-relative half-open interval, not capacity. */
    struct recover_range vram_reserved;

};
struct recover_token {
    u32 session, profile_crc, profile_version, snapshot_crc, plan_hash;
    struct recover_identity identity;
};
struct recover_local_saved { u32 key, dir_substep, value; };
struct recover;
struct recover_ops {
    u32 (*ticks)(void *opaque);
    int (*identity)(void *opaque, struct recover_identity *out);
    int (*start_ready)(void *opaque); /* stopped device AND confirmed CUI */
    int (*raw_config)(void *opaque, u32 *command, u32 bar[3]);
    int (*read)(void *opaque, const struct recover_bus_access *a, u32 *value);
    int (*write)(void *opaque, const struct recover_bus_access *a, u32 value);
    int (*route_read)(void *opaque, u32 *value); /* 0FACh byte */
    int (*route_write)(void *opaque, u32 value);
    /* stop output/DMA, release AND verify decode independently of MMIO.
     * Failure prohibits display/VRAM accesses. Shared HAL implementation T2. */
    int (*screen_safe)(void *opaque);
    int (*screen_restore_verify)(void *opaque);
    int (*abort_requested)(void *opaque, u32 session); /* bounded serial poll */
    /* Sole owner of local PTE/IRQ/index/window release; idempotent. Uses
     * recover_access(NONE, ...) for classified local writes. Failure is fatal.
     * Operation returns immediately after any nonzero accessor/safe-point. */
    int (*local_cleanup)(void *opaque);
    int (*event)(void *opaque, enum recover_state state, u32 session);
};
struct recover {
    const struct recover_ops *ops;
    void *opaque;
    const struct recover_profile *profile;
    struct recover_token token;
    u32 saved[RECOVER_MAX_REGS], saved_count;
    struct recover_local_saved local_saved[RECOVER_MAX_REGS];
    u32 local_saved_count;
    u32 next_session, budget, spent, call_tick, opcode_tick, restore_tick;
    u32 steps, polls, first_key;
    enum recover_state state;
    enum recover_reason reason;
    enum recover_phase first_phase;
    const struct recover_plan *plan;
    const struct recover_profile *bound_profile;
    struct recover_plan_buffers buffers;
    u32 bound_hash, cursor, expected_node, head, auth_phase, selected;
    bool entered, accessed, zero_hold, gpu_written, measuring;
    struct recover_bar_measurement cache, measurement;
    struct recover_bar_extent bars[3];
    bool enabled, route_write_approved, valid, in_call, in_opcode;
    bool failed, access_blocked, cold_latched, s0_verified, s1_verified;
};

/* profile_ok: format only; bind validates every path; begin checks extents. */
int recover_profile_ok(const struct recover_profile *p);
int recover_target_address(const struct recover_target *t, u32 dir,
                           struct recover_bus_access *a);
int recover_bind_plan(struct recover *r, const struct recover_profile *p,
    const struct recover_plan *plan, struct recover_plan_buffers *buf, u32 *hash);
int recover_plan_enter(struct recover *r, u32 node);
int recover_plan_leave(struct recover *r, u32 edge);
int recover_access(struct recover *r, u32 node, u32 dir_substep,
                   u32 target, u32 width, u32 *value);
int recover_bar_probe(struct recover *r, u32 bar, u32 raw, u32 mask,
    const struct recover_range *exclude, u32 exclude_count,
    const struct recover_range *upstream, u32 upstream_count);
u32 recover_profile_crc(const struct recover_profile *p);
void recover_init(struct recover *r, const struct recover_ops *ops, void *opaque,
                  bool enabled, bool route_write_approved);
int trial_begin(struct recover *r, const struct recover_profile *profile,
                u32 plan_hash, u32 budget);
/* approved means the caller has obtained the next-stage approval. */
int trial_step(struct recover *r, const struct recover_token *token, bool approved,
               int (*operation)(struct recover *r, void *arg), void *arg);
int recover_safe_point(struct recover *r);
int trial_end(struct recover *r, const struct recover_token *token,
              enum recover_reason reason);
int recover_request(struct recover *r, const struct recover_token *token,
                    enum recover_request request);
#endif
