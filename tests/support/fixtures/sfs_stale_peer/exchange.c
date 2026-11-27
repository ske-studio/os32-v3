#include "sys/dev/net/e82557/e82557.c"
#include "sys/dev/net/e82557/nic_e82557.c"
#include "sys/fs/serialfs/sfs_lan.h"
#include "sys/fs/serialfs/sfs_client.h"
#include "sys/kern/io_clock.h"
#include "sys/hal/time/tick_clock.h"
#include "sys/kern/irq/tick.h"
#include "sys/kern/irq/tick_budget.h"
#include "tick_order.h"
static void write_text(const char *s)
{
    __asm__ volatile("int $0x80" :: "a"(4), "b"(1), "c"(s), "d"(kstrlen(s)) : "memory");
}

static void field(const char *label, int value)
{
    char buf[16];
    char *p = buf + sizeof(buf) - 1;
    unsigned magnitude = value < 0 ? -value : value;
    *p = 0;
    do {
        *--p = '0' + magnitude % 10;
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) *--p = '-';
    write_text(label);
    write_text(p);
}

static __attribute__((noreturn)) void finish(int status)
{
    __asm__ volatile("int $0x80" :: "a"(1), "b"(status));
    __builtin_unreachable();
}
static __attribute__((noreturn)) void abort(void) { finish(2); }
static u32 now(void *ctx) { return tick_count; }
static void idle(void *ctx) { _halt(); }
static int can_wait(void *ctx) { return _irq_enabled(); }
static const SfsIo model_clock = {.now = now, .idle = idle, .can_wait = can_wait};
unsigned model_if = 1;
volatile u32 kctx_irq_depth;
static u8 dma[E82557_DMA_SIZE];
static SfsLan lan;

#include "sys/fs/serialfs/serialfs_persistent.c"
extern const SfsIo facade_sfs_lan_io;
extern void facade_e82557_tick(void);
extern void facade_sfs_lan_init(SfsLan *,const SfsLanNic *,const SfsIo *,void *);
extern int facade_sfs_lan_set_peer(SfsLan *, const char *);
extern void model_load(void);
void host_alive_snapshot(struct HostAlive *out) { *out=(struct HostAlive){0}; }
void boot_token_get(u8 out[16]) { kmemset(out,0,16); }
static const SfsIo *transport;
static u32 complete_at, gets, pending_nonce, reply_at;
static const u8 old_peer[6] = {2, 0, 0, 0, 0, 1};
static const u8 peer[6] = {2, 0, 0, 0, 0, 2};
static u16 hello_version = SFS_VERSION, hello_max = SFS_LAN_MAX_PAYLOAD;
static int expire_in_get, gets_at_deadline;
static u8 reply_type;
static u16 reply_seq;
static int peer_matches(const u8 expected[6])
{
    for (u32 i = 0; i < SFS_LAN_MAC_LEN; i++)
        if (lan.peer[i] != expected[i]) return 0;
    return 1;
}

static void inject(const u8 peer[6], u32 nonce)
{
    u8 *rfd = e82557_rfd(rx_ring(), nic.rx.dev);
    u8 *frame = rfd + E82557_RFD_HEADER;
    kmemcpy(frame, nic.mac, 6);
    kmemcpy(frame + 6, peer, 6);
    frame[12] = SFS_LAN_ETHERTYPE >> 8;
    frame[13] = SFS_LAN_ETHERTYPE & 255;
    u8 payload[SFS_HELLO_RSP_LEN] = {0};
    sfs_put16(payload + SFS_HELLO_RSP_VER, hello_version);
    sfs_put16(payload + SFS_HELLO_RSP_MAX, hello_max);
    sfs_put32(payload + SFS_HELLO_RSP_NONCE, nonce);
    if (reply_type == SFS_T_STAT) {
        sfs_put32(payload, 0);
        payload[SFS_STATUS_LEN] = reply_seq;
        sfs_encode(frame + 14, SFS_T_STAT | SFS_T_RESP, 7, reply_seq, payload, SFS_STATUS_LEN + 1);
    } else {
        sfs_encode(frame + 14, SFS_T_HELLO | SFS_T_RESP, 7, 0, payload, sizeof(payload));
    }
    e82557_wr16(rfd + E82557_DESC_COUNT, 60);
    e82557_wr16(rfd, E82557_RFD_C | E82557_RFD_OK);
}
unsigned inp(unsigned port) { return 0; }
unsigned inpw(unsigned port) { return E82557_RUS_READY << E82557_RUS_SHIFT; }
unsigned long inpd(unsigned port) { return 0; }
void outpd(unsigned port, unsigned long value) {}
void outp(unsigned port, unsigned value)
{
    if (port == nic.base + E82557_SCB_COMMAND && value == e82557_scb_cmd(E82557_CU_START,0)) {
        u32 issued = tick_count;

        complete_at = issued == 80 ? 101 : issued + 1;
        const u8 *wire = dma + E82557_TCB_OFF + E82557_TCB_HEADER + SFS_LAN_HDR_LEN;
        reply_type = wire[SFS_OFF_TYPE];
        reply_seq = sfs_get16(wire + SFS_OFF_SEQ);
        if (reply_type == SFS_T_HELLO) {
            pending_nonce = sfs_get32(wire + SFS_HDR_LEN + SFS_HELLO_REQ_NONCE);
            reply_at = issued < 80 ? 0 : issued == 80 ? 91 : issued + 1;
        } else if (reply_type == SFS_T_STAT) {
            reply_at = issued + 1;
        }
    }
}
void _halt(void)
{
    if (!model_if || tick_count > 210) abort();
    unsigned flags = irq_save();
    kctx_irq_depth++;
    tick_clock_step();
    if (complete_at == tick_count) {
        e82557_wr16(dma + E82557_TCB_OFF, E82557_CB_C | E82557_CB_OK);
    }

    if (reply_at && tick_count == reply_at)
        inject(tick_count == 91 ? old_peer : peer, pending_nonce);
    tick_dispatch();
    kctx_irq_depth--;
    irq_restore(flags);
}
void cpu_delay_us(u32 us) {}
u32 arch_tick_budget_cnt(u32 us) { return us; }
u32 arch_tick_budget_base(void) { return 0; }
u32 arch_tick_budget_elapsed(u32 base) { return 0; }
static int clock_read(u32 *t,u32 *s,u32 *g) { static u32 tsc; *t=++tsc; *s=*g=1; return 0; }
static int convert(u32 us,u32 s,u32 g,u32 *out) { *out=us; return 0; }
void link_init(const u8 *mac) {}
void link_tick(void) {}
void console_printf(u8 a, const char *fmt, ...) {}
static int observed_get(void *ctx)
{
    gets++;

    if (tick_count == 100) gets_at_deadline++;
    int byte = transport->get(ctx);
    if (byte >= 0 && expire_in_get) {
        _halt();
        expire_in_get = 0;
    }
    return byte;
}
static int check(const char *name, int ok, int rc)
{
    write_text(ok ? "OK " : "FAIL ");
    write_text(name);
    field(" rc=", rc);
    field(" tick=", tick_count);
    field(" gets=", gets);
    field(" rx=", lan.rx_pos);
    field("/", lan.rx_end);
    field(" prepared=", lan.prepared);
    field(" linked=", lan.linked);
    field(" peer=", lan.peer[5]);
    field(" sid=", resident.client.sid);
    field(" seq=", resident.client.seq);
    write_text("\n");
    return !ok;
}
int exercise(int module)
{
    write_text(module ? "module\n" : "resident\n");
    static const struct io_clock_ops clock = {clock_read, convert, 0};
    io_clock_set_ops(&clock);
    nic.dma.va = dma;
    nic.state = E82557_RUNNING;
    nic.mac[0] = 4;
    e82557_rx_init(rx_ring(), E82557_RFD_OFF, &nic.rx);
    tick_budget_config();

    if (module) model_load();
    if (tick_register(PC98_TICK_E82557, module ? facade_e82557_tick : e82557_tick) ||
        net_bind(&nic_ops)) abort();
    (module ? facade_sfs_lan_init : sfs_lan_init)(&lan, 0, &model_clock, 0);
    transport = module ? &facade_sfs_lan_io : &sfs_lan_io;
    SfsIo observed = *transport;
    observed.get = observed_get;
    enabled = 1;
    config.io = &observed;
    config.io_ctx = &lan;
    sfs_client_init(&resident.client, &observed, &lan, SFS_LAN_BAUD);
    resident.client.max_payload = SFS_LAN_MAX_PAYLOAD;
    SfsClient *client = &resident.client;
    int rc = sfs_ensure_session(100);
    int failed = check("expired HELLO published peer",
                       rc == OS32_ERR_HOSTDOWN && !lan.linked && !lan.peer[5] && !client->sid, rc);
    failed |= check("expired put entered get", !gets_at_deadline, rc);
    failed |= check("expired HELLO kept candidate",
                    lan.rx_pos == lan.rx_end && !lan.prepared && !lan.hello, rc);
    /* The retry at 90 has returned. The accepted TX at 80 still owns
     * its descriptor and completes at 101. */
    _halt();
    expire_in_get = 1;
    rc = sfs_ensure_session(tick_count + 2);
    failed |= check("late get published HELLO", rc == OS32_ERR_HOSTDOWN &&
                    !lan.linked && !lan.peer[5] && !client->sid && !lan.hello &&
                    !lan.prepared && lan.rx_pos == lan.rx_end, rc);
    hello_version = SFS_VERSION + 1;
    rc = sfs_ensure_session(200);
    failed |= check("invalid version published HELLO", rc == OS32_ERR_IO &&
                    !lan.linked && !lan.peer[5] && !client->sid, rc);
    hello_version = SFS_VERSION;
    hello_max = SFS_MAX_PAYLOAD - 1;
    rc = sfs_ensure_session(200);
    failed |= check("invalid payload limit published HELLO", rc == OS32_ERR_IO &&
                    !lan.linked && !lan.peer[5] && !client->sid, rc);
    hello_max = SFS_LAN_MAX_PAYLOAD;
    int (*set_peer)(SfsLan *, const char *) = module ? facade_sfs_lan_set_peer : sfs_lan_set_peer;
    if (set_peer(&lan, "02:00:00:00:00:02")) abort();
    expire_in_get = 1;
    rc = sfs_ensure_session(tick_count + 2);
    failed |= check("fixed peer lost at expiry", rc == OS32_ERR_HOSTDOWN &&
                    lan.peer_fixed && !lan.linked && peer_matches(peer), rc);
    if (set_peer(&lan, "")) abort();
    rc = sfs_ensure_session(200);
    failed |= check("new HELLO", rc == 0 && lan.linked && peer_matches(peer) && client->sid == 7, rc);
    client->seq = SFS_PERSISTENT_SEQ_LIMIT + 1;
    rc = sfs_ensure_session(200);
    failed |= check("renew", rc == 0 && !client->seq && lan.linked && client->sid == 7, rc);
    u8 data[16];
    struct SfsReply reply = {data, sizeof(data), 5};
    /* The TX descriptor for HELLO completed on its response tick. STAT
     * arrives one tick later. Advance another tick inside its first get. */
    expire_in_get = 1;
    rc = sfs_client_call_deadline(client, SFS_T_STAT, 0, 0, &reply, tick_count + 2);
    failed |= check("expired CALL kept unread reply",
                    rc == OS32_ERR_HOSTDOWN && !reply.length && lan.rx_pos == lan.rx_end &&
                    !lan.prepared && lan.linked && client->sid == 7, rc);
    rc = sfs_client_call_deadline(client, SFS_T_STAT, 0, 0, &reply, tick_count + 2);
    failed |= check("next CALL", rc == 0 && reply.length == SFS_STATUS_LEN + 1 &&
                    client->seq == 2 && data[SFS_STATUS_LEN] == client->seq, rc);
    expire_in_get = 1;
    rc = sfs_client_hello_deadline(client, 990, tick_count + 2);
    failed |= check("established peer lost at expiry", rc == OS32_ERR_HOSTDOWN && lan.linked &&
                    peer_matches(peer) && client->sid == 7 && client->seq == 2, rc);
    rc = sfs_client_hello(client, 991);
    failed |= check("foreground HELLO", rc == 0 && lan.linked &&
                    peer_matches(peer) && client->sid == 7 && !client->seq, rc);
    return failed;
}
u32 pci_cfg_read32(u32 b,u32 d,u32 f,u32 r) { abort(); }
int pci_cfg_write16(u32 b,u32 d,u32 f,u32 r,u16 v) { abort(); }
int dma_mark_leaked(const struct dma_buf *b) { abort(); }

__asm__(".global _start\n_start:\n mov (%esp),%eax\n dec %eax\n push %eax\n call exercise\n mov %eax,%ebx\n mov $1,%eax\n int $0x80");
