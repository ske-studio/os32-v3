/* Shared CUI/GUI terminal entry for layer-2 serial acceptance. */
#include "shell.h"
#include "../../kdebug/gf_command_wire.h"

#define GF2_KAPI_MIN 78U

int shell_cmd_gf2(int argc, char **argv)
{
    static const char *const cases[] = {
        "status", "start", "reference", "stop", "recover", "cycle",
        "bad-handle", "bad-method", "vblank", "accel", "display"
    };
    static const char *const accel_cases[] = {
        "init", "fill", "copy", "mono", "ifc", "upload", "download",
        "scale", "present-m2mf", "all"
    };
    struct gf2_request q = {0};
    if (argc < 2) goto usage;
    for (q.test = 0; q.test <= GF4_DISPLAY; q.test++)
        if (str_eq(argv[1], cases[q.test])) break;
    if (q.test > GF4_DISPLAY) goto usage;
    if (q.test == GF3_ACCEL) {
        if (argc != 3) goto usage;
        for (q.session = 0; q.session < sizeof(accel_cases)/sizeof(accel_cases[0]); q.session++)
            if (str_eq(argv[2], accel_cases[q.session])) break;
        if (q.session == sizeof(accel_cases)/sizeof(accel_cases[0])) goto usage;
    } else if (q.test == GF4_DISPLAY) {
        /* Packed session: case[7:0], connector[15:8], head[16], phase[17]. */
        if (argc != 3 || !argv[2][0]) goto usage;
        for (const char *digit = argv[2]; *digit; ++digit) {
            if (*digit < '0' || *digit > '9' || q.session > GF4_DISPLAY_SESSION_MAX / 10U)
                goto usage;
            q.session = q.session * 10U + (u32)(*digit - '0');
            if (q.session > GF4_DISPLAY_SESSION_MAX) goto usage;
        }
    } else if (argc != 2) goto usage;
    if (g_api->version < GF2_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "gf2: rejected rc=%d kdebug unavailable\n", OS32_ERR_NOSYS);
        return 1;
    }
    q.size = sizeof(q);
    int rc = g_api->kdebug_call(GF_OP_COMMAND, &q, sizeof(q));
    return rc || q.rc ? 1 : 0;
usage:
    g_api->kprintf(ATTR_WHITE, "Usage: gf2 status|start|reference|stop|recover|cycle|bad-handle|bad-method|vblank|accel <init|fill|copy|mono|ifc|upload|download|scale|present-m2mf|all> | display <packed-session>\n");
    return 2;
}
