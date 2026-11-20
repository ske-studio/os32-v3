/* Real X10 transport plus real X11 arbitration/dispatch, fake KAPI only. */
#define main x10_main
#define execute_command x10_execute
#define rsh_lan_execute x10_dispatch
#define rsh_lan_handoff x10_handoff
#include "remote_x10_host.c"
#undef main
#undef execute_command
#undef rsh_lan_execute
#undef rsh_lan_handoff
int execute_command(const char *line);
#include "../../userland/shell/rshell.c"

static int trace_open(const char *path, int flags) { (void)path; (void)flags; return -1; }
static unsigned int gui_runs, serial_reads, sfs_begins, sfs_ends;
static int mounted, shell_status, serial_phase, serial_pos, serial_eot;
static char serial_echo[1024];
static unsigned int echo_len;
static int lan_exit, resident_active;
static int boot_check, boot_remote, boot_runs, init_calls;
static int uart_available = 1, local_escape, child_status, begin_rc;
static int port_status(u32 *mode, u32 *baud, u32 *fifo)
{
    (void)mode; (void)baud; (void)fifo;
    return uart_available ? 0 : OS32_ERR_NOSYS;
}
int sh_in_sfs;
void sh_status_set(int n) { shell_status = n; }
static void boot_init(u32 baud) { CHECK(baud == SYS_SERIAL_BAUD && uart_available); ++init_calls; }
static void boot_terminal(const char *startup_notice)
{
    /* startup from ui.c */
}
static void shell_print(u8 attr, const char *fmt, ...)
{
    char text[256];
    va_list ap; va_start(ap, fmt); vsnprintf(text, sizeof(text), fmt, ap); va_end(ap);
    if (boot_check) {
        CHECK(init_calls == uart_available);
        CHECK(!strcmp(text, "startup") && resident_active == boot_remote && !boot_runs);
        return;
    }
    if (active) print(attr, "%s", text);
    else {
        CHECK(echo_len + strlen(text) < sizeof(serial_echo));
        strcpy(serial_echo + echo_len, text); echo_len += strlen(text);
    }
}
static void buzz(void) {}
static int shell_capture(u32 op)
{
    int rc = capture_op(op);
    if (op == CON_SINK_CAPTURE_END && lan_exit)
        queue(36, "stale during exit");
    return rc;
}
static int serial_get(void)
{
    CHECK(!active); ++serial_reads;
    if (lan_exit && serial_reads == 1) { CHECK(!pending); queue(35, "exit"); return -1; }
    if (lan_exit) return "exit\n"[serial_pos++]; /* Bound a lost-return mutant. */
    if (serial_phase == 0) {
        /* LAN arrives after the first serial character, with gaps in the line. */
        const char *s = "echo serial\n";
        if (serial_pos == 1 && !pending) queue(20, "echo lan");
        if (serial_pos < (int)strlen(s)) {
            if (serial_pos == 2) { serial_pos++; return -1; }
            /* compensate for the deliberate gap, keeping all input bytes */
            int i = serial_pos > 2 ? serial_pos - 1 : serial_pos;
            ++serial_pos; return s[i];
        }
        if (serial_pos == (int)strlen(s)) { ++serial_pos; return '\n'; }
        serial_phase = 1; return -1;
    }
    if (serial_phase == 1) { CHECK(!pending); serial_phase = 2; serial_pos = 0; }
    return "exit\n"[serial_pos++];
}
static int local_get(void) { return local_escape ? 27 : -1; }
static int serial_ok(void) { CHECK(uart_available); return 1; }
static void rs_active(int n)
{
    if (boot_check) CHECK(init_calls == uart_available);
    if (!n && lan_exit) {
        CHECK(!active && !handle && begins == ends && body_len == 1 && body[0] == 4);
    }
    resident_active = n;
}
static int serial_put(u8 c) { CHECK(!active); if (c == 4) ++serial_eot; return KAPI_SER_TX_OK; }
static void halt(void) { ++tick; }
static int is_mounted(const char *p) { CHECK(!strcmp(p, "/host")); return mounted; }
static const char *cwd(void) { return "/"; }
static int chdir_fake(const char *p) { (void)p; return 0; }
static int sfs_begin(void) { CHECK(active && mounted); ++sfs_begins; return begin_rc; }
static int sfs_end(int n) { CHECK(active && n == child_status); ++sfs_ends; return 0; }
void sh_refuse(const char *s, int n) { (void)s; (void)n; }
int execute_command(const char *line)
{
    CHECK(line);
    if (boot_check) {
        CHECK(boot_remote && !strcmp(line, "rshell"));
        ++boot_runs;
        return 0;
    }
    if (!strncmp(line, "os32gui", 7)) {
        CHECK(!active && !handle && accept_ack); ++gui_runs; return SH_STATUS_ERROR;
    }
    if (!strcmp(line, "echo serial")) {
        CHECK(!active && pending); ++executes; return 0;
    }
    CHECK(active && !frozen && !handle); ++executes; return sh_in_sfs ? child_status : 0;
}
int main(void)
{
    api.version = KAPI_VERSION;
    api.sys_open = trace_open;
    api.get_tick = now; api.sys_yield = yield; api.sys_halt = halt;
    api.boot_token_get = boot; api.host_open = open_host; api.host_close = close_host;
    api.host_status = status_host; api.host_read = read_host; api.host_write = write_host;
    api.host_notice_read = read_notice; api.con_sink_capture = shell_capture;
    api.con_sink_stat = stat_sink; api.con_sink_read = read_sink;
    api.kprintf = shell_print; api.buz_off = buzz;
    api.serial_trygetchar = serial_get; api.kbd_trygetchar_local = local_get;
    api.serial_get_status = port_status;
    api.serial_is_initialized = serial_ok; api.serial_putchar = serial_put;
    api.rshell_set_active = rs_active; api.sys_is_mounted = is_mounted;
    api.sys_getcwd = cwd; api.sys_chdir = chdir_fake;
    api.sfs_begin = sfs_begin; api.sfs_end = sfs_end;
    api.serial_init = boot_init;
    boot_check = 1;
    for (int mask = 0; mask < 4; ++mask) {
        uart_available = mask & 1;
        api.host_notice_read = (mask & 2) ? read_notice : 0;
        resident_active = boot_runs = init_calls = 0;
        boot_remote = mask != 0;
        int eots_before_start = serial_eot;
        boot_terminal("startup");
        CHECK(boot_runs == boot_remote && init_calls == uart_available);
        CHECK(resident_active == boot_remote && serial_eot == eots_before_start);
    }
    boot_check = resident_active = 0;
    char *argv[] = { "rshell" };
    CHECK(cmd_rshell_resident(1, argv) == 0);
    CHECK(executes == 2 && serial_eot == 3 && !pending);
    CHECK(strstr(serial_echo, "> echo serial\n") && !strstr(serial_echo, "> exit"));
    unsigned int reads = serial_reads;
    for (int i = 0; i < 3; i++) {
        const char *cmd = i == 0 ? "gui" : i == 1 ? "os32gui" : "os32gui -t 30";
        unsigned int previous = out_count;
        accept_ack = 0; queue(21 + i, cmd); CHECK(rsh_lan_poll() == 1);
        CHECK(out_count == previous + 2); /* ACCEPT and one final OUT. */
        CHECK(gui_runs == (unsigned int)i + 1 && accept_ack);
        CHECK(!strncmp(request, "RSH OUT ", 8));
        CHECK(body[body_len - 1] == 4 && strstr(body, "os32gui: launch failed"));
    }
    accept_status = 400; accept_ack = 0; queue(24, "gui");
    CHECK(rsh_lan_poll() == OS32_ERR_IO && gui_runs == 3 && !active);
    accept_status = 200; next_ready(); queue(24, "gui");
    CHECK(rsh_lan_poll() == OS32_ERR_EXIST && gui_runs == 3);
    queue(25, "sfs run echo no"); CHECK(rsh_lan_poll() == 1);
    CHECK(!sfs_begins && shell_status == SH_STATUS_USAGE);
    mounted = 1; queue(26, "sfs run echo yes"); CHECK(rsh_lan_poll() == 1);
    CHECK(sfs_begins == 1 && sfs_ends == 1);
    for (int i = 0; i < 5; i++) {
        const char *cmds[] = {"serial 9600", "terminal", "send hello", "tvdump", "rshell"};
        unsigned int count = executes; queue(27 + i, cmds[i]); CHECK(rsh_lan_poll() == 1);
        CHECK(executes == count);
    }
    unsigned int count = executes;
    queue(32, "rshell lantrace"); CHECK(rsh_lan_poll() == 1);
    CHECK(executes == count + 1);
    CHECK(serial_reads == reads && begins == ends && !active && !handle);
    lan_exit = 1; serial_reads = serial_pos = 0;
    int eots = serial_eot;
    queue(33, "stale before reopen");
    CHECK(cmd_rshell_resident(1, argv) == 0);
    CHECK(!resident_active && !rsh_in_rshell && !pending && serial_reads == 1);
    CHECK(strstr(serial_echo, "discarded 1 stale LAN notices"));
    CHECK(serial_eot == eots + 1); /* Initial prompt only; LAN owns the exit EOT. */
    queue(34, "stale after exit");
    rsh_lan_reset();
    CHECK(!pending);
    lan_exit = 0;
    begin_rc = OS32_ERR_BUSY;
    unsigned int runs = executes;
    queue(37, "sfs run echo busy"); CHECK(rsh_lan_poll() == 1);
    CHECK(shell_status == SH_STATUS_ERROR && executes == runs && sfs_ends == 1);
    begin_rc = 0;
    child_status = 139;
    queue(38, "sfs run fault"); CHECK(rsh_lan_poll() == 1);
    CHECK(shell_status == 139 && sfs_ends == 2 && !sh_in_sfs && !active && begins == ends);
    local_escape = 1;
    api.host_notice_read = 0;
    serial_pos = 0;
    CHECK(cmd_rshell_resident(1, argv) == 0 && !resident_active);
    uart_available = 0;
    reads = serial_reads; eots = serial_eot;
    CHECK(cmd_rshell_resident(1, argv) == SH_STATUS_ERROR && !resident_active);
    api.host_notice_read = read_notice;
    CHECK(cmd_rshell_resident(1, argv) == 0 && !resident_active);
    CHECK(serial_reads == reads && serial_eot == eots);
    open_error = OS32_ERR_NOSYS;
    CHECK(rsh_lan_poll() == OS32_ERR_NOSYS);
    open_error = 0; next_ready(); notice_error = OS32_ERR_NOSYS;
    CHECK(rsh_lan_poll() == OS32_ERR_AGAIN);
    count = ready_count; next_ready();
    CHECK(rsh_lan_poll() == OS32_ERR_AGAIN && ready_count == count);
    puts("GREEN X11: serial half-line, LAN ownership, ACCEPT/200/END, scope, serial echo/EOT/exit");
    return 0;
}
