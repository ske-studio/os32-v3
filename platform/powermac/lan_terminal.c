/* Single foreground LAN terminal. The service validates the boot ID. */
#include "lan_boot.h"
#include "lib/kstring.h"

struct term_chunk { u64 seq; u16 len; u8 data[PLN1_PAYLOAD_MAX]; };
static struct term_chunk fifo[16];
static u8 mac[6], nonce[16], boot_id[16];
static u32 head, count;
static u64 in_next, in_drop_frames, out_next, out_drop, out_bytes;
static int connected;

int pm_term_connected(void) { return connected; }

static int same_bytes(const u8 *a, const u8 *b, u32 n)
{
    u32 i;
    for (i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static void put64(u8 *p, u64 v)
{
    u32 i;
    for (i = 0; i < 8; ++i) p[i] = (u8)(v >> (56 - i * 8));
}

void pm_term_receive(const struct lan_message *m, const u8 src[6])
{
    if (m->type == PLN1_TERM_OPEN) {
        if (m->payload_bytes || m->seq) return;
        kmemcpy(mac, src, 6);
        kmemcpy(nonce, m->host_nonce, 16);
        kmemcpy(boot_id, m->boot_id, 16);
        in_next = in_drop_frames = 0;
        head = count = 0;
        out_next = out_drop = out_bytes = 0;
        connected = 1;
        pm_terminal_open();
        return;
    }
    if (!connected || m->type != PLN1_TERM_IN ||
        !same_bytes(mac, src, 6) || !same_bytes(nonce, m->host_nonce, 16)) return;
    if (m->seq < in_next) return;
    if (m->seq > in_next) {
        in_drop_frames += m->seq - in_next;
        pm_terminal_line_lost();
    }
    in_next = m->seq + 1;
    pm_terminal_feed(m->payload, m->payload_bytes);
}

void pm_term_write(const u8 *data, u32 bytes)
{
    while (bytes) {
        u32 n;
        if (count) {
            struct term_chunk *tail = &fifo[(head + count - 1) % 16];
            n = PLN1_PAYLOAD_MAX - tail->len;
            if (n > bytes) n = bytes;
            if (n) {
                kmemcpy(tail->data + tail->len, data, n);
                tail->len += (u16)n;
                data += n;
                bytes -= n;
                continue;
            }
        }
        n = bytes > PLN1_PAYLOAD_MAX ? PLN1_PAYLOAD_MAX : bytes;
        if (count == 16) {
            out_next++;
            out_drop++;
            out_bytes += n;
        } else {
            struct term_chunk *c = &fifo[(head + count) % 16];
            c->seq = out_next++;
            c->len = (u16)n;
            kmemcpy(c->data, data, n);
            ++count;
        }
        data += n;
        bytes -= n;
    }
}

int pm_term_state(struct lan_message *m)
{
    if (!connected) return 0;
    kmemset(m, 0, sizeof(*m));
    kmemcpy(m->boot_id, boot_id, 16);
    kmemcpy(m->host_nonce, nonce, 16);
    m->drop_records = out_drop;
    m->drop_bytes = out_bytes;
    m->type = PLN1_TERM_STATE;
    m->seq = out_next;
    m->payload_bytes = 24;
    put64(m->payload, in_next);
    put64(m->payload + 8, in_drop_frames);
    return 1;
}

int pm_term_tx_peek(struct lan_message *m)
{
    struct term_chunk *c;
    if (!connected || !count) return 0;
    kmemset(m, 0, sizeof(*m));
    kmemcpy(m->boot_id, boot_id, 16);
    kmemcpy(m->host_nonce, nonce, 16);
    m->drop_records = out_drop;
    m->drop_bytes = out_bytes;
    c = &fifo[head];
    m->type = PLN1_TERM_OUT;
    m->seq = c->seq;
    m->payload_bytes = c->len;
    kmemcpy(m->payload, c->data, c->len);
    return 1;
}

void pm_term_tx_complete(void)
{
    head = (head + 1) % 16;
    --count;
}

const u8 *pm_term_peer(void)
{
    return mac;
}
