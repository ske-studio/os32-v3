/* Independent NOTIFY receiver: NET_HOSTSVC_DESIGN §5 and addendum #3/#4.
 * A8/A9 call these from link_tick, after peer/session/header validation.
 * No task-side entry drives this state machine. */
#include "link_notify.h"
#include "link.h"
#include "host_mailbox.h"
#include "le_internal.h"
#include "os32_kapi_shared.h"

/* §5: mailbox overflow drops the oldest entry, so reception always has four
 * message credits, independently of the application's consumption rate. */
#define NOTIFY_WINDOW 4
#define NOTIFY_ALL_SERVICES 65535u

static int active, extended_hello, generation_known;
static u32 host_generation, ack_seq;
static u16 alive_service;
static int want_ack, want_window;

void link_notify_reset(void)
{
    active = 0;
    extended_hello = 0;
    generation_known = 0;
    host_generation = 0;
    alive_service = 0;
    ack_seq = 0;
    want_ack = 0;
    want_window = 0;
    /* Mailbox and non-consuming ALIVE observation outlive link sessions. */
}

void link_notify_session(u16 sess, u16 epoch, int extended, u32 generation,
                         u16 alive_id)
{
    /* The caller owns the (sess,epoch) identity and rejects old frames. */
    (void)sess;
    (void)epoch;
    link_notify_reset();
    active = 1;
    extended_hello = !!extended;
    generation_known = extended_hello;
    host_generation = extended_hello ? generation : 0;
    alive_service = extended_hello ? alive_id : 0;
    want_window = 1; /* Initial rid=0 WINDOW starts the host's notification TX. */
}

void link_notify_rx(u32 seq, const u8 *payload, u16 len)
{
    if (!active || !payload || len < LINK_NOTIFY_DATA_OFF ||
        len > LINK_NOTIFY_MAX_LEN)
        return;
    u32 generation = le_read32(payload + LINK_NOTIFY_HOST_GEN_OFF);
    u16 service = le_read16(payload + LINK_NOTIFY_SERVICE_OFF);
    u16 kind = le_read16(payload + LINK_NOTIFY_KIND_OFF);
    u16 length = le_read16(payload + LINK_NOTIFY_LENGTH_OFF);
    if (!generation || !service || service == NOTIFY_ALL_SERVICES ||
        le_read16(payload + LINK_NOTIFY_RESERVED_OFF) != 0 ||
        length != len - LINK_NOTIFY_DATA_OFF ||
        (generation_known && generation != host_generation))
        return;

    const u8 *data = payload + LINK_NOTIFY_DATA_OFF;
    int alive = extended_hello && service == alive_service &&
                kind == LINK_NOTIFY_ALIVE_KIND;
    if (alive && (length != LINK_NOTIFY_ALIVE_LEN ||
                  le_read32(data) != generation))
        return;

    /* Go-Back-N: both a gap and a duplicate get only the last contiguous ACK.
     * Equality of unsigned next sequence also works across u32 wrap. */
    if (seq != ack_seq + 1u) {
        want_ack = 1;
        return;
    }
    int rc = host_mailbox_put(generation, service, kind, data, length, alive);
    /* Lack of notification storage must not stall the communication itself.
     * Any other put error leaves this sequence unaccepted. */
    if (rc != 0 && rc != OS32_ERR_NOSYS)
        return;
    /* A legacy HELLO supplies no identity: pin the first accepted generation,
     * but never infer an internal ALIVE service number from wire content. */
    host_generation = generation;
    generation_known = 1;
    ack_seq = seq;
    want_ack = 1;
    want_window = 1;
}

void link_notify_tick(u32 now)
{
    /* Sender owns the 100ms retransmission timer. Here each tick is just one
     * bounded attempt per pending control; errors retain work until recovery. */
    (void)now;
    if (!active) return;
    if (want_ack) {
        int rc = link_notify_control(LINK_OP_ACK, ack_seq, NOTIFY_WINDOW);
        if (rc != 0) return;
        want_ack = 0;
    }
    if (want_window) {
        int rc = link_notify_control(LINK_OP_WINDOW, ack_seq, NOTIFY_WINDOW);
        if (rc != 0) return;
        want_window = 0;
    }
}
