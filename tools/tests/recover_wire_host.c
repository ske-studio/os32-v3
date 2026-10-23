/* Real dispatcher; the existing inert fixture counts every HAL dependency. */
#define _start kdebug_fixture_start
#define serial_get_setup fixture_serial_get_setup
#include "kdebug_init_host.c"
#undef serial_get_setup
static unsigned wire_attach_calls, io_reads, io_writes, io_bad;
void recover_test_outp(u32 port, u32 value)
{ io_writes++; if (port != RECOVER_ROUTE_PORT || value != RECOVER_ROUTE_98) io_bad++; }
u32 recover_test_inp(u32 port)
{ io_reads++; if (port != RECOVER_ROUTE_PORT) io_bad++; return 0xfc; }
const struct serial_setup *serial_get_setup(void)
{ wire_attach_calls++; return fixture_serial_get_setup(); }
#undef _start

STATIC_ASSERT(sizeof(KdebugRecoverVirtual) == 40, recover_virtual_wire_size);
static int wire_tests(void)
{
    KdebugRecover route = {0};
    static const struct recover_ops gf_ops = {0};
    owner.ops = &gf_ops;
    unsigned attach_before = wire_attach_calls;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_ROUTE_TEST, &route, sizeof(route)) != -1 ||
        wire_attach_calls != attach_before) return 1;
    if (io_reads || io_writes) return 1;
    owner.ops = 0;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_ROUTE_TEST, 0, sizeof(route)) != -1) return 1;
    const u32 route_sizes[] = {0, 39, 41, 48};
    for (u32 i = 0; i < sizeof(route_sizes)/sizeof(*route_sizes); i++)
        if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_ROUTE_TEST, &route, route_sizes[i]) != -1) return 1;
    route.session = 1;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_ROUTE_TEST, &route, sizeof(route)) != -1) return 1;
    route.session = 0; owner.token.session = 1;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_ROUTE_TEST, &route, sizeof(route)) != -1) return 1;
    owner.token.session = 0; owner.state = REC_PAUSED;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_ROUTE_TEST, &route, sizeof(route)) != -1) return 1;
    if (wire_attach_calls != attach_before) return 1;
    owner.state = REC_IDLE;
    /* A valid 40B request dispatches the dedicated entry and copies its result. */
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_ROUTE_TEST, &route, sizeof(route)) ||
        wire_attach_calls != attach_before + 1 || route_wire_calls != 1 ||
        route.session != 1 || route.state != REC_VERIFIED || route.verified != 1) return 1;
    if (io_reads || io_writes || !owner.ops || !owner.ops->route_write) return 1;
    if (owner.ops->route_write(0, 1) != -1 || io_writes) return 1;
    if (owner.ops->route_write(0, RECOVER_ROUTE_98) || io_writes != 1 || io_bad) return 1;
    u32 value;
    if (owner.ops->route_read(0, &value) || value != 0xfc || io_reads != 1 || io_bad) return 1;
    KdebugRecoverVirtual req = {.version = RECOVER_VIRTUAL_VERSION};
    for (u32 action = RECOVER_VIRTUAL_BEGIN; action <= RECOVER_VIRTUAL_END; action++) {
        req.action = action;
        for (u32 id = RECOVER_VIRTUAL_NORMAL; id <= RECOVER_VIRTUAL_TXBUSY_STOP; id++) {
            req.case_id = id;
            unsigned before=wire_attach_calls;
            if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, &req, sizeof(req)) != KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED ||
                wire_attach_calls != before+1) return 1;
        }
    }
    /* An unknown begin case is UNSUPPORTED and does not reach attach. */
    req.action = RECOVER_VIRTUAL_BEGIN;
    req.case_id = RECOVER_VIRTUAL_TXBUSY_STOP + 1;
    unsigned unknown_before = wire_attach_calls;
    if (kdebug_ops.dbg_call(KDEBUG_OP_RECOVER_VIRTUAL, &req, sizeof(req)) != KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED ||
        wire_attach_calls != unknown_before) return 1;
    /* Invalid requests must never reach attach, regardless of the shared -1. */
    req.action = RECOVER_VIRTUAL_BEGIN;
    req.case_id = RECOVER_VIRTUAL_TXBUSY_STOP;
    unsigned before=wire_attach_calls;
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
    if (wire_attach_calls != before) return 1;
    return 0;
}
void _start(void)
{
    int rc = wire_tests();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
