/* Host Services SDK transport. The KAPI owns peer-silence detection: only
 * it sees PROCESSING replies, so polling AGAIN must not start a second timer. */
#include "host_call_internal.h"
#include "os32api.h"

extern KernelAPI *kapi;

static int call_expired(u32 start, u32 timeout)
{
    /* Round milliseconds up to 100Hz ticks without overflowing. */
    u32 ticks = timeout / (1000u / HOST_TICK_HZ) +
                (timeout % (1000u / HOST_TICK_HZ) != 0);
    return timeout && (u32)(kapi->get_tick() - start) >= ticks;
}

static int call_wait(u32 start, u32 timeout, int rc)
{
    if (rc != OS32_ERR_AGAIN && rc != OS32_ERR_FULL) return rc;
    if (!timeout) return OS32_ERR_AGAIN;
    if (call_expired(start, timeout)) return OS32_ERR_TIMEOUT;
    kapi->sys_yield();
    return 0;
}

int host_exchange(const char *req, u32 req_len, const void *in, u32 in_len,
                  host_body_fn body, void *ctx, int *status, u32 timeout)
{
    u32 start = kapi->get_tick(), sent = 0, got = 0, st = 0, length = 0;
    u8 buf[HOST_WRITE_CHUNK];
    int h, rc;
    if (!req || !req_len || req_len > HOST_REQ_MAX || (in_len && !in))
        return OS32_ERR_INVAL;
    for (;;) {
        if (call_expired(start, timeout)) return OS32_ERR_TIMEOUT;
        h = kapi->host_open(req, req_len);
        if (h >= 0) break;
        rc = call_wait(start, timeout, h);
        if (rc) return rc;
    }
    for (;;) {
        if (call_expired(start, timeout)) { rc = OS32_ERR_TIMEOUT; break; }
        if (sent < in_len) {
            u32 n = in_len - sent;
            if (n > HOST_WRITE_CHUNK) n = HOST_WRITE_CHUNK;
            rc = kapi->host_write(h, (const u8 *)in + sent, n);
            if (rc > 0) { sent += (u32)rc; continue; }
            /* A service may reject the declaration before accepting its body. */
            if (rc == OS32_ERR_INVAL) {
                int sr = kapi->host_status(h, &st, &length);
                if (!sr) { if (status) *status = (int)st; rc = HOST_ESERVICE; }
                else if (sr != OS32_ERR_AGAIN) rc = sr;
                break;
            }
            if (!rc) rc = OS32_ERR_AGAIN;
        } else {
            rc = kapi->host_status(h, &st, &length);
            if (!rc) {
                if (status) *status = (int)st;
                rc = kapi->host_read(h, buf, sizeof(buf));
                if (!rc) { rc = (int)got; break; }
                if (rc > 0) {
                    got += (u32)rc;
                    rc = body ? body(ctx, buf, (u32)rc) : 0;
                    if (rc) break;
                    continue;
                }
            }
        }
        rc = call_wait(start, timeout, rc);
        if (rc) break;
    }
    kapi->host_close(h);
    return rc;
}

struct CallBuffer { u8 *out; u32 cap, used; };
static int call_buffer(void *ctx, const u8 *buf, u32 n)
{
    struct CallBuffer *b = ctx;
    if (n > b->cap - b->used) return OS32_ERR_NOSPC;
    for (u32 i = 0; i < n; i++) b->out[b->used++] = buf[i];
    return 0;
}

int host_call(const char *req, const void *in, u32 in_len,
              void *out, u32 cap, u32 timeout)
{
    u32 n = 0;
    struct CallBuffer b = { out, cap, 0 };
    if (!req || (cap && !out)) return OS32_ERR_INVAL;
    while (req[n] && n <= HOST_REQ_MAX) n++;
    return host_exchange(req, n, in, in_len, call_buffer, &b, 0, timeout);
}

/* Cache the complete LIST text, with no fixed service/name count. */
static char *notice_list;
static u32 notice_generation, notice_alive;
static HostNotice pending_alive;
static int have_alive;

static void notice_clear(void)
{
    if (notice_list) kapi->mem_free(notice_list);
    notice_list = 0;
    notice_generation = notice_alive = 0;
}

struct ListBuffer { char *text; u32 used; };
static int list_body(void *ctx, const u8 *buf, u32 n)
{
    struct ListBuffer *b = ctx;
    char *next = kapi->mem_alloc(b->used + n + 1);
    if (!next) return OS32_ERR_NOSPC;
    for (u32 i = 0; i < b->used; i++) next[i] = b->text[i];
    for (u32 i = 0; i < n; i++) next[b->used + i] = (char)buf[i];
    if (b->text) kapi->mem_free(b->text);
    b->text = next;
    b->used += n;
    next[b->used] = 0;
    return 0;
}

static int list_uint(const char **text, u32 *out)
{
    const char *p = *text;
    u32 v = 0;
    if (*p < '0' || *p > '9') return OS32_ERR_IO;
    while (*p >= '0' && *p <= '9') {
        u32 digit = (u32)(*p++ - '0');
        if (v > (0xffffffffu - digit) / 10u) return OS32_ERR_IO;
        v = v * 10u + digit;
    }
    *out = v;
    *text = p;
    return 0;
}

static int list_id(const char *name, u32 *id)
{
    const char *p = notice_list;
    while (*p && *p++ != '\n') {}
    while (*p) {
        const char *n = name;
        int match;
        u32 version, value;
        while (*n && *p == *n) { p++; n++; }
        match = !*n && *p == ' ';
        while (*p && *p != ' ' && *p != '\n') p++;
        if (*p != ' ') return OS32_ERR_IO;
        p++;
        if (list_uint(&p, &version) || *p != ' ') return OS32_ERR_IO;
        p++;
        if (list_uint(&p, &value) || *p != ' ' || !value || value >= 65535)
            return OS32_ERR_IO;
        if (match) { *id = value; return 0; }
        while (*p && *p++ != '\n') {}
    }
    return OS32_ERR_NOTFOUND;
}

static int notice_load(void)
{
    struct ListBuffer b = { 0, 0 };
    const char *p;
    const char *prefix = "generation ";
    int st = 0;
    u32 generation;
    int rc = host_exchange("LIST", 4, 0, 0, list_body, &b, &st, HOST_CALL_TIMEOUT_MS);
    if (rc < 0) { if (b.text) kapi->mem_free(b.text); return rc; }
    p = b.text;
    if (!p || st != 200) rc = OS32_ERR_IO;
    else {
        while (*prefix && *p == *prefix) { p++; prefix++; }
        if (*prefix || list_uint(&p, &generation) || !generation || *p != '\n')
            rc = OS32_ERR_IO;
    }
    if (rc < 0) { if (b.text) kapi->mem_free(b.text); return rc; }
    notice_list = b.text;
    notice_generation = generation;
    if (have_alive && pending_alive.host_generation != generation) have_alive = 0;
    notice_alive = 0;
    rc = list_id("os32.alive", &notice_alive);
    return rc == OS32_ERR_NOTFOUND ? 0 : rc;
}

int os32_host_notice_read(const char *name, HostNotice *out)
{
    u32 id;
    int rc;
    if (!name || !*name || !out) return OS32_ERR_INVAL;
    if (kapi->version < 82) return OS32_ERR_NOSYS;
    /* One refresh/retry, including a host generation change during LIST. */
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!notice_list) { rc = notice_load(); if (rc) { notice_clear(); return rc; } }
        if (notice_alive) {
            HostNotice alive;
            rc = kapi->host_notice_read(notice_generation, notice_alive, &alive);
            if (!rc) {
                pending_alive = alive;
                have_alive = 1;
                notice_clear();
                rc = notice_load();
                if (rc) { notice_clear(); return rc; }
            } else if (rc == OS32_ERR_STALE) { notice_clear(); have_alive = 0; continue; }
            else if (rc != OS32_ERR_AGAIN) return rc;
        }
        rc = list_id(name, &id);
        if (rc) return rc;
        if (have_alive && id == notice_alive && pending_alive.host_generation == notice_generation) {
            *out = pending_alive;
            have_alive = 0;
            return 0;
        }
        rc = kapi->host_notice_read(notice_generation, id, out);
        if (rc != OS32_ERR_STALE) return rc;
        notice_clear();
        have_alive = 0;
    }
    return OS32_ERR_STALE;
}
