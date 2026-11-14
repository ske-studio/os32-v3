/* LAN_OPERATOR_DESIGN §§2–3, X10. Called by the foreground at a line boundary. */
#include "shell.h"
#include "../../include/remote_control.h"
#include "../../include/con_sink.h"
#include "../lib/host/libos32host.h"
#include <stdio.h>

#define RSH_BOOT_BYTES 16
#define RSH_READY_TICKS (2u * OS32_TICK_HZ)
#define RSH_STALL_TICKS OS32_TICK_HZ /* 1000ms without progress */
#define RSH_TOTAL_TICKS (HOST_CALL_TIMEOUT_MS / 1000u * OS32_TICK_HZ)

static u32 generation, last_id, ready_tick;
static int have_id, ready, attempted;
static const char *state = REMOTE_STATE_IDLE;
static u8 result[REMOTE_RESULT_BODY_MAX];
static u32 result_len;
static int accepting;

static void boot_text(char *out)
{
    static const char hex[] = "0123456789abcdef";
    u8 token[RSH_BOOT_BYTES];
    g_api->boot_token_get(token);
    for (unsigned int i = 0; i < sizeof(token); i++) {
        out[2 * i] = hex[token[i] >> 4];
        out[2 * i + 1] = hex[token[i] & 15];
    }
    out[2 * sizeof(token)] = 0;
}

static int wait_again(int rc, u32 start, u32 progress)
{
    if (rc != 0 && rc != OS32_ERR_AGAIN) return rc;
    u32 now = g_api->get_tick();
    if ((u32)(now - progress) >= RSH_STALL_TICKS ||
        (u32)(now - start) >= RSH_TOTAL_TICKS) return OS32_ERR_TIMEOUT;
    g_api->sys_yield();
    return 0;
}

/* No handle is held while executing. Close on every error, including timeout. */
static int exchange(const char *req, const u8 *body, u32 len,
                    char *reply, u32 cap)
{
    u32 start = g_api->get_tick(), progress = start;
    u32 sent = 0, got = 0, status = 0, length = 0;
    int h = g_api->host_open(req, (u32)strlen(req)), rc;
    if (h < 0) return h;
    while (sent < len) {
        u32 chunk = len - sent;
        if (chunk > HOST_WRITE_CHUNK) chunk = HOST_WRITE_CHUNK;
        rc = g_api->host_write(h, body + sent, chunk);
        if (rc > 0 && (u32)rc <= chunk) {
            sent += (u32)rc;
            progress = g_api->get_tick();
        } else {
            if (rc > 0) rc = OS32_ERR_IO;
            rc = wait_again(rc, start, progress);
            if (rc) goto close;
        }
        if ((u32)(g_api->get_tick() - start) >= RSH_TOTAL_TICKS) {
            rc = OS32_ERR_TIMEOUT;
            goto close;
        }
    }
    for (;;) {
        rc = g_api->host_status(h, &status, &length);
        if (!rc) break;
        rc = wait_again(rc, start, progress);
        if (rc) goto close;
    }
    if (status != 200 || length >= cap) { rc = OS32_ERR_IO; goto close; }
    progress = g_api->get_tick();
    while (got < length) {
        rc = g_api->host_read(h, reply + got, length - got);
        if (rc > 0 && (u32)rc <= length - got) {
            got += (u32)rc;
            progress = g_api->get_tick();
        } else {
            if (!rc || rc > 0) rc = OS32_ERR_IO;
            rc = wait_again(rc, start, progress);
            if (rc) goto close;
        }
        if ((u32)(g_api->get_tick() - start) >= RSH_TOTAL_TICKS) {
            rc = OS32_ERR_TIMEOUT;
            goto close;
        }
    }
    reply[got] = 0;
    rc = 0;
close:
    g_api->host_close(h);
    return rc;
}

static int decimal(const char **text, u32 *value)
{
    const char *p = *text;
    u32 v = 0;
    if (*p < '0' || *p > '9') return OS32_ERR_IO;
    do {
        u32 d = (u32)(*p++ - '0');
        if (v > (0xffffffffu - d) / 10u) return OS32_ERR_IO;
        v = v * 10u + d;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *value = v;
    return 0;
}

static int send_ready(void)
{
    char boot[RSH_BOOT_BYTES * 2 + 1], id[REMOTE_ID8_HEX_DIGITS + 1];
    char req[REMOTE_NOTIFY_DATA_MAX], reply[REMOTE_NOTIFY_DATA_MAX];
    const char *p;
    u32 gen, svc;
    boot_text(boot);
    if (have_id) snprintf(id, sizeof(id), "%08x", (unsigned int)last_id);
    else snprintf(id, sizeof(id), "%s", REMOTE_STATE_NONE);
    snprintf(req, sizeof(req), "%s %s %s 1 %s %s", REMOTE_REQ_RSH_READY,
             boot, REMOTE_MODE_CUI, id, state);
    attempted = 1;
    ready_tick = g_api->get_tick();
    int rc = exchange(req, 0, 0, reply, sizeof(reply));
    if (rc) { ready = 0; return rc; }
    p = reply;
    if (strncmp(p, "gen ", 4)) goto bad;
    p += 4;
    if (decimal(&p, &gen) || !gen || strncmp(p, " svc ", 5)) goto bad;
    p += 5;
    if (decimal(&p, &svc) || svc != REMOTE_SVC_RSHELL || (*p && strcmp(p, "\n"))) goto bad;
    if (generation != gen) {
        generation = gen;
        have_id = 0;
        state = REMOTE_STATE_IDLE;
        /* Publish the reset state before taking work from the new generation. */
        if (strcmp(id, REMOTE_STATE_NONE)) { ready = 0; return send_ready(); }
    }
    ready = 1;
    return 0;
bad:
    ready = 0;
    return OS32_ERR_IO;
}

int rsh_lan_flush(unsigned int id, unsigned int dropped)
{
    char boot[RSH_BOOT_BYTES * 2 + 1], req[REMOTE_NOTIFY_DATA_MAX];
    char reply[REMOTE_NOTIFY_DATA_MAX];
    boot_text(boot);
    if (accepting) {
        if (dropped) return OS32_ERR_NOSPC;
        snprintf(req, sizeof(req), "%s %s %08x %u", REMOTE_REQ_RSH_ACCEPT,
                 boot, id, (unsigned int)result_len);
    } else snprintf(req, sizeof(req), "%s %s %08x %u %u", REMOTE_REQ_RSH_OUT,
                    boot, id, dropped, (unsigned int)result_len);
    return exchange(req, result, result_len, reply, sizeof(reply));
}

/* Only our reserved service is consumed. The known generation survives exit;
 * a changed generation already discards its old mailbox entries in the kernel. */
void rsh_lan_reset(void)
{
    HostNotice notice;
    unsigned int discarded = 0;
    if (generation && g_api->host_notice_read) {
        while (!g_api->host_notice_read(generation, REMOTE_SVC_RSHELL, &notice))
            discarded++;
    }
    ready = attempted = 0;
    if (discarded)
        g_api->kprintf(ATTR_YELLOW, "rshell: discarded %u stale LAN notices\n", discarded);
}

static int command(const HostNotice *n, u32 *id, char *line)
{
    const unsigned int prefix = REMOTE_ID8_HEX_DIGITS + 1;
    u32 value = 0;
    if (n->length <= prefix + 1 || n->length > prefix + REMOTE_RSHELL_COMMAND_MAX + 1 ||
        n->data[REMOTE_ID8_HEX_DIGITS] != ' ' || n->data[n->length - 1] != '\n')
        return OS32_ERR_INVAL;
    for (unsigned int i = 0; i < REMOTE_ID8_HEX_DIGITS; i++) {
        unsigned int c = n->data[i], digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return OS32_ERR_INVAL;
        value = (value << 4) | digit;
    }
    for (unsigned int i = prefix; i < n->length - 1u; i++) {
        if (n->data[i] < ' ' || n->data[i] > '~') return OS32_ERR_INVAL;
        line[i - prefix] = (char)n->data[i];
    }
    line[n->length - prefix - 1] = 0;
    *id = value;
    return 0;
}

static int collect(void)
{
    u8 records[CON_SINK_REC_MAX];
    result_len = 0;
    for (;;) {
        int n = g_api->con_sink_read(records, sizeof(records));
        if (n <= 0) return n;
        for (unsigned int pos = 0; pos < (unsigned int)n;) {
            u32 size, head = 0, text = 0;
            switch (records[pos]) {
            case CON_SINK_REC_PRINT:
                if ((unsigned int)n - pos < CON_SINK_HDR_PRINT) return OS32_ERR_IO;
                head = CON_SINK_HDR_PRINT;
                text = records[pos + 2];
                size = head + text;
                break;
            case CON_SINK_REC_CLEAR: size = CON_SINK_HDR_CLEAR; break;
            case CON_SINK_REC_CURSOR: size = CON_SINK_HDR_CURSOR; break;
            case CON_SINK_REC_EXIT: size = CON_SINK_HDR_EXIT; break;
            default: return OS32_ERR_IO;
            }
            if (size > (unsigned int)n - pos || text > sizeof(result) - 1 - result_len)
                return OS32_ERR_NOSPC;
            if (text) memcpy(result + result_len, records + pos + head, text);
            result_len += text;
            pos += size;
        }
    }
}

int rsh_lan_poll(void)
{
    HostNotice notice;
    char line[REMOTE_RSHELL_COMMAND_MAX + 1];
    u32 id, before, after;
    int rc, handoff = 0, exit_requested = 0;
    accepting = 0;
    if (!attempted || (u32)(g_api->get_tick() - ready_tick) >= RSH_READY_TICKS) {
        rc = send_ready();
        if (rc) return rc;
    }
    if (!ready) return OS32_ERR_AGAIN;
    rc = g_api->host_notice_read(generation, REMOTE_SVC_RSHELL, &notice);
    if (rc) {
        if (rc == OS32_ERR_STALE || rc == OS32_ERR_INVAL) ready = 0;
        return rc;
    }
    if (notice.host_generation != generation || notice.service != REMOTE_SVC_RSHELL)
        return OS32_ERR_STALE;
    if (notice.kind == REMOTE_NOTIFY_PROBE) return send_ready();
    if (notice.kind != REMOTE_NOTIFY_EXECUTE) return OS32_ERR_INVAL;
    rc = command(&notice, &id, line);
    if (rc) return rc;
    if (have_id && id <= last_id) return OS32_ERR_EXIST;
    rc = g_api->con_sink_capture(CON_SINK_CAPTURE_BEGIN);
    if (rc) return rc;
    rc = g_api->con_sink_stat(0, &before);
    if (rc) goto end;
    last_id = id;
    have_id = 1;
    state = REMOTE_STATE_RUNNING;
    exit_requested = str_eq(line, "exit");
    if (!exit_requested) {
        g_api->buz_off();
        g_api->kprintf(ATTR_YELLOW, "%s", "> ");
        g_api->kprintf(ATTR_WHITE, "%s", line);
        g_api->kprintf(ATTR_WHITE, "%s", "\n");
        accepting = rsh_lan_execute(line);
        (void)sh_refused_take();
    }
    state = REMOTE_STATE_DONE;
finish:
    rc = g_api->con_sink_capture(CON_SINK_CAPTURE_FREEZE);
    if (rc && rc != OS32_ERR_NOSPC) goto end;
    int frozen = rc;
    rc = g_api->con_sink_stat(0, &after);
    if (rc) goto end;
    u32 dropped = after - before;
    if (frozen == OS32_ERR_NOSPC && !dropped) dropped = 1;
    rc = collect();
    if (rc) goto end;
    result[result_len++] = 0x04;
    rc = rsh_lan_flush((unsigned int)id, (unsigned int)dropped);
    if (rc) ready = 0;
    else handoff = accepting;
end:
    g_api->con_sink_capture(CON_SINK_CAPTURE_END);
    if (handoff) {
        int status = rsh_lan_handoff(line);
        ready = 0;
        attempted = 0; /* Immediate CUI READY if the launch returned. */
        accepting = handoff = 0;
        /* A successful switch exits the shell. A return reports failure via
         * the existing OUT contract, without submitting/executing gui again. */
        rc = g_api->con_sink_capture(CON_SINK_CAPTURE_BEGIN);
        if (rc) return rc;
        rc = g_api->con_sink_stat(0, &before);
        if (rc) goto end;
        g_api->kprintf(ATTR_WHITE, "> %s\n", line);
        g_api->kprintf(ATTR_RED, "os32gui: launch failed (rc=%d)\n", status);
        goto finish;
    }
    if (exit_requested) return RSH_LAN_EXIT;
    return rc ? rc : 1;
}
