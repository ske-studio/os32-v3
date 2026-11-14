/* Fake KAPI at the shell boundary. No product source is replaced in baseline. */
#include "shell.h"
#include "../../include/remote_control.h"
#include "../../include/con_sink.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static KernelAPI api;
KernelAPI *g_api = &api;
static u32 tick, gen = 7, drops, body_len, reply_pos;
static unsigned int ready_count, out_count, executes, begins, ends, closes, yields;
static int active, frozen, pending, handle, is_out, open_error, notice_error;
static unsigned int writes;
static int accept_status = 200, accept_ack;
static int is_accept;
static int stall, partial, freeze_error, stat_error, bad_reply, begin_error;
static HostNotice notice;
static char capture[REMOTE_RESULT_BODY_MAX], body[REMOTE_RESULT_BODY_MAX];
static u32 capture_len, capture_pos;
static char request[REMOTE_NOTIFY_DATA_MAX], ready_request[REMOTE_NOTIFY_DATA_MAX];
static char response[REMOTE_NOTIFY_DATA_MAX];

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static u32 now(void) { return tick; }
static i32 yield(void) { ++tick; ++yields; return 0; }
static void boot(u8 *out) { for (int i = 0; i < 16; ++i) out[i] = (u8)(i * 17); }
static i32 open_host(const char *req, u32 len)
{
    CHECK(!handle);
    CHECK(len < sizeof(request));
    memcpy(request, req, len); request[len] = 0;
    if (open_error) return open_error;
    handle = 1;
    is_accept = !strncmp(req, REMOTE_REQ_RSH_ACCEPT, strlen(REMOTE_REQ_RSH_ACCEPT));
    is_out = is_accept || !strncmp(req, REMOTE_REQ_RSH_OUT, strlen(REMOTE_REQ_RSH_OUT));
    reply_pos = 0;
    if (is_out) { ++out_count; body_len = 0; response[0] = 0; }
    else {
        ++ready_count;
        strcpy(ready_request, request);
        if (bad_reply) strcpy(response, "gen 7 svc 65533");
        else snprintf(response, sizeof(response), "gen %u svc %u", (unsigned int)gen, REMOTE_SVC_RSHELL);
    }
    return 3;
}
static i32 close_host(i32 h) { CHECK(h == 3 && handle); handle = 0; ++closes; return 0; }
static i32 status_host(i32 h, u32 *st, u32 *len)
{
    CHECK(h == 3 && handle);
    if (stall == 2) return OS32_ERR_AGAIN;
    *st = is_accept ? (u32)accept_status : 200;
    if (is_accept && *st == 200) accept_ack = 1;
    *len = strlen(response); return 0;
}
static i32 read_host(i32 h, void *buf, u32 cap)
{
    CHECK(h == 3 && handle);
    if (stall == 3) return OS32_ERR_AGAIN;
    u32 n = strlen(response) - reply_pos;
    if (n > cap) n = cap;
    if (partial && n > 2) n = 2;
    memcpy(buf, response + reply_pos, n); reply_pos += n;
    return (i32)n;
}
static i32 write_host(i32 h, const void *buf, u32 len)
{
    CHECK(h == 3 && handle && active && frozen && is_out);
    CHECK(len <= 1400);
    if (stall == 1) return OS32_ERR_AGAIN;
    if (partial && (++writes % 2 == 0)) return OS32_ERR_AGAIN;
    if (partial && len > 3) len = 3;
    CHECK(body_len + len <= sizeof(body));
    memcpy(body + body_len, buf, len); body_len += len;
    return (i32)len;
}
static int read_notice(u32 generation, u32 service, HostNotice *out)
{
    CHECK(generation == gen && service == REMOTE_SVC_RSHELL);
    if (notice_error) return notice_error;
    if (!pending) return OS32_ERR_AGAIN;
    *out = notice; pending = 0; return 0;
}
static int capture_op(u32 op)
{
    if (op == CON_SINK_CAPTURE_BEGIN) {
        if (begin_error) return begin_error;
        CHECK(!active && !handle); active = 1; frozen = 0;
        capture_len = capture_pos = 0; ++begins;
    } else if (op == CON_SINK_CAPTURE_FREEZE) {
        CHECK(active); frozen = 1;
        if (freeze_error) return freeze_error;
        return drops ? OS32_ERR_NOSPC : 0;
    } else {
        CHECK(op == CON_SINK_CAPTURE_END && active);
        active = 0; ++ends;
    }
    return 0;
}
static i32 stat_sink(u32 *count, u32 *dropped)
{
    (void)count;
    if (stat_error) return OS32_ERR_IO;
    *dropped = frozen ? drops : 0; return 0;
}
static i32 read_sink(void *buf, u32 cap)
{
    CHECK(active && frozen && cap >= CON_SINK_REC_MAX);
    if (capture_pos == capture_len) return 0;
    /* Each call returns a single complete PRINT record. */
    u32 n = capture_len - capture_pos;
    if (n > CON_SINK_PRINT_MAX) n = CON_SINK_PRINT_MAX;
    u8 *p = buf; p[0] = CON_SINK_REC_PRINT; p[1] = 7; p[2] = (u8)n;
    memcpy(p + CON_SINK_HDR_PRINT, capture + capture_pos, n);
    capture_pos += n; return (i32)(n + CON_SINK_HDR_PRINT);
}
static void print(u8 attr, const char *fmt, ...)
{
    (void)attr;
    CHECK(active && !frozen);
    va_list args; va_start(args, fmt);
    int n = vsnprintf(capture + capture_len, sizeof(capture) - capture_len, fmt, args);
    va_end(args); CHECK(n >= 0); capture_len += (u32)n;
}
static void quiet(void) { CHECK(active); }
int execute_command(const char *line)
{
    CHECK(active && !frozen && !handle);
    ++executes;
    if (!strcmp(line, "big")) {
        while (capture_len < REMOTE_RESULT_BODY_MAX - 1) capture[capture_len++] = 'x';
    } else print(7, "%s", "ok\n");
    return 0;
}
int rsh_lan_execute(const char *line) { (void)execute_command(line); return 0; }
int rsh_lan_handoff(const char *line) { (void)line; CHECK(0); return 1; }
int sh_refused_take(void) { return 0; }
static void queue(unsigned int id, const char *line)
{
    memset(&notice, 0, sizeof(notice));
    notice.host_generation = gen; notice.service = REMOTE_SVC_RSHELL;
    notice.kind = REMOTE_NOTIFY_EXECUTE;
    notice.length = (unsigned short)snprintf((char *)notice.data, sizeof(notice.data), "%08x %s\n", id, line);
    pending = 1;
}
static void next_ready(void) { tick += 2 * OS32_TICK_HZ; }
static void output(unsigned int id, unsigned int drop)
{
    char expected[REMOTE_NOTIFY_DATA_MAX];
    snprintf(expected, sizeof(expected), "RSH OUT 00112233445566778899aabbccddeeff %08x %u %u", id, drop, (unsigned int)body_len);
    CHECK(!strcmp(request, expected));
    CHECK(body_len && body[body_len - 1] == 4);
    CHECK(!active && !handle && begins == ends);
}
int main(void)
{
    api.get_tick = now; api.sys_yield = yield; api.boot_token_get = boot;
    api.host_open = open_host; api.host_close = close_host;
    api.host_status = status_host; api.host_read = read_host; api.host_write = write_host;
    api.host_notice_read = read_notice; api.con_sink_capture = capture_op;
    api.con_sink_stat = stat_sink; api.con_sink_read = read_sink;
    api.kprintf = print; api.buz_off = quiet;
    partial = 1;
    queue(1, "echo hi"); CHECK(rsh_lan_poll() == 1); output(1, 0);
    CHECK(!strcmp(ready_request, "RSH READY 00112233445566778899aabbccddeeff cui 1 none idle"));
    CHECK(body_len == 14 && !memcmp(body, "> echo hi\nok\n\004", body_len));
    /* Above includes exact echo/body/EOT, not a terminating NUL. */
    queue(1, "echo twice"); CHECK(rsh_lan_poll() == OS32_ERR_EXIST); CHECK(executes == 1);
    queue(0, "echo old"); CHECK(rsh_lan_poll() == OS32_ERR_EXIST); CHECK(executes == 1);
    queue(2, "echo drop"); drops = 5;
    CHECK(rsh_lan_poll() == 1); output(2, 5); drops = 0;
    queue(99, "never"); notice.kind = REMOTE_NOTIFY_PROBE;
    CHECK(rsh_lan_poll() == 0); CHECK(executes == 2);
    CHECK(strstr(ready_request, "00000002 done"));
    queue(3, "bad\nline"); CHECK(rsh_lan_poll() == OS32_ERR_INVAL);
    queue(3, "bad\033"); CHECK(rsh_lan_poll() == OS32_ERR_INVAL);
    queue(3, "echo good"); begin_error = OS32_ERR_BUSY;
    CHECK(rsh_lan_poll() == OS32_ERR_BUSY); CHECK(executes == 2); begin_error = 0;
    /* INVAL invalidates READY and backs off until the next announcement. */
    notice_error = OS32_ERR_INVAL; CHECK(rsh_lan_poll() == OS32_ERR_INVAL);
    CHECK(rsh_lan_poll() == OS32_ERR_AGAIN);
    notice_error = 0;
    next_ready();
    /* Same generation across reconnect preserves the ID. */
    notice_error = OS32_ERR_STALE; CHECK(rsh_lan_poll() == OS32_ERR_STALE);
    notice_error = 0; next_ready(); queue(2, "duplicate");
    CHECK(rsh_lan_poll() == OS32_ERR_EXIST); CHECK(executes == 2);
    ++gen; next_ready(); queue(1, "new generation");
    CHECK(rsh_lan_poll() == 1); output(1, 0);
    CHECK(strstr(ready_request, "none idle"));
    /* AGAIN/STALE retries only after two seconds. */
    next_ready(); open_error = OS32_ERR_AGAIN;
    CHECK(rsh_lan_poll() == OS32_ERR_AGAIN);
    unsigned int count = ready_count;
    CHECK(rsh_lan_poll() == OS32_ERR_AGAIN && ready_count == count);
    next_ready(); open_error = OS32_ERR_STALE; CHECK(rsh_lan_poll() == OS32_ERR_STALE);
    next_ready(); open_error = 0; CHECK(rsh_lan_poll() == OS32_ERR_AGAIN);
    CHECK(ready_count == count + 1);
    /* Full result capacity plus EOT, and partial writes. */
    queue(2, "big"); CHECK(rsh_lan_poll() == 1); output(2, 0);
    CHECK(body_len == REMOTE_RESULT_BODY_MAX);
    /* Timed out writes close and END; they never rerun a command. */
    queue(3, "timeout"); stall = 1;
    u32 start = tick; CHECK(rsh_lan_poll() == OS32_ERR_TIMEOUT);
    CHECK(tick - start == OS32_TICK_HZ && !active && !handle && begins == ends);
    stall = 0; next_ready(); queue(3, "again"); CHECK(rsh_lan_poll() == OS32_ERR_EXIST);
    for (int mode = 2; mode <= 3; ++mode) {
        next_ready(); stall = mode; start = tick;
        CHECK(rsh_lan_poll() == OS32_ERR_TIMEOUT);
        CHECK(tick - start == OS32_TICK_HZ && !handle); stall = 0;
    }
    next_ready(); bad_reply = 1; CHECK(rsh_lan_poll() == OS32_ERR_IO); bad_reply = 0;
    next_ready(); queue(4, "exit"); CHECK(rsh_lan_poll() == RSH_LAN_EXIT); output(4, 0);
    CHECK(body_len == 1 && !active && !handle && begins == ends);
    queue(5, "freeze error"); freeze_error = OS32_ERR_IO;
    CHECK(rsh_lan_poll() == OS32_ERR_IO && !active && begins == ends); freeze_error = 0;
    queue(6, "stat error"); stat_error = 1;
    CHECK(rsh_lan_poll() == OS32_ERR_IO && !active && begins == ends); stat_error = 0;
    CHECK(yields && closes);
    puts("PASS remote X10 fake KAPI lifecycle");
    return 0;
}
