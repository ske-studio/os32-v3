/* Foreground-only reserved NOTIFY diagnostics. No command runs in link_tick. */
#include "lan_boot.h"
#include "boot_token.h"
#include "remote_control.h"
#include "os32/os32_kapi_shared.h"
#include "sys/kern/kstring.h"

static i32 handle = -1;
static u32 generation, last_id, result_id, written, reply_bytes;
static u8 peer[6], reply[64];
static int have_id, ready, attempted, pending, ready_request;
static u64 ready_at;
static struct pm_diag_message result;

static char *number(char *p, u32 value)
{
    char digits[10];
    u32 n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) *p++ = digits[--n];
    return p;
}
static char *hex(char *p, const u8 *bytes, u32 n)
{
    static const char digits[] = "0123456789abcdef";
    while (n--) { *p++ = digits[*bytes >> 4]; *p++ = digits[*bytes++ & 15]; }
    return p;
}
static char *id_text(char *p, u32 id)
{
    u8 bytes[4] = {id >> 24, id >> 16, id >> 8, id};
    return hex(p, bytes, 4);
}
static int digit(u8 c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* Body: id8 SP decimal-opcode [SP BE-argument-hex] LF. */
static int decode(const HostNotice *n, u32 *id, struct pm_diag_message *q)
{
    u32 pos = 9, op = 0, digits = 0, end = n->length;
    *id = 0;
    if (end < 8 || end > sizeof(n->data)) return 0;
    for (u32 i = 0; i < 8; i++) {
        int v = digit(n->data[i]);
        if (v < 0) return 0;
        *id = (*id << 4) | (u32)v;
    }
    if (end < 11 || n->data[8] != ' ' || n->data[end - 1] != '\n') return 0;
    --end;
    while (pos < end && n->data[pos] >= '0' && n->data[pos] <= '9') {
        op = op * 10 + n->data[pos++] - '0';
        if (++digits > 5 || op > 65535) return 0;
    }
    if (!digits) return 0;
    q->opcode = (u16)op;
    q->payload_bytes = 0;
    if (pos == end) return 1;
    if (n->data[pos++] != ' ' || pos == end || (end - pos) % 2) return 0;
    while (pos < end) {
        int hi = digit(n->data[pos++]), lo = digit(n->data[pos++]);
        if (hi < 0 || lo < 0) return 0;
        q->payload[q->payload_bytes++] = (u8)((hi << 4) | lo);
    }
    return 1;
}
static void close_request(void)
{
    if (handle >= 0) (void)link_host_close(handle, 0);
    handle = -1; written = reply_bytes = 0;
}
static int decimal(const u8 **p, const u8 *end, u32 *out)
{
    u32 n = 0;
    if (*p == end || **p < '0' || **p > '9') return 0;
    while (*p < end && **p >= '0' && **p <= '9') {
        u32 d = *(*p)++ - '0';
        if (n > (0xffffffffu - d) / 10) return 0;
        n = n * 10 + d;
    }
    *out = n; return 1;
}
static int ready_reply(void)
{
    const u8 *p = reply + 4, *end = reply + reply_bytes;
    u32 gen, svc;
    if (reply_bytes < 14 || memcmp(reply, "gen ", 4) ||
        !decimal(&p, end, &gen) || !gen || end - p < 5 || memcmp(p, " svc ", 5)) return 0;
    p += 5;
    if (!decimal(&p, end, &svc) || svc != REMOTE_SVC_PPC_DIAG ||
        (p != end && !(end - p == 1 && *p == '\n'))) return 0;
    return gen == generation;
}
static void open_request(int is_ready, u64 now, u32 hz)
{
    char request[128], *p = request;
    u8 boot[16];
    boot_token_get(boot);
    const char *prefix = is_ready ? REMOTE_REQ_RSH_READY : REMOTE_REQ_PDIAG_OUT;
    while (*prefix) *p++ = *prefix++;
    *p++ = ' '; p = hex(p, boot, 16); *p++ = ' ';
    if (is_ready) {
        const char *mode = "diag 1 ";
        while (*mode) *p++ = *mode++;
        if (have_id) p = id_text(p, last_id);
        else { memcpy(p, "none", 4); p += 4; }
        const char *state = have_id ? " done" : " idle";
        while (*state) *p++ = *state++;
        attempted = 1; ready_at = now + (u64)hz * 2;
        ready = 0;
    } else {
        p = id_text(p, result_id); *p++ = ' '; p = number(p, result.status);
        *p++ = ' '; p = number(p, result.payload_bytes);
    }
    ready_request = is_ready;
    handle = link_host_open(request, (u32)(p - request), 0);
}
void pm_link_diag_poll(const struct pm_lan_diag *d, u64 now, u32 hz)
{
    struct HostAlive alive;
    host_alive_snapshot(&alive);
    if (alive.valid && (generation != alive.host_generation ||
                       memcmp(peer, link_peer_mac, sizeof(peer)))) {
        close_request();
        generation = alive.host_generation;
        memcpy(peer, link_peer_mac, sizeof(peer));
        have_id = pending = ready = attempted = 0;
    }
    if (!link_hello_ok || !alive.valid) {
        close_request(); ready = attempted = 0; return;
    }
    if (handle >= 0) {
        u32 status;
        i32 rc = link_host_status(handle, &status, 0, 0);
        if (ready_request) {
            if (rc == OS32_ERR_AGAIN) return;
            if (rc || status != 200) { close_request(); return; }
            const u8 *data;
            rc = link_host_read_stage(handle, sizeof(reply) - reply_bytes, &data, 0);
            if (rc > 0 && (u32)rc <= sizeof(reply) - reply_bytes) {
                memcpy(reply + reply_bytes, data, (u32)rc); reply_bytes += (u32)rc;
            } else if (!rc) { ready = ready_reply(); close_request(); }
            else if (rc != OS32_ERR_AGAIN) close_request();
        } else if (rc != OS32_ERR_AGAIN) {
            /* 200 delivers; a 4xx is permanent (the agent refused this result), so drop it. */
            if (!rc && ((status == 200 && written == result.payload_bytes) ||
                        (status >= 400 && status < 500))) pending = 0;
            close_request();
        } else if (written < result.payload_bytes) {
            rc = link_host_write(handle, result.payload + written, result.payload_bytes - written, 0);
            if (rc > 0 && (u32)rc <= result.payload_bytes - written) written += (u32)rc;
            else if (rc != OS32_ERR_AGAIN) close_request();
        }
        return;
    }
    if (pending) { open_request(0, now, hz); return; }
    if (!attempted || now >= ready_at) { open_request(1, now, hz); return; }
    if (!ready) return;
    HostNotice notice;
    int rc = host_mailbox_stage(generation, REMOTE_SVC_PPC_DIAG, &notice);
    if (rc) { if (rc != OS32_ERR_AGAIN) ready = attempted = 0; return; }
    if (notice.host_generation != generation || notice.service != REMOTE_SVC_PPC_DIAG) {
        ready = attempted = 0; return;
    }
    host_mailbox_commit(generation, notice.serial);
    if (notice.kind == REMOTE_NOTIFY_PROBE) { attempted = 0; return; }
    struct pm_diag_message request = {0};
    u32 id;
    int valid = decode(&notice, &id, &request);
    memset(&result, 0, sizeof(result));
    result_id = id;
    if (have_id && id <= last_id) {
        result.status = id == last_id ? PLN1_STATUS_CONFLICT : PLN1_STATUS_STALE;
    } else {
        if (notice.kind != REMOTE_NOTIFY_EXECUTE || !valid) result.status = PLN1_STATUS_BAD_LENGTH;
        else pm_lan_execute(&request, d, &result);
        last_id = id; have_id = 1;
    }
    pending = 1;
}
