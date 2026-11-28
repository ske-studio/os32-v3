#include "userland/shell/rshell.c"

#include "sys/net/link.h"
#include "sys/net/link_notify.h"
#include "sys/net/host_mailbox.h"
#include "remote_control.h"
#include "le_internal.h"

enum { ENTER_READ = 42000, REQUEST_SEQ = 1 };
static unsigned acked, mixed_lines, lan_commands, out_headers, out_bodies;
static HostNotice mailbox;
static int bad_exchange;

int host_mailbox_put(u32 generation, u16 service, u16 kind, const void *data,
                     u16 len, int alive)
{
    if (mailbox.length || len > sizeof(mailbox.data)) return OS32_ERR_NOSPC;
    mailbox.host_generation = generation;
    mailbox.service = service;
    mailbox.kind = kind;
    mailbox.length = len;
    memcpy(mailbox.data, data, len);
    return 0;
}

int link_notify_control(u8 op, u32 seq, u16 window)
{
    if (op == LINK_OP_ACK && seq == REQUEST_SEQ) ++acked;
    return 0;
}

static void receive_request(void)
{
    static const char command[] = "0000002a ping\n";
    u8 packet[LINK_NOTIFY_DATA_OFF + sizeof(command) - 1] = {0};
    le_write32(packet + LINK_NOTIFY_HOST_GEN_OFF, 1);
    le_write16(packet + LINK_NOTIFY_SERVICE_OFF, REMOTE_SVC_RSHELL);
    le_write16(packet + LINK_NOTIFY_KIND_OFF, REMOTE_NOTIFY_EXECUTE);
    le_write16(packet + LINK_NOTIFY_LENGTH_OFF, sizeof(command) - 1);
    memcpy(packet + LINK_NOTIFY_DATA_OFF, command, sizeof(command) - 1);
    link_notify_rx(REQUEST_SEQ, packet, sizeof(packet));
    link_notify_tick(0);
}

/* One pending repeat per keyboard. Model arrivals independently of pumps. */
static unsigned reads, due, delivered, pending, ready;

static int uart_char(void)
{
    ++reads;
    if (reads == 2) return 's';
    if (reads == 800) receive_request();
    if (reads <= 4096 && reads % 1024 == 0) {
        ++due;
        pending = 1;
    }
    if (reads == 3000) return 'b';
    if (reads == ENTER_READ) {
        if (acked != 1 || !mailbox.length || out_headers) bad_exchange = 1;
        return '\r';
    }
    return -1;
}

static int pump(unsigned int budget)
{
    if (budget && pending) {
        pending = 0;
        ++ready;
        ++delivered;
    }
    return 0;
}

static int local_char(void)
{
    if (reads > ENTER_READ + 8) return 0x1b;
    if (!ready) return -1;
    --ready;
    return 'a';
}

static int read_notice(u32 generation, u32 service, HostNotice *out)
{
    if (!mailbox.length || generation != mailbox.host_generation || service != mailbox.service)
        return OS32_ERR_AGAIN;
    *out = mailbox;
    mailbox.length = 0;
    return 0;
}

static const char ready_reply[] = "gen 1 svc 65534";
enum { READY_HANDLE = 1, OUT_HANDLE };

static i32 host_open(const char *req, u32 len)
{
    if (!strncmp(req, "RSH READY ", 10)) return READY_HANDLE;
    ++out_headers;
    if (strcmp(req, "RSH OUT 00000000000000000000000000000000 0000002a 0 1") ||
        reads <= ENTER_READ || mixed_lines != 1 || lan_commands != 1)
        bad_exchange = 1;
    return OUT_HANDLE;
}

static i32 host_write(i32 h, const void *data, u32 len)
{
    if (h != OUT_HANDLE || len != 1 || *(const u8 *)data != 4) bad_exchange = 1;
    ++out_bodies;
    return len;
}

static i32 host_status(i32 h, u32 *status, u32 *len)
{
    *status = 200;
    *len = h == READY_HANDLE ? sizeof(ready_reply) - 1 : 0;
    return 0;
}

static i32 host_read(i32 h, void *data, u32 len)
{
    if (h != READY_HANDLE || len != sizeof(ready_reply) - 1) return OS32_ERR_IO;
    memcpy(data, ready_reply, len);
    return len;
}

int execute_command(const char *line)
{
    if (!strcmp(line, "saabaa")) ++mixed_lines;
    else if (!strcmp(line, "ping")) ++lan_commands;
    return 0;
}

static u32 clock_tick(void) { static u32 tick; return ++tick; }
static int serial_status(u32 *mode, u32 *baud, u32 *fifo) { return 0; }
static int initialized(void) { return 1; }
static int put_char(u8 ch) { return KAPI_SER_TX_OK; }
static void active(int on) {}
static void quiet(void) {}
static void print(u8 attr, const char *fmt, ...) {}
static void boot_token(u8 *out) { memset(out, 0, 16); }
static i32 close_handle(i32 h) { return 0; }
static int capture(u32 op) { return 0; }
static i32 sink_stat(u32 *n, u32 *drops) { *n = *drops = 0; return 0; }
static i32 sink_read(void *buf, u32 cap) { return 0; }
int sh_in_sfs;
void sh_status_set(int status) {}
void sh_refuse(const char *what, int limit) {}
int sh_refused_take(void) { return 0; }

static KernelAPI api = {
    .io_pump = pump, .kbd_trygetchar_local = local_char, .serial_trygetchar = uart_char,
    .host_notice_read = read_notice, .serial_get_status = serial_status,
    .serial_is_initialized = initialized, .serial_putchar = put_char,
    .rshell_set_active = active, .get_tick = clock_tick, .buz_off = quiet, .kprintf = print,
    .boot_token_get = boot_token, .host_open = host_open, .host_write = host_write,
    .host_status = host_status, .host_read = host_read, .host_close = close_handle,
    .con_sink_capture = capture, .con_sink_stat = sink_stat, .con_sink_read = sink_read
};
KernelAPI *g_api = &api;

int probe(void)
{
    link_notify_session(1, 1, 1, 1, 0);
    cmd_rshell_resident(0, NULL);
    if (due != 4 || delivered != due || mixed_lines != 1) return 1;
    if (bad_exchange || lan_commands != 1 || out_headers != 1 || out_bodies != 1) return 2;
    return 0;
}
