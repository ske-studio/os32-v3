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
#endif
