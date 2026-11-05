/* HW_BASIC_DESIGN §§2-5,7: shell adapters and heap-owned load lifetime. */
#include "shell.h"
#include "hwb.h"
#include "pci_dev_contract.h"

#define HWB_KAPI_MIN 78U /* kdebug_call, includes v59 time and PCI enumeration */
static struct hwb_state hw_session;
static int hw_initialized;
static int hw_unavailable;
static struct hwb_program *hw_program;
static char *hw_source;

static int hw_access(void *ctx, struct hw_req *req)
{
    (void)ctx;
    int rc = g_api->kdebug_call(HW_OP, req, sizeof(*req));
    if (rc == OS32_ERR_NOSYS) hw_unavailable = 1;
    return rc;
}
static int hw_time(void *ctx, u32 *lo, u32 *hi)
{
    (void)ctx;
    return g_api->sys_time_now(lo, hi);
}
static void hw_yield(void *ctx) { (void)ctx; g_api->sys_yield(); }
static int hw_point(void *ctx, u32 *flag)
{
    struct hw_req req = {.version=HW_VERSION, .action=HW_POINT};
    int rc = hw_access(ctx, &req);
    if (rc) return rc;
    if (req.status != HW_OK) return OS32_ERR_IO;
    *flag = req.value;
    return 0;
}
static void hw_line(void *ctx, const char *line)
{
    (void)ctx;
    g_api->kprintf(ATTR_WHITE, "%s\n", line);
}
static int hw_read_file(void *ctx, const char *path, char *buf, u32 cap, u32 *bytes)
{
    (void)ctx;
    int fd = g_api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return fd;
    u32 used = 0;
    int rc = 0;
    for (;;) {
        char extra;
        int n = g_api->sys_read(fd, used < cap ? buf + used : &extra,
                               used < cap ? (int)(cap - used) : 1);
        if (n < 0) { rc = n; break; }
        if (!n) break;
        if (used == cap || (u32)n > cap - used) { rc = OS32_ERR_INVAL; break; }
        used += (u32)n;
    }
    g_api->sys_close(fd);
    if (!rc) *bytes = used;
    return rc;
}
static int hw_find_id(void *ctx, u32 vendor, u32 device, u32 start, u32 *bdfs, u32 cap)
{
    (void)ctx;
    int count = g_api->pci_count();
    if (count < 0) return count;
    u32 total = 0, stored = 0;
    for (int i = 0; i < count; i++) {
        struct pci_dev d;
        int rc = g_api->pci_get((u32)i, &d);
        if (rc) return rc;
        if (d.vendor != vendor || d.device != device) continue;
        if (total++ >= start && stored < cap) bdfs[stored++] = HW_BDF(d.bus, d.dev, d.fn);
    }
    return (int)total;
}
static const struct hwb_backend hw_backend = {
    0, hw_access, hw_time, hw_yield, hw_point, hw_line, hw_read_file, hw_find_id
};
static void hw_release(void)
{
    hw_session.program = 0;
    if (hw_program) g_api->mem_free(hw_program);
    if (hw_source) g_api->mem_free(hw_source);
    hw_program = 0; hw_source = 0;
}
/* Initial values are single decimal/hex u32 literals, no shell evaluation. */
static int hw_initial(const char *arg, char *name, u32 *value)
{
    u32 n = 0, base = 10, v = 0, digits = 0;
    while (*arg && *arg != '=') {
        char c = *arg++;
        if (n == HWB_NAME_CHARS || !((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || c == '_' || (n && c >= '0' && c <= '9'))) return -1;
        name[n++] = c;
    }
    if (!n || *arg++ != '=') return -1;
    name[n] = 0;
    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) { base = 16; arg += 2; }
    while (*arg) {
        char c = *arg++;
        u32 d = c >= '0' && c <= '9' ? (u32)(c - '0') :
                c >= 'a' && c <= 'f' ? (u32)(c - 'a' + 10) :
                c >= 'A' && c <= 'F' ? (u32)(c - 'A' + 10) : base;
        if (d >= base || v > (U32_MAX - d) / base) return -1;
        v = v * base + d; digits++;
    }
    if (!digits) return -1;
    *value = v;
    return 0;
}
int shell_cmd_hw(int argc, char **argv)
{
    if (argc < 2) return HWB_EXIT_ARG;
    if (g_api->version < HWB_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "hw: rejected rc=%d kdebug unavailable\n", OS32_ERR_NOSYS);
        return HWB_EXIT_NOSYS;
    }
    if (!hw_initialized) { hwb_init(&hw_session, &hw_backend); hw_initialized = 1; }
    hw_unavailable = 0;
    enum hwb_rc rc;
    if (str_eq(argv[1], "run") || str_eq(argv[1], "load")) {
        struct hwb_initial initial[HWB_VARIABLES];
        char names[HWB_VARIABLES][HWB_NAME_CHARS + 1U];
        if (argc < 3 || (u32)(argc - 3) > HWB_VARIABLES) return HWB_EXIT_ARG;
        for (int i = 3; i < argc; i++) {
            initial[i - 3].name = names[i - 3];
            if (hw_initial(argv[i], names[i - 3], &initial[i - 3].value)) return HWB_EXIT_ARG;
        }
        struct hwb_program *program = g_api->mem_alloc(sizeof(*program));
        char *source = g_api->mem_alloc(HWB_FILE_BYTES);
        if (!program || !source) {
            if (program) g_api->mem_free(program);
            if (source) g_api->mem_free(source);
            return HWB_EXIT_IO;
        }
        rc = hwb_load(&hw_session, program, source, HWB_FILE_BYTES, argv[2], initial, argc - 3);
        if (rc) { g_api->mem_free(program); g_api->mem_free(source); }
        else {
            /* hwb_load has already stopped a replaced run and installed fresh. */
            if (hw_program) g_api->mem_free(hw_program);
            if (hw_source) g_api->mem_free(hw_source);
            hw_program = program; hw_source = source;
            if (str_eq(argv[1], "run")) rc = hwb_continue(&hw_session);
        }
    } else if (str_eq(argv[1], "reset")) {
        if (argc != 2) return HWB_EXIT_ARG;
        hwb_reset(&hw_session); return HWB_EXIT_OK;
    } else if (str_eq(argv[1], "step") || str_eq(argv[1], "continue") || str_eq(argv[1], "abort")) {
        if (argc != 2 || !hw_program) return HWB_EXIT_ARG;
        if (str_eq(argv[1], "abort")) { hwb_abort(&hw_session); rc = hw_session.rc; }
        else rc = str_eq(argv[1], "step") ? hwb_step(&hw_session) : hwb_continue(&hw_session);
    } else {
        /* Shell removes quotes. Restore the literal operand for print/log;
         * joining other tokens retains HW expression/address grammar. */
        char line[HWB_LINE_BYTES + 1U]; u32 n = 0;
        for (int i = 1; i < argc; i++) {
            int quote = i == 2 && (str_eq(argv[1], "print") || str_eq(argv[1], "log"));
            if (i > 1) { if (n == HWB_LINE_BYTES) return HWB_EXIT_ARG; line[n++] = ' '; }
            if (quote) { if (n == HWB_LINE_BYTES) return HWB_EXIT_ARG; line[n++] = '"'; }
            for (const char *p = argv[i]; *p; p++) {
                if (quote && (*p == '"' || *p == '\\')) {
                    if (n == HWB_LINE_BYTES) return HWB_EXIT_ARG;
                    line[n++] = '\\';
                }
                if (n == HWB_LINE_BYTES) return HWB_EXIT_ARG;
                line[n++] = *p;
            }
            if (quote) { if (n == HWB_LINE_BYTES) return HWB_EXIT_ARG; line[n++] = '"'; }
        }
        line[n] = 0;
        rc = hwb_command(&hw_session, line, n);
    }
    if (hw_session.stopped) hw_release();
    if (hw_unavailable)
        g_api->kprintf(ATTR_RED, "hw: rejected rc=%d kdebug unavailable\n", OS32_ERR_NOSYS);
    return hwb_exit_status(hw_unavailable ? HWB_NOSYS : rc);
}
