/* cmd_recover.c — `recover status` / `recover now <session>` (realhw_recover T3)
 * kdebug_call の私有 op (kdebug/recover_wire.h) で状態を読み、now は指定 session
 * の復旧を頼む。ホスト側: tools/rshell_recover.py。試験: tools/tests/recover_cli_host.c */
#include "shell.h"
#include "../../kdebug/recover_wire.h"

#define RECOVER_KAPI_MIN 78U /* kdebug_call (slot 253) の版 */

int shell_cmd_recover(int argc, char **argv)
{
    KdebugRecover req = {0};
    u32 op = KDEBUG_OP_RECOVER_STATUS;
    if (argc == 3 && str_eq(argv[1], "now")) {
        const char *s = argv[2];
        if (!*s) goto usage;
        while (*s) {
            u32 digit = (u32)(*s++ - '0');
            if (digit > 9 || req.session > (0xffffffffU - digit) / 10U) goto usage;
            req.session = req.session * 10U + digit;
        }
        op = KDEBUG_OP_RECOVER_NOW;
    } else if (argc != 2 || !str_eq(argv[1], "status")) goto usage;
    if (g_api->version < RECOVER_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "recover: kdebug unavailable\n");
        return 1;
    }
    int rc = g_api->kdebug_call(op, &req, sizeof(req));
    if (rc) {
        g_api->kprintf(ATTR_RED, "recover: rejected rc=%d\n", rc);
        return 1;
    }
    g_api->kprintf(ATTR_WHITE,
        "recover sid=%u state=%u reason=%u verified=%u profile=%08X plan=%08X snapshot=%08X seq=%u lost=%u discarded=%u\n",
        req.session, req.state, req.reason, req.verified, req.profile_crc,
        req.plan_hash, req.snapshot_crc, req.seq, req.lost_lines, req.discarded_bytes);
    return 0;
usage:
    g_api->kprintf(ATTR_WHITE, "Usage: recover status | recover now <decimal-session>\n");
    return 2;
}
