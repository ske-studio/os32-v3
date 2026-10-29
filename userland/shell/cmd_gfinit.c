/* cmd_gfinit.c — `gfinit inspect <sid>` / `gfinit size-bars|prepare|modeset|verify|abort <sid> --allow-write` /
 * `gfinit prepare <sid> --mode-c --allow-write` / `gfinit run-one <sid> <main> --allow-write` (GF6200_S7_SHELL S7a/S7c)。
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

#define GF_VERIFY_LINE_MAX 64U
static int gfinit_verify_line(char *line, struct gfinit_prepare_request *p)
{
    char *kind = line;
    while (*kind == ' ' || *kind == '\t' || *kind == '\r') kind++;
    if (!*kind) return 0;
    char *reg = kind;
    while (*reg && *reg != ' ' && *reg != '\t') reg++;
    if (!*reg) return -1;
    *reg++ = 0;
    while (*reg == ' ' || *reg == '\t') reg++;
    u32 type, value = 0, digits = 0;
    if (str_eq(kind, "MMIO")) type = GF_VERIFY_MMIO;
    else if (str_eq(kind, "CR")) type = GF_VERIFY_CR;
    else if (str_eq(kind, "SR")) type = GF_VERIFY_SR;
    else return -1;
    if (reg[0] == '0' && (reg[1] == 'x' || reg[1] == 'X')) reg += 2;
    while (*reg) {
        u32 digit;
        if (*reg >= '0' && *reg <= '9') digit = *reg - '0';
        else if (*reg >= 'a' && *reg <= 'f') digit = *reg - 'a' + 10U;
        else if (*reg >= 'A' && *reg <= 'F') digit = *reg - 'A' + 10U;
        else break;
        if (value > (~0U - digit) / 16U) return -1;
        value = value * 16U + digit; digits++; reg++;
    }
    while (*reg == ' ' || *reg == '\t' || *reg == '\r') reg++;
    if (!digits || *reg || (type != GF_VERIFY_MMIO && value > 255U) ||
        p->count == GF_NO_VERIFY_MAX) return -1;
    p->regs[p->count++] = (struct gfinit_no_verify){type, value};
    return 0;
}
static int gfinit_verify_file(const char *path, struct gfinit_prepare_request *p)
{
    int fd = g_api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return -1;
    char line[GF_VERIFY_LINE_MAX];
    u32 used = 0;
    int rc = 0, comment = 0;
    for (;;) {
        char ch;
        int n = g_api->sys_read(fd, &ch, 1);
        if (n < 0) { rc = -1; break; }
        if (!n || ch == '\n') {
            line[used] = 0;
            if (gfinit_verify_line(line, p)) { rc = -1; break; }
            used = 0; comment = 0;
            if (!n) break;
        } else if (ch == '#') comment = 1;
        else if (!comment) {
            if (used == sizeof(line) - 1) { rc = -1; break; }
            line[used++] = ch;
        }
    }
    g_api->sys_close(fd);
    return rc;
}

int shell_cmd_gfinit(int argc, char **argv)
{
    struct gfinit_prepare_request payload = {0};
    struct gfinit_request q = {0};
    const char *verify_file = 0;
    u32 op;
    if (argc == 3 && str_eq(argv[1], "inspect")) op = GF_OP_INSPECT;
    else {
        int run_one = argc >= 2 && str_eq(argv[1], "run-one");
        int mode_c = argc >= 5 && str_eq(argv[1], "prepare") && str_eq(argv[3], "--mode-c");
        int expected = run_one || mode_c ? 5 : 4;
        if (mode_c) {
            for (int i = 4; i < argc; i++) {
                if (str_eq(argv[i], "--allow-write")) {
                    if (q.flags & GF_ALLOW_WRITE) goto usage;
                    q.flags |= GF_ALLOW_WRITE;
                } else if (str_eq(argv[i], "--verify") && i + 1 < argc &&
                           str_eq(argv[i + 1], "required") && !(q.flags & GF_VERIFY_REQUIRED)) {
                    q.flags |= GF_VERIFY_REQUIRED; i++;
                } else if (str_eq(argv[i], "--no-verify-file") && i + 1 < argc && !verify_file) {
                    verify_file = argv[++i]; q.flags |= GF_NO_VERIFY_FILE;
                } else goto usage;
            }
            if (!(q.flags & GF_ALLOW_WRITE)) goto usage;
            expected = argc;
        }
        if (!mode_c && (argc != expected || !str_eq(argv[argc - 1], "--allow-write"))) goto usage;
        if (str_eq(argv[1], "size-bars")) op = GF_OP_SIZE_BARS;
        else if (str_eq(argv[1], "prepare")) op = GF_OP_PREPARE;
        else if (run_one) {
            op = GF_OP_RUN_ONE;
            if (gfinit_session(argv[3], &q.script_id) ||
                q.script_id >= GF_INIT_MAINS) goto usage;
        } else if (str_eq(argv[1], "modeset")) op = GF_OP_MODESET;
        else if (str_eq(argv[1], "verify")) op = GF_OP_VERIFY;
        else if (str_eq(argv[1], "abort")) op = GF_OP_ABORT;
        else goto usage;
        q.flags |= GF_ALLOW_WRITE;
        if (mode_c) q.flags |= GF_MODE_C;
    }
    if (gfinit_session(argv[2], &q.session)) goto usage;
    if (g_api->version < GFINIT_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "gfinit: kdebug unavailable\n");
        return 1;
    }
    q.version = GF_INIT_VERSION;
    q.size = sizeof(q);
    q.max_steps = GF_INIT_SCHEDULE;
    int rc;
    if (verify_file) {
        if (gfinit_verify_file(verify_file, &payload)) {
            g_api->kprintf(ATTR_RED, "gfinit: invalid no-verify file (MMIO/CR/SR hex, max %u entries)\n", GF_NO_VERIFY_MAX);
            return 2;
        }
        q.size = sizeof(payload); payload.request = q;
        rc = g_api->kdebug_call(op, &payload, sizeof(payload));
        q = payload.request;
    } else rc = g_api->kdebug_call(op, &q, sizeof(q));
    if (op == GF_OP_INSPECT)
        g_api->kprintf(ATTR_WHITE, "GF inspect-end sid=%u rc=%d\n", q.session, rc);
    else if (rc)
        g_api->kprintf(ATTR_RED, "gfinit: rejected rc=%d\n", rc);
    return rc ? 1 : 0;
usage:
    g_api->kprintf(ATTR_WHITE, "Usage: gfinit inspect <sid> | gfinit {size-bars|prepare|modeset|verify|abort} <sid> --allow-write | gfinit prepare <sid> --mode-c --allow-write [--verify required] [--no-verify-file <path>] | gfinit run-one <sid> <main:0-%u> --allow-write\n",
                   GF_INIT_MAINS - 1U);
    return 2;
}
