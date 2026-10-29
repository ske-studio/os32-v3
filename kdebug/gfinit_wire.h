#ifndef KDEBUG_GFINIT_WIRE_H
#define KDEBUG_GFINIT_WIRE_H
/* Private kdebug_call wire; include after u32 is defined (GNU89 compatible). */
#define GF_INIT_MAINS 5U
#define GF_INIT_SCHEDULE 4096U
#define GF_INIT_VERSION 1U
#define GF_ALLOW_WRITE 1U
#define GF_MODE_C 2U
#define GF_VERIFY_REQUIRED 8U
#define GF_NO_VERIFY_FILE 16U
#define GF_VERIFY_RECORD 32U
#define GF_NO_VERIFY_MAX 64U
enum gfinit_verify_kind { GF_VERIFY_MMIO, GF_VERIFY_CR, GF_VERIFY_SR };
struct gfinit_no_verify { u32 kind, reg; };
#define GF_OP_INSPECT 5U
#define GF_OP_SIZE_BARS 6U
#define GF_OP_PREPARE 7U
#define GF_OP_RUN_ONE 8U
#define GF_OP_VERIFY 9U
#define GF_OP_ABORT 10U
#define GF_OP_MODESET 13U
#define GF_OP_C_ONLY 14U
struct gfinit_request {
    u32 version, size, flags, session, script_id, max_steps;
    u32 status, fail_pc, fail_reg, steps;
};
/* Optional PREPARE payload only; the original 40-byte/version-1 wire stays valid. */
struct gfinit_prepare_request {
    struct gfinit_request request;
    u32 count;
    struct gfinit_no_verify regs[GF_NO_VERIFY_MAX];
};
#endif
