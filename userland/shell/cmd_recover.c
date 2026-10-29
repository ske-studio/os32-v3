/* cmd_recover.c — `recover status` / `recover now <session>` (realhw_recover T3)、
 * `recover virtual begin|step|end` (T4b)。kdebug_call の私有 op (kdebug/recover_wire.h)
 * で状態を読み、now は指定 session の復旧を頼む。ホスト側: tools/rshell_recover.py。試験: tools/tests/recover_cli_host.c */
#include "shell.h"
#include "../../kdebug/recover_wire.h"

#define RECOVER_KAPI_MIN 78U /* kdebug_call (slot 253) の版 */

static int recover_session(const char *s, u32 *out)
{
    *out = 0;
    if (!*s) return -1;
    while (*s) {
        u32 digit = (u32)(*s++ - '0');
        if (digit > 9 || *out > (0xffffffffU - digit) / 10U) return -1;
        *out = *out * 10U + digit;
    }
    return 0;
}

int shell_cmd_recover(int argc, char **argv)
{
    static const char *const cases[] = {
        [RECOVER_VIRTUAL_NORMAL] = "normal", [RECOVER_VIRTUAL_FAULT] = "fault",
        [RECOVER_VIRTUAL_TRIAL_EXPIRE] = "trial-expire", [RECOVER_VIRTUAL_POLL_STOP] = "poll-stop",
        [RECOVER_VIRTUAL_TXBUSY_STOP] = "txbusy-stop",
    };
    KdebugRecover req = {0};
    KdebugRecoverVirtual v = {.version = RECOVER_VIRTUAL_VERSION};
    void *buf = &req;
    u32 size = sizeof(req);
    u32 op = KDEBUG_OP_RECOVER_STATUS;
    if (argc == 2 && str_eq(argv[1], "restore")) {
        op = KDEBUG_OP_RECOVER_RESTORE;
    } else if (argc == 3 && str_eq(argv[1], "now")) {
        if (recover_session(argv[2], &req.session)) goto usage;
        op = KDEBUG_OP_RECOVER_NOW;
    } else if (argc == 4 && str_eq(argv[1], "route-test")) {
        if (!str_eq(argv[2], "0") || !str_eq(argv[3], "--approve")) goto usage;
        op = KDEBUG_OP_RECOVER_ROUTE_TEST;
    } else if (argc == 4 && str_eq(argv[1], "virtual")) {
        if (str_eq(argv[2], "begin")) {
            while (v.case_id < sizeof(cases) / sizeof(*cases) && !str_eq(argv[3], cases[v.case_id]))
                v.case_id++;
            if (v.case_id == sizeof(cases) / sizeof(*cases)) goto usage;
            v.action = RECOVER_VIRTUAL_BEGIN;
        } else {
            if (str_eq(argv[2], "step")) v.action = RECOVER_VIRTUAL_STEP;
            else if (str_eq(argv[2], "end")) v.action = RECOVER_VIRTUAL_END;
            else goto usage;
            if (recover_session(argv[3], &v.session)) goto usage;
        }
        op = KDEBUG_OP_RECOVER_VIRTUAL;
        buf = &v;
        size = sizeof(v);
    } else if (argc != 2 || !str_eq(argv[1], "status")) goto usage;
    if (g_api->version < RECOVER_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "recover: kdebug unavailable\n");
        return 1;
    }
    int rc = g_api->kdebug_call(op, buf, size);
    if (rc) {
        g_api->kprintf(ATTR_RED, "recover: rejected rc=%d\n", rc);
        return 1;
    }
    if (op == KDEBUG_OP_RECOVER_VIRTUAL) return 0;
    g_api->kprintf(ATTR_WHITE,
        "recover sid=%u state=%u reason=%u verified=%u profile=%08X plan=%08X snapshot=%08X seq=%u lost=%u discarded=%u\n",
        req.session, req.state, req.reason, req.verified, req.profile_crc,
        req.plan_hash, req.snapshot_crc, req.seq, req.lost_lines, req.discarded_bytes);
    return 0;
usage:
    g_api->kprintf(ATTR_WHITE, "Usage: recover route-test 0 --approve | recover status | recover restore | recover now <decimal-session> | recover virtual begin <normal|fault|trial-expire|poll-stop|txbusy-stop> | recover virtual step|end <session>\n");
    return 2;
}
