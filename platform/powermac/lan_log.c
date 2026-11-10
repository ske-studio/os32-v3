/* Bounded byte FIFO for the PowerMac boot log; producer is CPU0. */
#include "lan_boot.h"
#include "kboot_log.h"
#include "ppc_oea.h"

#define LOG_SLOTS 64u
#define LOG_BYTES 256u
#define LOG_COPY_MAX (LOG_SLOTS * LOG_BYTES)
#define LOG_SATURATED 1u

struct log_slot { u64 seq; u32 len; u8 data[LOG_BYTES]; };
static struct log_slot slots[LOG_SLOTS];
static u8 scratch[LOG_BYTES];
static u32 head, count, open_slot, busy, registered, drop_tail, flags;
static u64 next_seq, drop_records, drop_bytes;

static void add_sat(u64 *v, u64 n)
{
    if (n > ~(u64)0 - *v) { *v = ~(u64)0; flags |= LOG_SATURATED; }
    else *v += n;
}
static void close_drop(void)
{
    if (drop_tail) {
        add_sat(&next_seq, 1);
        add_sat(&drop_records, 1);
        drop_tail = 0;
    }
}
static void discard(u32 n)
{
    u64 total = (u64)drop_tail + n;
    u64 chunks = total >> 8;
    add_sat(&drop_bytes, n);
    add_sat(&next_seq, chunks);
    add_sat(&drop_records, chunks);
    drop_tail = (u32)(total & (LOG_BYTES - 1));
}
static void close_slot(void)
{
    if (open_slot) {
        struct log_slot *s = &slots[(head + count - 1) % LOG_SLOTS];
        s->seq = next_seq;
        add_sat(&next_seq, 1);
        open_slot = 0;
    }
}
static int log_write(void *ctx, const char *buf, u32 n)
{
    u32 saved, copied = 0;
    (void)ctx;
    if (!n) return 0;
    saved = ppc_irq_save();
    if (busy) {
        close_slot();
        discard(n);
        ppc_irq_restore(saved);
        return 0;
    }
    busy = 1;
    ppc_irq_restore(saved);
    while (n && copied < LOG_COPY_MAX) {
        u32 amount;
        struct log_slot *s;
        saved = ppc_irq_save();
        if (!open_slot && count == LOG_SLOTS) {
            discard(n);
            n = 0;
            ppc_irq_restore(saved);
            break;
        }
        close_drop();
        if (!open_slot) {
            s = &slots[(head + count) % LOG_SLOTS];
            s->len = 0;
            count++;
            open_slot = 1;
        }
        s = &slots[(head + count - 1) % LOG_SLOTS];
        amount = LOG_BYTES - s->len;
        if (amount > n) amount = n;
        if (amount > LOG_COPY_MAX - copied) amount = LOG_COPY_MAX - copied;
        for (u32 i = 0; i < amount; i++) s->data[s->len + i] = (u8)buf[i];
        s->len += amount;
        if (s->len == LOG_BYTES) close_slot();
        ppc_irq_restore(saved);
        buf += amount;
        n -= amount;
        copied += amount;
    }
    saved = ppc_irq_save();
    if (n) { close_slot(); discard(n); }
    busy = 0;
    ppc_irq_restore(saved);
    return 0;
}
static const struct pm_log_sink sink = { log_write, 0 };
int pm_lan_log_register(void)
{
    if (registered) return 0;
    if (pm_log_add_sink(&sink)) return -1;
    registered = 1;
    return 0;
}
int pm_lan_log_peek(const u8 **data, u32 *len, u64 *seq,
                    u64 *records, u64 *bytes, u32 *status)
{
    u32 saved;
    struct log_slot *s;
    if (!data || !len || !seq || !records || !bytes || !status) return LAN_EINVAL;
    saved = ppc_irq_save();
    if (count == 1) close_slot();
    if (!count) { ppc_irq_restore(saved); return LAN_AGAIN; }
    s = &slots[head];
    for (u32 i = 0; i < s->len; i++) scratch[i] = s->data[i];
    *data = scratch; *len = s->len; *seq = s->seq;
    *records = drop_records; *bytes = drop_bytes; *status = flags;
    ppc_irq_restore(saved);
    return LAN_OK;
}
void pm_lan_log_stats(u64 *next, u64 *records, u64 *bytes, u32 *status)
{
    u32 saved = ppc_irq_save();
    *next = next_seq; *records = drop_records; *bytes = drop_bytes; *status = flags;
    ppc_irq_restore(saved);
}
void pm_lan_log_complete(int result)
{
    u32 saved = ppc_irq_save();
    if (result == LAN_OK && count) {
        head = (head + 1) % LOG_SLOTS;
        count--;
    }
    ppc_irq_restore(saved);
}
