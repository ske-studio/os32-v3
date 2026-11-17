/* Read-only PLN1 diagnostics. The service serializes calls and validates peers. */
#include "lan_boot.h"
#include "arch/ppc/kernel_probe.h"
#include "arch/ppc/ppc_trap.h"
#include "arch/ppc/ppc_oea.h"
#include "lib/kstring.h"

#if !KBOOT_LINK
/* Internal command key; only make_key below knows the legacy envelope. */
struct request_key {
    struct pm_diag_message command;
    u32 request_id;
    u64 serial, records, bytes;
    u16 flags;
    u8 identity[32], kind;
};
struct entry { struct request_key req; struct lan_message rsp; };
static struct entry cache[16];
static u8 session_mac[6], session_nonce[16];
static u32 count, next, last;
static int active;

#endif

static u32 get(const u8 *p, u32 n)
{
    u32 v = 0;
    while (n--) v = (v << 8) | *p++;
    return v;
}
static void put(u8 *p, u64 v, u32 n)
{
    while (n) { --n; p[n] = (u8)v; v >>= 8; }
}
#if !KBOOT_LINK
static int equal(const u8 *a, const u8 *b, u32 n)
{
    while (n--) if (*a++ != *b++) return 0;
    return 1;
}
static void make_key(const struct lan_message *q, struct request_key *key)
{
    memset(key, 0, sizeof(*key));
    key->request_id = q->request_id; key->serial = q->seq;
    key->records = q->drop_records; key->bytes = q->drop_bytes;
    key->flags = q->flags; key->kind = q->type;
    memcpy(key->identity, q->boot_id, 16);
    memcpy(key->identity + 16, q->host_nonce, 16);
    key->command.opcode = q->opcode; key->command.status = q->status;
    key->command.payload_bytes = q->payload_bytes;
    memcpy(key->command.payload, q->payload, q->payload_bytes);
}
static int same_request(const struct request_key *a, const struct request_key *b)
{
    return a->request_id == b->request_id && a->serial == b->serial &&
        a->records == b->records && a->bytes == b->bytes &&
        a->flags == b->flags && a->kind == b->kind &&
        equal(a->identity, b->identity, sizeof(a->identity)) &&
        a->command.opcode == b->command.opcode && a->command.status == b->command.status &&
        a->command.payload_bytes == b->command.payload_bytes &&
        equal(a->command.payload, b->command.payload, a->command.payload_bytes);
}
#endif
static int contains(const struct pm_lan_range *r, u32 base, u32 len)
{
    return base >= r->base && base - r->base < r->size &&
        len <= r->size - (base - r->base) && len - 1 <= 0xffffffffu - base;
}
int pm_lan_copy8(u32 va, u8 *out)
{
    u32 flags = ppc_irq_save();
    int rc = 1;
    if (ppc_trap_onfault_data((u32)ppc_probe_copy8_insn, va,
                              (u32)ppc_probe_copy8_resume)) {
        rc = ppc_probe_copy8(va, out);
        ppc_trap_onfault_clear();
    }
    ppc_irq_restore(flags);
    return rc;
}
void pm_lan_execute(const struct pm_diag_message *q, const struct pm_lan_diag *d,
                    struct pm_diag_message *r)
{
    u32 i, n, off, va, pa, tick, hz;
    memset(r, 0, sizeof(*r));
    u64 tb, us;
    const struct pm_lan_range *range = 0;
    const u32 *words;
    switch (q->opcode) {
    case PLN1_OP_STATUS: case PLN1_OP_CLOCK: case PLN1_OP_MEMORY: n = 0; break;
    case PLN1_OP_READ_DIAG: n = PLN1_REQ_READ_DIAG_BYTES; break;
    case PLN1_OP_PEEK: n = PLN1_REQ_PEEK_BYTES; break;
    default: r->status = PLN1_STATUS_UNSUPPORTED; return;
    }
    if (q->payload_bytes != n) { r->status = PLN1_STATUS_BAD_LENGTH; return; }
    switch (q->opcode) {
    case PLN1_OP_STATUS: case PLN1_OP_MEMORY:
        words = q->opcode == PLN1_OP_STATUS ? d->status : d->memory;
        n = q->opcode == PLN1_OP_STATUS ? d->status_words : d->memory_words;
        for (i = 0; i < n; ++i) put(r->payload + i * 4, words[i], 4);
        r->payload_bytes = (u16)(n * 4); return;
    case PLN1_OP_CLOCK:
        d->clock(&tick, &hz, &tb, &us);
        put(r->payload, tick, 4); put(r->payload + 4, hz, 4);
        put(r->payload + 8, tb, 8); put(r->payload + 16, us, 8);
        r->payload_bytes = 24; return;
    case PLN1_OP_READ_DIAG:
        off = get(q->payload, 2); n = get(q->payload + 2, 2);
        if (!n || n > 128 || off > 256 || n > 256 - off) goto bad_range;
        for (i = 0; i < n; ++i) r->payload[i] = d->diag[off + i];
        r->payload_bytes = (u16)n; return;
    case PLN1_OP_PEEK:
        va = get(q->payload, 4); n = get(q->payload + 4, 2);
        if (!n || n > 256 || get(q->payload + 6, 2)) goto bad_range;
        for (i = 0; i < d->peek_ok_count; ++i)
            if (contains(&d->peek_ok[i], va, n)) { range = &d->peek_ok[i]; break; }
        if (!range) goto bad_range;
        for (i = 0; i < n; ++i) {
            if (d->translate(va + i, &pa) || !contains(range, pa, 1)) goto bad_range;
            if (d->copy8(va + i, &r->payload[i])) {
                r->status = PLN1_STATUS_FAULT; return;
            }
        }
        r->payload_bytes = (u16)n; return;
    }
    return;
bad_range:
    r->payload_bytes = 0; r->status = PLN1_STATUS_RANGE;
}
#if !KBOOT_LINK
int pm_lan_monitor(const struct lan_message *q, const u8 mac[6],
                   const struct pm_lan_diag *d, struct lan_message *r)
{
    u32 i;
    struct request_key key;
    if (!q || !mac || !d || !r || q->type != PLN1_TYPE_REQ || q->payload_bytes > 256) return 0;
    if (!active || !equal(mac, session_mac, 6) || !equal(q->host_nonce, session_nonce, 16)) {
        memcpy(session_mac, mac, 6); memcpy(session_nonce, q->host_nonce, 16);
        count = next = last = 0; active = 1;
    }
    make_key(q, &key);
    for (i = 0; i < count; ++i) if (cache[i].req.request_id == q->request_id) {
        if (same_request(&key, &cache[i].req)) { *r = cache[i].rsp; return 1; }
        memset(r, 0, sizeof(*r)); *r = *q; r->type = PLN1_TYPE_RSP;
        r->payload_bytes = 0; r->status = PLN1_STATUS_CONFLICT; return 1;
    }
    memset(r, 0, sizeof(*r));
    memcpy(r->boot_id, q->boot_id, 16); memcpy(r->host_nonce, q->host_nonce, 16);
    r->seq = q->seq; r->request_id = q->request_id; r->opcode = q->opcode; r->type = PLN1_TYPE_RSP;
    if (count && q->request_id <= last) { r->status = PLN1_STATUS_STALE; return 1; }
    struct pm_diag_message request = {.opcode = q->opcode, .payload_bytes = q->payload_bytes}, result;
    memcpy(request.payload, q->payload, q->payload_bytes);
    pm_lan_execute(&request, d, &result);
    r->status = result.status; r->payload_bytes = result.payload_bytes;
    memcpy(r->payload, result.payload, result.payload_bytes);
    cache[next].req = key; cache[next].rsp = *r;
    next = (next + 1) % 16; if (count < 16) ++count; last = q->request_id;
    return 1;
}

#endif
