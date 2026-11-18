/* Foreground-only LOG adapter. Permanent refusal consumes and counts a loss. */
#include "lan_boot.h"
#include "boot_token.h"
#include "net/link.h"
#include "net/host_mailbox.h"
#include "os32/os32_kapi_shared.h"

#if KBOOT_SH
#include "hostfs_trace.h"
#include "kprintf.h"
#include "kstring.h"
#include <stdarg.h>
#include "net/link_platform.h"
#include "nic_gem.h"
#endif

#define CHUNK_BYTES 256u
#define REQUEST_BYTES 160u
static i32 handle = -1;
static u8 body[CHUNK_BYTES];
static u32 length, written, generation, loss_flags;
static u64 sequence, records, bytes, lost_records, lost_bytes;
static int pending, connected;

static char *decimal(char *p, u64 value)
{
    /* Shift/subtract avoids a libgcc 64-bit division on PowerPC. */
    char digits[20];
    u32 n = 0;
    do {
        u64 quotient = 0;
        u32 remainder = 0;
        for (int bit = 63; bit >= 0; bit--) {
            remainder = (remainder << 1) | (u32)((value >> bit) & 1);
            if (remainder >= 10) { remainder -= 10; quotient |= (u64)1 << bit; }
        }
        digits[n++] = (char)('0' + remainder);
        value = quotient;
    } while (value);
    while (n) *p++ = digits[--n];
    return p;
}
#if KBOOT_SH
/* RX/timeout hooks can run in IRQ. Only foreground formats and submits LOG. */
#define TRACE_SLOTS 8u
#define TRACE_SUCCESS_TICKS 400u
#define TRACE_DROP_TICKS 100u
#define TRACE_DROP_LIMIT 4u
struct trace_record {
    struct pm_gem_trace gem;
    union {
        struct {
            struct link_trace_call link;
            const char *phase, *reason;
            u32 tick, received, length;
            i32 detail;
            int rc;
        } finish;
        struct link_trace_rx event;
    };
    const char *kind;
    enum link_trace_event type;
};

static struct trace_record trace_queue[TRACE_SLOTS];
static u32 trace_head, trace_count, trace_lost;
static u32 last_success, drop_start, drop_count;
static int sampled_success;
static struct link_trace_rx last_drop;

/* Called with interrupts excluded from both producers. */
static struct trace_record *trace_reserve(void)
{
    if (trace_count == TRACE_SLOTS) {
        trace_lost++;
        return 0;
    }
    return &trace_queue[(trace_head + trace_count++) % TRACE_SLOTS];
}

void link_trace_event(enum link_trace_event event, struct link_trace_rx detail)
{
    static const char *const kinds[] = { "drop", "silence", "resync", "invalidate" };
    if (event == LINK_TRACE_RESPONSE_DROP) {
        if (detail.tick - drop_start >= TRACE_DROP_TICKS) {
            drop_start = detail.tick;
            drop_count = 0;
        }
        if (drop_count >= TRACE_DROP_LIMIT) return;
        if (drop_count && detail.rid == last_drop.rid && detail.sess == last_drop.sess &&
            detail.epoch == last_drop.epoch && detail.reason == last_drop.reason) return;
        last_drop = detail;
        drop_count++;
    }
    struct trace_record *r = trace_reserve();
    if (!r) return;
    r->kind = kinds[event];
    r->type = event;
    r->event = detail;
    if (event != LINK_TRACE_RESPONSE_DROP) pm_gem_trace_snapshot(&r->gem);
}

void pm_hfs_trace_finish(i32 handle, const char *phase, const char *reason,
                         int rc, i32 detail, u32 received, u32 length)
{
    u32 now = link_platform_now();
    /* Keep all failures. Sample successful body-bearing CALLs so a 64 KiB cp
     * cannot generate one LOG group for every read/write chunk. */
    if (!rc && (!length || (sampled_success && now - last_success < TRACE_SUCCESS_TICKS)))
        return;
    unsigned int f = link_platform_lock();
    struct trace_record *r = trace_reserve();
    if (r) {
        r->kind = 0;
        r->finish.tick = now;
        r->finish.phase = phase;
        r->finish.reason = reason;
        r->finish.rc = rc;
        r->finish.detail = detail;
        r->finish.received = received;
        r->finish.length = length;
        r->finish.link = (struct link_trace_call){0};
        link_trace_call_snapshot(handle, &r->finish.link);
        if (rc) pm_gem_trace_snapshot(&r->gem);
        else {
            last_success = now;
            sampled_success = 1;
        }
    }
    link_platform_unlock(f);
}

/* Share the kernel formatter. Collect one line before publishing to the
 * existing LAN log FIFO, keeping this trace off the synchronous ESCC sink. */
static char trace_line[CHUNK_BYTES];
static u32 trace_line_bytes;

static void trace_write(const char *buf, u32 n, u8 attr)
{
    (void)attr;
    u32 room = sizeof(trace_line) - trace_line_bytes;
    u32 take = n < room ? n : room;
    kmemcpy(trace_line + trace_line_bytes, buf, take);
    trace_line_bytes += take;
    if (n && buf[n - 1] == '\n') {
        trace_line[trace_line_bytes - 1] = '\n';
        pm_lan_log_trace(trace_line, trace_line_bytes);
        trace_line_bytes = 0;
    }
}

#define KPRINTF_WRITE trace_write
#define KPRINTF_PRINT(str, attr) trace_write(str, kstrlen(str), attr)
#define KPRINTF_PRINT_UTF8 KPRINTF_PRINT
#define KPRINTF_NAME pm_link_trace_printf
#define KPRINTF_VNAME pm_link_trace_vprintf
#include "lib/kprintf_format.inc"

static void trace_rx(const char *kind, const struct link_trace_rx *e)
{
    pm_link_trace_printf(7, "G4 A %s rid=%u sess=%u epoch=%u tick=%u flags=%x state=%u status=%u len=%u reason=%s\n",
            kind, e->rid, (u32)e->sess, (u32)e->epoch, e->tick, (u32)e->flags,
            (u32)e->state, e->status, e->length, e->reason);
}

static void trace_gem(u32 rid, u32 tick, const struct pm_gem_trace *g)
{
    char packets[21], drops[21];
    *decimal(packets, g->rx_packets) = 0;
    *decimal(drops, g->rx_drops) = 0;
    pm_link_trace_printf(7, "G4 F rid=%u tick=%u ready=%u rx_packets=%s rx_drops=%s adapter=%u link=%u tx_pending=%u\n",
            rid, tick, g->ready, packets, drops, g->adapter, g->link_queue, g->tx_pending);
}

static void trace_finish(const struct trace_record *r)
{
    const struct link_trace_call *t = &r->finish.link;
    if (t->control.reason) trace_rx("control", &t->control);
    if (t->response.reason) trace_rx("response", &t->response);
    if (t->blocked_by || (r->finish.rc && t->read_wait))
        pm_link_trace_printf(7, "G4 C rid=%u again=%s owner_rid=%u\n", t->rid,
                             t->read_wait ? t->read_wait : "resolved", t->blocked_by);
    if (t->planned) {
        pm_link_trace_printf(7, "G4 D plan rid=%u tick=%u\n", t->rid, t->plan_tick);
        if (t->tried)
            pm_link_trace_printf(7, "G4 D tx rid=%u tick=%u sent=%u rc=%d first_reject=%d rejects=%u credit=%u\n",
                    t->rid, t->tx_tick, (u32)t->sent, (u32)t->tx_rc,
                    (u32)t->first_reject, t->rejects, (u32)t->credit);
        else
            pm_link_trace_printf(7, "G4 D tx rid=%u reason=no_attempt\n", t->rid);
    } else if (t->rid && r->finish.rc) {
        const char *reason = !t->up ? "not_up" :
                             !t->response.reason ? "no_response" :
                             !t->length ? "empty" :
                             t->received >= t->length ? "fully_received" :
                             !t->owns_ring ? "no_ring" : "no_timer_pass";
        pm_link_trace_printf(7, "G4 D no_plan rid=%u reason=%s\n", t->rid, reason);
    }
    pm_link_trace_printf(7, "G4 B rid=%u sess=%u epoch=%u tick=%u phase=%s reason=%s rc=%d detail=%d got=%u len=%u\n",
            t->rid, (u32)t->sess, (u32)t->epoch, r->finish.tick, r->finish.phase,
            r->finish.reason, (u32)r->finish.rc, (u32)r->finish.detail,
            r->finish.received, r->finish.length);
    if (r->finish.rc) trace_gem(t->rid, r->finish.tick, &r->gem);
}

static void trace_poll(void)
{
    /* IRQ can enqueue after this check; the next foreground pass drains it. */
    if (!trace_count) return;
    unsigned int f = link_platform_lock();
    struct trace_record r = trace_queue[trace_head];
    u32 lost = trace_lost;
    trace_lost = 0;
    trace_head = (trace_head + 1) % TRACE_SLOTS;
    trace_count--;
    link_platform_unlock(f);

    if (lost) pm_link_trace_printf(7, "G4 lost=%u\n", lost);
    if (!r.kind) {
        trace_finish(&r);
    } else if (r.type == LINK_TRACE_RESPONSE_DROP) {
        trace_rx(r.kind, &r.event);
    } else {
        pm_link_trace_printf(7, "G4 E %s rid=%u sess=%u epoch=%u tick=%u state=%u reason=%s\n",
                r.kind, r.event.rid, (u32)r.event.sess, (u32)r.event.epoch,
                r.event.tick, (u32)r.event.state, r.event.reason);
        trace_gem(r.event.rid, r.event.tick, &r.gem);
    }
}
#endif

static u64 sum(u64 a, u64 b)
{
    return b > ~(u64)0 - a ? ~(u64)0 : a + b;
}
static void close_request(void)
{
    if (handle >= 0) (void)link_host_close(handle, 0);
    handle = -1;
    written = 0;
}
void pm_link_log_poll(void)
{
    struct HostAlive alive;
    host_alive_snapshot(&alive);
    int up = link_hello_ok && alive.valid;
    if (connected && (!up || generation != alive.host_generation)) {
        /* Delivery at the old peer's tail is unprovable, even if retried. */
        if (pending) {
            lost_records = sum(lost_records, 1);
            lost_bytes = sum(lost_bytes, length);
        }
        close_request();
    }
    connected = up;
    if (!up) return;
    generation = alive.host_generation;
    if (!pending) {
#if KBOOT_SH
        trace_poll();
#endif
        const u8 *data;
        u32 flags;
        if (pm_lan_log_peek(&data, &length, &sequence, &records, &bytes, &flags) != LAN_OK)
            return;
        for (u32 i = 0; i < length; i++) body[i] = data[i];
        loss_flags |= flags;
        pending = 1;
    }
    if (handle < 0) {
        static const char hex[] = "0123456789abcdef";
        char request[REQUEST_BYTES], *p = request;
        u8 boot[16];
        boot_token_get(boot);
        *p++ = 'L'; *p++ = 'O'; *p++ = 'G'; *p++ = ' ';
        for (u32 i = 0; i < 16; i++) {
            *p++ = hex[boot[i] >> 4]; *p++ = hex[boot[i] & 15];
        }
        *p++ = ' '; p = decimal(p, sequence);
        *p++ = ' '; p = decimal(p, sum(records, lost_records));
        *p++ = ' '; p = decimal(p, sum(bytes, lost_bytes));
        *p++ = ' '; p = decimal(p, loss_flags);
        *p++ = ' '; p = decimal(p, length);
        handle = link_host_open(request, (u32)(p - request), 0);
        return;
    }
    u32 status;
    i32 rc = link_host_status(handle, &status, 0, 0);
    if (rc != OS32_ERR_AGAIN) {
        if (!rc && status == 200) {
            pm_lan_log_complete(LAN_OK);
            pending = 0;
        } else if (!rc && status >= 400 && status < 500) {
            /* The peer refused this chunk permanently; do not claim delivery. */
            lost_records = sum(lost_records, 1);
            lost_bytes = sum(lost_bytes, length);
            pm_lan_log_complete(LAN_OK);
            pending = 0;
        }
        close_request();
        return;
    }
    if (written < length) {
        rc = link_host_write(handle, body + written, length - written, 0);
        if (rc > 0) written += (u32)rc;
        else if (rc != OS32_ERR_AGAIN) close_request();
    }
}
