/* LAN_OPERATOR_DESIGN §5 (:128-132), §8 P4c/hostsvc-init (:217,223).
 * Real transport + SfsClient, fake only host API, IRQ and foreground clock. */
#include "types.h"
#include "os32_kapi_shared.h"
#include "platform/powermac/hostfs.h"
#include "platform_host.h"
#include "fs/serialfs/serialfs.h"
#include "fs/vfs_device.h"
#include "net/host_mailbox.h"
#include "net/link.h"
static int ee = 1;
int _irq_enabled(void) { return ee; }
volatile u32 kctx_irq_depth;
static u32 clock, opens, closes, writes, polls, stalls, status_code = 200;
static u32 reply_len, reply_pos, body_len, declared;
static int auto_reply;
static u32 hellos;
static void (*registered_work)(void);
int idle_work_register(void (*fn)(void)) { registered_work = fn; return 0; }
static int stall_phase = -1;
static u8 reply[SFS_BUFFER_FRAME], body[SFS_BUFFER_FRAME];
#include "io_source.inc"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
u32 link_platform_now(void) { return clock; }
void idle_work_run(void) { ++polls; ++clock; if (registered_work) registered_work(); else pm_hfs_link_poll(); }
i32 platform_host_open(const char *req, u32 n, int owner)
{
    (void)owner;
    if (stall_phase == HFS_OPEN) { ++stalls; return OS32_ERR_AGAIN; }
    if (n < 10 || req[0] != 'S' || req[8] != ' ') return -1;
    /* Review F1: like link_decl_len, the declared length must cover the
     * WDATA exactly; a short declaration makes the real link_host_write INVAL. */
    declared = 0;
    for (u32 i = 9; i < n; ++i) {
        if (req[i] < '0' || req[i] > '9') return -1;
        declared = declared * 10 + (u32)(req[i] - '0');
    }
    if (!declared || declared > 1044) return -1;
    body_len = reply_pos = 0;
    ++opens;
    return 3;
}
i32 platform_host_write(i32 h, const void *buf, u32 n, int owner)
{
    (void)h; (void)owner;
    if (stall_phase == HFS_WRITE) { ++stalls; return OS32_ERR_AGAIN; }
    if (n > 7) n = 7;
    if (body_len + n > declared) return OS32_ERR_INVAL; /* link_host_write: len > remain */
    for (u32 i = 0; i < n; ++i) body[body_len++] = ((const u8 *)buf)[i];
    ++writes;
    return (i32)n;
}
i32 platform_host_status(i32 h, u32 *status, u32 *len, int owner)
{
    (void)h; (void)owner;
    if (stall_phase == HFS_STATUS) { ++stalls; return OS32_ERR_AGAIN; }
    if (auto_reply) {
        u8 pl[SFS_HELLO_RSP_LEN] = {0};
        u16 size = SFS_STATUS_LEN;
        u8 type = body[SFS_OFF_TYPE];
        if (type == SFS_T_HELLO) {
            ++hellos; size = SFS_HELLO_RSP_LEN;
            sfs_put16(pl + SFS_HELLO_RSP_VER, SFS_VERSION);
            sfs_put16(pl + SFS_HELLO_RSP_MAX, SFS_LAN_MAX_PAYLOAD);
            sfs_put32(pl + SFS_HELLO_RSP_NONCE,
                      sfs_get32(body + SFS_HDR_LEN + SFS_HELLO_REQ_NONCE));
        } else if (type == SFS_T_STAT) {
            size += SFS_STAT_RSP_LEN;
            pl[SFS_STATUS_LEN] = SFS_KIND_FILE;
            sfs_put32(pl + SFS_STATUS_LEN + 1, 2048);
        }
        reply_len = sfs_encode(reply, type | SFS_T_RESP, 7,
                              sfs_get16(body + SFS_OFF_SEQ), pl, size);
    }
    *status = status_code; *len = reply_len;
    return 0;
}
i32 platform_host_read_stage(i32 h, u32 n, const u8 **out, int owner)
{
    (void)h; (void)owner;
    if (stall_phase == HFS_READ) { ++stalls; return OS32_ERR_AGAIN; }
    if (n > 5) n = 5;
    *out = reply + reply_pos; reply_pos += n;
    return (i32)n;
}
i32 platform_host_close(i32 h, int owner)
{ (void)h; (void)owner; ++closes; return 0; }
static void reset(void)
{
    tx_len = pending = result = 0; handle = -1;
    clock = opens = closes = writes = polls = stalls = body_len = 0;
    pm_hostfs_transport.rx_pos = pm_hostfs_transport.rx_end = 0;
    stall_phase = -1; status_code = 200;
}
static u8 notice_heap[64 * sizeof(HostNotice)];
void *kmalloc(u32 n) { return n == sizeof(notice_heap) ? notice_heap : 0; }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
int res_owner_get(void) { return 1; }
struct VfsStdinRead *exec_stdin_state(void) { return 0; }
int input_getchar_kapi(void) { return -1; }
int fd_is_redirected(int fd) { (void)fd; return 0; }
int fd_redirect_read(int fd, void *b, u32 n) { (void)fd; (void)b; (void)n; return -1; }
u32 link_hello_ok;
u8 link_peer_mac[6] = {2, 3, 4, 5, 6, 7};
static int boot_suite(void)
{
    static const struct vfs_device_name names[] = {
        {SERIALFS_DEVNAME, VFS_NAME_EXACT, VFS_DEV_SERIAL, 0, 1}
    };
    const u8 broadcast[6] = {255,255,255,255,255,255};
    reset(); auto_reply = 1;
    CHECK(host_mailbox_init() == 0);
    CHECK(!pm_hfs_peer());
    pm_hfs_bind_peer(link_peer_mac);
    const u8 *fixed = pm_hfs_link_allowed_peer();
    CHECK(fixed);
    for (u32 i = 0; i < 6; ++i) CHECK(fixed[i] == link_peer_mac[i]);
    pm_hfs_bind_peer(broadcast);
    CHECK(!pm_hfs_link_allowed_peer());
    vfs_device_names_register(names, 1);
    CHECK(pm_hostfs_boot(1000000) == 0);
    CHECK(vfs_is_mounted("/host") && hellos == 0 && opens == 0 && registered_work);
    const SfsClient *borrowed = sfs_persistent_client();
    CHECK(borrowed);
    CHECK(host_mailbox_put(1, 3, 1, 0, 0, 1) == 0);
    link_hello_ok = 1;
    CHECK(pm_hfs_peer());
    int fd = vfs_open("/host/a", O_RDONLY);
    CHECK(fd >= 3 && hellos == 1 && borrowed->sid == 7);
    CHECK(vfs_seek(fd, 0, SEEK_END) == 2048);
    u32 generation = sfs_persistent_generation();
    CHECK(host_mailbox_put(2, 3, 1, 0, 0, 1) == 0);
    CHECK(vfs_open("/host/a", O_RDONLY) == VFS_ERR_STALE);
    CHECK(sfs_persistent_scope_begin() == 0);
    int new_fd = vfs_open("/host/a", O_RDONLY);
    CHECK(new_fd >= 3);
    CHECK(hellos == 2);
    CHECK(sfs_persistent_generation() == generation + 1);
    CHECK(vfs_seek(fd, 0, SEEK_SET) == VFS_ERR_STALE);
    CHECK(vfs_seek(new_fd, 0, SEEK_END) == 2048);
    CHECK(vfs_is_mounted("/host") && borrowed->sid == 7);
    vfs_close(fd); vfs_close(new_fd);
    CHECK(sfs_persistent_scope_end(0) == 0);
    return 0;
}
static int suite(void)
{
    u8 payload[4] = {0}, frame[SFS_BUFFER_FRAME];
    u32 n = sfs_encode(frame, SFS_T_STAT, 7, 1, payload, sizeof(payload));
    reply_len = sfs_encode(reply, SFS_T_STAT | SFS_T_RESP, 7, 1, payload, sizeof(payload));
    reset();
    CHECK(pm_hfs_set_clock(1000000) == 0);
    CHECK(pm_hostfs_io.put_until(0, frame, n, 100) == 0 && !opens);
    for (u32 i = 0; i < 40 && pending; ++i) pm_hostfs_io.idle(0);
    CHECK(!pending && !result && opens == 1 && closes == 1 && writes > 1);
    CHECK(body_len == n);
    for (u32 i = 0; i < n; ++i) CHECK(body[i] == frame[i]);
    for (u32 i = 0; i < reply_len; ++i) CHECK(pm_hostfs_io.get(&pm_hostfs_transport) == reply[i]);
    CHECK(pm_hostfs_io.get(&pm_hostfs_transport) == -1);
    /* The caller's repeated put must not reopen even after completion. */
    CHECK(pm_hostfs_io.put_until(0, frame, n, 200) == 0);
    pm_hostfs_io.idle(0); CHECK(opens == 1);
    for (int phase_test = HFS_OPEN; phase_test <= HFS_READ; ++phase_test) {
        reset(); stall_phase = phase_test;
        CHECK(pm_hostfs_io.put_until(0, frame, n, 25) == 0);
        for (int i = 0; i < 50; ++i) {
            pm_hostfs_io.idle(0);
            if (clock < 25) CHECK(pm_hostfs_io.put_until(0, frame, n, 1000) == 0);
        }
        CHECK(!pending && result == OS32_ERR_TIMEOUT && stalls > 0);
        CHECK(opens == (phase_test == HFS_OPEN ? 0u : 1u));
        CHECK(pm_hostfs_io.put_until(0, frame, n, 1000) < 0);
        pm_hostfs_io.idle(0); CHECK(!pending);
    }
    reset();
    ee = 0;
    CHECK(!pm_hostfs_io.can_wait(0));
    CHECK(pm_hostfs_io.put_until(0, frame, n, 100) < 0);
    pm_hostfs_io.idle(0); CHECK(!polls && !opens);
    ee = 1; kctx_irq_depth = 1;
    CHECK(!pm_hostfs_io.can_wait(0));
    CHECK(pm_hostfs_io.put_until(0, frame, n, 100) < 0);
    pm_hostfs_io.idle(0); CHECK(!polls && !opens);
    kctx_irq_depth = 0;
    CHECK(pm_hostfs_io.put_until(0, frame, n, 0) == OS32_ERR_TIMEOUT);
    /* CRC, exact length, upper bound and unread-buffer preservation. */
    CHECK(pm_hfs_rx_frame(reply, reply_len) == 0);
    CHECK(pm_hfs_rx_frame(reply, reply_len) < 0);
    pm_hostfs_transport.rx_pos = pm_hostfs_transport.rx_end = 0;
    reply[reply_len - 1] ^= 1;
    CHECK(pm_hfs_rx_frame(reply, reply_len) < 0);
    reply[reply_len - 1] ^= 1;
    CHECK(pm_hfs_rx_frame(reply, reply_len - 1) < 0);
    sfs_put16(reply + SFS_OFF_LEN, sizeof(payload) + 1);
    sfs_put32(reply + reply_len - SFS_CRC_LEN,
              sfs_crc32(reply + SFS_OFF_TYPE, reply_len - SFS_PREAMBLE_LEN - SFS_CRC_LEN));
    CHECK(pm_hfs_rx_frame(reply, reply_len) < 0);
    CHECK(pm_hfs_rx_frame(reply, SFS_BUFFER_FRAME + 1) < 0);
    CHECK(pm_hfs_rx_frame(reply, 1) < 0);
    reset(); reply_len = 0;
    CHECK(pm_hostfs_io.put_until(0, frame, n, 100) == 0);
    for (int i = 0; i < 20 && pending; ++i) pm_hostfs_io.idle(0);
    CHECK(!pending && result == 0 && closes == 1 && pm_hostfs_io.get(&pm_hostfs_transport) < 0);
    /* A complete SfsClient call retains its entry deadline across all AGAINs. */
    reset(); stall_phase = HFS_STATUS;
    SfsClient client;
    sfs_client_init(&client, &pm_hostfs_io, &pm_hostfs_transport, 115200);
    client.sid = 7;
    struct SfsReply out = {.data = payload, .capacity = sizeof(payload)};
    CHECK(sfs_client_call_deadline(&client, SFS_T_STAT, payload, sizeof(payload), &out, 100) == OS32_ERR_HOSTDOWN);
    CHECK(clock == 100 && opens == 1);
    pm_hfs_link_poll();
    CHECK(!pending && closes == 1);
    return 0;
}
void _start(void)
{
    int rc = suite();
    if (!rc) rc = boot_suite();
    if (rc) __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(&rc), "d"(sizeof(rc)) : "memory");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}

/* Deterministic boot identity for the virtual guest. */
void boot_token_get(u8 out[16])
{
    for (unsigned int i = 0; i < 16; ++i) out[i] = (u8)(i + 1);
}
