#ifndef KDEBUG_GFINIT_WIRE_H
#define KDEBUG_GFINIT_WIRE_H
/* Private kdebug_call wire; include after u32 is defined (GNU89 compatible). */
#define GF_INIT_MAINS 5U
#define GF_INIT_SCHEDULE 4096U
#define GF_INIT_VERSION 1U
#define GF_ALLOW_WRITE 1U
#define GF_OP_INSPECT 5U
#define GF_OP_SIZE_BARS 6U
#define GF_OP_PREPARE 7U
#define GF_OP_RUN_ONE 8U
#define GF_OP_VERIFY 9U
#define GF_OP_ABORT 10U
struct gfinit_request {
    u32 version, size, flags, session, script_id, max_steps;
    u32 status, fail_pc, fail_reg, steps;
};
#endif
