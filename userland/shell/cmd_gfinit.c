/* cmd_gfinit.c — `gfinit inspect <sid>` / `gfinit size-bars|prepare|verify|abort <sid> --allow-write` /
 * `gfinit run-one <sid> <main> --allow-write` (GF6200_S7_SHELL S7a/S7c)。
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
    else {
        int run_one = argc >= 2 && str_eq(argv[1], "run-one");
        int expected = run_one ? 5 : 4;
        if (argc != expected || !str_eq(argv[argc - 1], "--allow-write")) goto usage;
        if (str_eq(argv[1], "size-bars")) op = GF_OP_SIZE_BARS;
        else if (str_eq(argv[1], "prepare")) op = GF_OP_PREPARE;
        else if (run_one) {
            op = GF_OP_RUN_ONE;
            if (gfinit_session(argv[3], &q.script_id) ||
                q.script_id >= GF_INIT_MAINS) goto usage;
        } else if (str_eq(argv[1], "verify")) op = GF_OP_VERIFY;
        else if (str_eq(argv[1], "abort")) op = GF_OP_ABORT;
        else goto usage;
        q.flags = GF_ALLOW_WRITE;
    }
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
    g_api->kprintf(ATTR_WHITE, "Usage: gfinit inspect <sid> | gfinit {size-bars|prepare|verify|abort} <sid> --allow-write | gfinit run-one <sid> <main:0-%u> --allow-write\n",
                   GF_INIT_MAINS - 1U);
    return 2;
}
