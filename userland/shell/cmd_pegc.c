/* Shared CUI/GUI terminal command; diagnostics require an active PEGC GUI. */
#include "shell.h"
#include "../../kdebug/pegc_command_wire.h"
#define PEGC_KAPI_MIN 78U

static int decimal(const char *s, u32 *out)
{
    u32 n = 0;
    if (!*s) return 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return 0;
        u32 digit = (u32)(*s - '0');
        if (n > ((u32)-1 - digit) / 10U) return 0;
        n = n * 10U + digit;
    }
    *out = n;
    return 1;
}
int shell_cmd_pegc(int argc, char **argv)
{
    struct pegc_request req = {0};
    if (argc == 2 && str_eq(argv[1], "present")) req.test = PEGC_TEST_PRESENT;
    else if (argc == 6 && str_eq(argv[1], "gap") && str_eq(argv[2], "sweep")) {
        req.test = PEGC_TEST_GAP_SWEEP;
        if (!decimal(argv[3], &req.start_ns) || !decimal(argv[4], &req.end_ns) ||
            !decimal(argv[5], &req.step_ns) || !req.step_ns || req.start_ns > req.end_ns)
            goto usage;
    } else goto usage;
    if (g_api->version < PEGC_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "pegc: kdebug unavailable\n");
        return SH_STATUS_ERROR;
    }
    req.version = PEGC_COMMAND_VERSION;
    req.size = sizeof(req);
    int rc = g_api->kdebug_call(PEGC_OP_COMMAND, &req, sizeof(req));
    g_api->kprintf(rc || req.rc ? ATTR_RED : ATTR_WHITE,
                  "pegc: rc=%d reply_rc=%d result_count=%u\n", rc, req.rc, req.result_count);
    return rc || req.rc ? SH_STATUS_ERROR : SH_STATUS_OK;
usage:
    g_api->kprintf(ATTR_WHITE, "Usage: pegc present | pegc gap sweep <start> <end> <step>\n");
    return SH_STATUS_USAGE;
}
