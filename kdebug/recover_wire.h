#ifndef KDEBUG_RECOVER_WIRE_H
#define KDEBUG_RECOVER_WIRE_H
/* Internal kdebug_call payload: no KAPI slot or shared ABI generation change.
 * Include after the caller's u32 definition; usable by GNU89 clients. */
#define KDEBUG_OP_RECOVER_STATUS 3U
#define KDEBUG_OP_RECOVER_NOW 4U
typedef struct KdebugRecover {
    u32 session, state, reason, verified;
    u32 profile_crc, plan_hash, snapshot_crc, seq, lost_lines, discarded_bytes;
} KdebugRecover;
#define KDEBUG_OP_RECOVER_VIRTUAL 11U
#define RECOVER_VIRTUAL_VERSION 1U
#define RECOVER_VIRTUAL_BEGIN 0U
#define RECOVER_VIRTUAL_STEP 1U
#define RECOVER_VIRTUAL_END 2U
#define RECOVER_VIRTUAL_NORMAL 0U
#define RECOVER_VIRTUAL_FAULT 1U
#define RECOVER_VIRTUAL_TRIAL_EXPIRE 2U
#define RECOVER_VIRTUAL_POLL_STOP 3U
#define RECOVER_VIRTUAL_TXBUSY_STOP 4U
#define KDEBUG_RECOVER_VIRTUAL_INVALID (-1)
#define KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED (-2)
typedef struct KdebugRecoverVirtual {
    u32 version, action, session, case_id, reserved[6];
} KdebugRecoverVirtual;
#endif
