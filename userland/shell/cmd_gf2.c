/* Shared CUI/GUI terminal entry for layer-2 serial acceptance. */
#include "shell.h"
#include "../../kdebug/gf_command_wire.h"
#include "../../hwdev/gf/feature5.h"

#define GF2_KAPI_MIN 78U

int shell_cmd_gf2(int argc, char **argv)
{
    static const char *const cases[] = {
        "status", "start", "reference", "stop", "recover", "cycle",
        "bad-handle", "bad-method", "vblank", "accel", "display", "feature"
    };
    static const char *const accel_cases[] = {
        "init", "fill", "copy", "mono", "ifc", "upload", "download",
        "scale", "present-m2mf", "all"
    };
    static const char *const feature_cases[] = {
        "info", "arg-lo", "arg-hi", "clock-read", "clock-calc", "clock-prog",
        "ram-prog", "perf-count", "perf-get", "perf-select", "therm",
        "fan-mode", "fan-get", "fan-cstate", "volt-get", "volt-set", "volt-id",
        "gpio-find", "gpio-get", "gpio-set", "tile-find", "tile-set", "tile-clear",
        "save", "restore", "free", "poll", "wait", "last-rc"
    };
    struct gf2_request q = {0};
    if (argc < 2) goto usage;
    if (str_eq(argv[1], "enter") || str_eq(argv[1], "leave")) {
        if (argc != 2) goto usage;
        q.test = GF4_DISPLAY;
        q.session = str_eq(argv[1], "enter") ? GF4_CUI_ENTER : GF4_CUI_LEAVE;
        goto dispatch;
    }
    for (q.test = 0; q.test <= GF5_FEATURE; q.test++)
        if (str_eq(argv[1], cases[q.test])) break;
    if (q.test > GF5_FEATURE) goto usage;
    if (q.test == GF5_FEATURE) {
        if (argc < 3) goto usage;
        for (q.session = 0; q.session <= GF5T_LAST_RC; ++q.session)
            if (str_eq(argv[2], feature_cases[q.session])) break;
        if (q.session > GF5T_LAST_RC) goto usage;
        if (q.session == GF5T_ARG_LO || q.session == GF5T_ARG_HI) {
            u32 operand[2] = {0, 0};
            if (argc != 5) goto usage;
            for (u32 i = 0; i < 2; ++i) {
                if (!argv[3+i][0]) goto usage;
                for (const char *digit = argv[3+i]; *digit; ++digit) {
                    if (*digit < '0' || *digit > '9' || operand[i] > GF5_WIRE_HALF_MASK / 10U)
                        goto usage;
                    operand[i] = operand[i] * 10U + (u32)(*digit - '0');
                    if (operand[i] > GF5_WIRE_HALF_MASK) goto usage;
                }
            }
            if (operand[0] >= GF5_WIRE_OPERANDS) goto usage;
            q.session |= operand[0] << GF5_WIRE_INDEX_SHIFT | operand[1] << GF5_WIRE_VALUE_SHIFT;
        } else if (argc != 3) goto usage;
    } else if (q.test == GF3_ACCEL) {
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
dispatch:
    if (g_api->version < GF2_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "gf2: rejected rc=%d kdebug unavailable\n", OS32_ERR_NOSYS);
        return q.test == GF5_FEATURE ? 77 : 1;
    }
    q.size = sizeof(q);
    int rc = g_api->kdebug_call(GF_OP_COMMAND, &q, sizeof(q));
    if (rc || q.rc)
        g_api->kprintf(ATTR_RED, "gf2: failed test=%u rc=%d reply_rc=%d\n", q.test, rc, q.rc);
    if (q.test == GF5_FEATURE) {
        int result = rc ? rc : q.rc;
        if (result == GF5_ENOTSUP || result == 77 || result == OS32_ERR_NOSYS) return 77;
    }
    return rc || q.rc ? 1 : 0;
usage:
    g_api->kprintf(ATTR_WHITE, "Usage: gf2 enter|leave|status|start|reference|stop|recover|cycle|bad-handle|bad-method|vblank|accel <init|fill|copy|mono|ifc|upload|download|scale|present-m2mf|all> | display <packed-session> | feature <info|arg-lo|arg-hi|clock-read|clock-calc|clock-prog|ram-prog|perf-count|perf-get|perf-select|therm|fan-mode|fan-get|fan-cstate|volt-get|volt-set|volt-id|gpio-find|gpio-get|gpio-set|tile-find|tile-set|tile-clear|save|restore|free|poll|wait|last-rc> [operand halfword]\n");
    return 2;
}
