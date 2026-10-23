/* Real dispatcher; the existing inert fixture counts every HAL dependency. */
#define _start kdebug_fixture_start
#include "kdebug_init_host.c"
#undef _start

STATIC_ASSERT(sizeof(KdebugRecoverVirtual) == 40, recover_virtual_wire_size);
static int wire_tests(void)
{
    KdebugRecoverVirtual req = {.version = RECOVER_VIRTUAL_VERSION};
    for (u32 action = RECOVER_VIRTUAL_BEGIN; action <= RECOVER_VIRTUAL_END; action++) {
        req.action = action;
        for (u32 id = RECOVER_VIRTUAL_NORMAL; id <= RECOVER_VIRTUAL_TXBUSY_STOP; id++) {
            req.case_id = id;
            /* -1 for begin normal, fault or trial-expire / step / end comes from recovery_attach failing
             * in this inert fixture, not from the wire check. */
            int expected = action == RECOVER_VIRTUAL_BEGIN &&
                id != RECOVER_VIRTUAL_NORMAL && id != RECOVER_VIRTUAL_FAULT &&
                id != RECOVER_VIRTUAL_TRIAL_EXPIRE ?
                KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED : -1;
            if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, &req, sizeof(req)) !=
                expected) return 1;
        }
    }
    /* A well-formed begin poll-stop returns UNSUPPORTED, so each malformed variant
     * below must be rejected by the wire check itself. */
    req.action = RECOVER_VIRTUAL_BEGIN;
    req.case_id = RECOVER_VIRTUAL_POLL_STOP;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, &req, sizeof(req)) != KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED) return 1;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, 0, sizeof(req)) != KDEBUG_RECOVER_VIRTUAL_INVALID) return 1;
    const u32 sizes[] = {0, 39, 41};
    for (u32 i = 0; i < sizeof(sizes)/sizeof(*sizes); i++)
        if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, &req, sizes[i]) != KDEBUG_RECOVER_VIRTUAL_INVALID) return 1;
    req.version = 0;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, &req, sizeof(req)) != KDEBUG_RECOVER_VIRTUAL_INVALID) return 1;
    req.version = RECOVER_VIRTUAL_VERSION + 1;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, &req, sizeof(req)) != KDEBUG_RECOVER_VIRTUAL_INVALID) return 1;
    req.version = RECOVER_VIRTUAL_VERSION;
    for (u32 i = 0; i < sizeof(req.reserved)/sizeof(*req.reserved); i++) {
        req.reserved[i] = 1;
        if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, &req, sizeof(req)) != KDEBUG_RECOVER_VIRTUAL_INVALID) return 1;
        req.reserved[i] = 0;
    }
    owner.state = REC_PAUSED;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, &req, sizeof(req)) != -1) return 1;
    return 0;
}
void _start(void)
{
    int rc = wire_tests();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
