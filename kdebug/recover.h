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
#define RECOVER_HALF_TICKS 0x80000000UL
#define RECOVER_ROUTE_PORT 0x0FACU
#define RECOVER_ROUTE_MASK 0x03U
#define RECOVER_ROUTE_98 0U
#define RECOVER_COMMAND_MEM 0x0002U

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

/* Actual address spaces/widths and hardware policy belong to T2 profiles.
 * key is an opaque bus descriptor, never a pointer. Only classified safe
 * reads may enter this table. Mixed W1C, indexed, trigger etc. need T2's
 * specialized accessors; they must not masquerade as REC_RW.
 */
struct recover_reg {
    u32 key, width, write_mask, verify_mask;
    enum recover_kind kind;
    bool read_safe;
};
struct recover_write { u32 key, width, value, verify_mask; };
struct recover_identity {
    u32 boot, image_version, kdebug_version, bdf, device, subsys, revision, bar0;
};
struct recover_profile {
    u32 version, crc;
    const struct recover_reg *regs; /* restoration order, not reverse order */
    u32 count;
    const struct recover_write *quiesce;
    u32 quiesce_count;
    u32 trial_limit, opcode_limit, restore_limit, max_steps, max_polls;
    bool cold_required;
};
struct recover_token {
    u32 session, profile_crc, profile_version, snapshot_crc, plan_hash;
    struct recover_identity identity;
};
struct recover;
struct recover_ops {
    u32 (*ticks)(void *opaque);
    int (*identity)(void *opaque, struct recover_identity *out);
    int (*start_ready)(void *opaque); /* stopped device AND confirmed CUI */
    int (*raw_config)(void *opaque, u32 *command, u32 *bar0);
    int (*read)(void *opaque, u32 key, u32 width, u32 *value);
    int (*write)(void *opaque, u32 key, u32 width, u32 value);
    int (*route_read)(void *opaque, u32 *value); /* 0FACh byte */
    int (*route_write)(void *opaque, u32 value);
    /* stop output/DMA, release AND verify decode independently of MMIO.
     * Failure prohibits display/VRAM accesses. Shared HAL implementation T2. */
    int (*screen_safe)(void *opaque);
    int (*screen_restore_verify)(void *opaque);
    int (*abort_requested)(void *opaque, u32 session); /* bounded serial poll */
    int (*event)(void *opaque, enum recover_state state, u32 session);
};
struct recover {
    const struct recover_ops *ops;
    void *opaque;
    const struct recover_profile *profile;
    struct recover_token token;
    u32 saved[RECOVER_MAX_REGS], saved_count;
    u32 next_session, budget, spent, call_tick, opcode_tick, restore_tick;
    u32 steps, polls, first_key;
    enum recover_state state;
    enum recover_reason reason;
    enum recover_phase first_phase;
    bool enabled, route_write_approved, valid, in_call, in_opcode;
    bool failed, access_blocked, cold_latched, s0_verified, s1_verified;
};

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
