#ifndef KDEBUG_GFINIT_H
#define KDEBUG_GFINIT_H
#include "recover.h"
/* Private kdebug_call contract. S7 publishes the shell/SDK spelling. */
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
STATIC_ASSERT(sizeof(struct gfinit_request) == 40, gfinit_request_size);
/* Release inspection storage when an external recover request terminates it. */
void gfinit_recover_end(void);
int gfinit_call(u32 op, void *buf, u32 size, const struct recover_ops *base,
                struct recover_transport *transport, const struct recover_profile *s0);
#endif
