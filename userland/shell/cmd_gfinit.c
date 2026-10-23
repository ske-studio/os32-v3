/* cmd_gfinit.c — `gfinit inspect <sid>` / `gfinit abort <sid> --allow-write` (GF6200_S7_SHELL S7a)。
 * kdebug_call の私有 op (kdebug/gfinit_wire.h) を呼ぶ。試験: tools/tests/gfinit_owner_host.c */
#include "shell.h"
#include "../../kdebug/gfinit_wire.h"

#define GFINIT_KAPI_MIN 78U /* kdebug_call (slot 253) の版 */

static int gfinit_session(const char *s, u32 *out)
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

int shell_cmd_gfinit(int argc, char **argv)
{
    struct gfinit_request q = {0};
    u32 op;
    if (argc == 3 && str_eq(argv[1], "inspect")) op = GF_OP_INSPECT;
    else if (argc == 4 && str_eq(argv[1], "abort") &&
             str_eq(argv[3], "--allow-write")) {
        op = GF_OP_ABORT;
        q.flags = GF_ALLOW_WRITE;
    } else goto usage;
    if (gfinit_session(argv[2], &q.session)) goto usage;
    if (g_api->version < GFINIT_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "gfinit: kdebug unavailable\n");
        return 1;
    }
    q.version = GF_INIT_VERSION;
    q.size = sizeof(q);
    q.max_steps = GF_INIT_SCHEDULE;
    int rc = g_api->kdebug_call(op, &q, sizeof(q));
    if (op == GF_OP_INSPECT)
        g_api->kprintf(ATTR_WHITE, "GF inspect-end sid=%u rc=%d\n", q.session, rc);
    else if (rc)
        g_api->kprintf(ATTR_RED, "gfinit: rejected rc=%d\n", rc);
    return rc ? 1 : 0;
usage:
    g_api->kprintf(ATTR_WHITE, "Usage: gfinit inspect <sid> | gfinit abort <sid> --allow-write\n");
    return 2;
}
