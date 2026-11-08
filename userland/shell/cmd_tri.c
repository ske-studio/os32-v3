/* Shared CUI/GUI terminal entry for Trident diagnostics. */
#include "shell.h"
#include "../../kdebug/tri_command_wire.h"

/* kdebug_call became available at this KAPI version (cmd_gf2.c). */
#define TRI_KAPI_MIN 78U

static int tri_number(const char *s, u32 *out)
{
    if (!*s) return 0;
    u32 n = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return 0;
        u32 digit = (u32)(*s - '0');
        if (n > ((u32)-1 - digit) / 10U) return 0;
        n = n * 10U + digit;
    }
    *out = n;
    return 1;
}
int shell_cmd_tri(int argc, char **argv)
{
    static const char *const cases[] = {
        [TRI_TEST_ALL] = "all",
        [TRI_TEST_MODES] = "modes",
        [TRI_TEST_MODE] = "mode",
        [TRI_TEST_DPMS] = "dpms",
        [TRI_TEST_CURSOR] = "cursor",
        [TRI_TEST_CPU32] = "cpu32",
        [TRI_TEST_POLL] = "poll",
        [TRI_TEST_PRESENT] = "present"
    };
    struct tri_request req = {0};

    if (argc < 2) goto usage;
    if (str_eq(argv[1], "gap")) {
        req.test = TRI_TEST_GAP;
        if (argc == 3) {
            req.test = TRI_TEST_GAP_SET;
            if (!tri_number(argv[2], &req.arg)) goto usage;
        } else if (argc == 6 && str_eq(argv[2], "sweep")) {
            req.test = TRI_TEST_GAP_SWEEP;
            if (!tri_number(argv[3], &req.arg) ||
                !tri_number(argv[4], &req.end_ns) ||
                !tri_number(argv[5], &req.step_ns) ||
                !req.step_ns || req.arg > req.end_ns) goto usage;
        } else if (argc != 2) goto usage;
        goto dispatch;
    }
    for (req.test = 0; req.test < sizeof(cases) / sizeof(cases[0]); ++req.test)
        if (str_eq(argv[1], cases[req.test])) break;
    if (req.test == sizeof(cases) / sizeof(cases[0])) goto usage;
    if (req.test == TRI_TEST_MODE) {
        if (argc != 3 || !argv[2][0]) goto usage;
        for (const char *digit = argv[2]; *digit; ++digit) {
            if (*digit < '0' || *digit > '9') goto usage;
            u32 value = (u32)(*digit - '0');
            if (req.arg > ((u32)-1 - value) / 10U) goto usage;
            req.arg = req.arg * 10U + value;
        }
        if (!req.arg) goto usage;
    } else if (argc != 2) goto usage;

dispatch:
    if (g_api->version < TRI_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "tri: rejected rc=%d result_count=0 kdebug unavailable\n",
                       OS32_ERR_NOSYS);
        return SH_STATUS_ERROR;
    }
    req.version = TRI_COMMAND_VERSION;
    req.size = sizeof(req);
    int rc = g_api->kdebug_call(TRI_OP_COMMAND, &req, sizeof(req));
    g_api->kprintf(rc || req.rc ? ATTR_RED : ATTR_WHITE,
                   "tri: test=%u rc=%d reply_rc=%d result_count=%u\n",
                   req.test, rc, req.rc, req.result_count);
    return rc || req.rc ? SH_STATUS_ERROR : SH_STATUS_OK;
usage:
    g_api->kprintf(ATTR_WHITE,
                   "Usage: tri all|modes|mode <number>|dpms|cursor|cpu32|poll|present|gap [<ns>|sweep <start> <end> <step>]\n");
    return SH_STATUS_USAGE;
}
