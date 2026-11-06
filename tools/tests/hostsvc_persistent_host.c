/* C1 SFS unit: real VFS/SerialFS/client/mailbox, one virtual SfsIo.
 * C3 deadline HELLO/oneway are dependency seams until that stage lands. */
#include "hostsvc_fixture.h"
#include "fs/serialfs/serialfs.h"
#include "fs/vfs_device.h"
#include "net/host_mailbox.h"
#include "os32_kapi_shared.h"
#include "lib/kstring.h"
#define IO_H
static unsigned int irq_save(void) { unsigned int f = hs_if(); hs_set_if(0); return f; }
static void irq_restore(unsigned int f) { hs_set_if(f); }
static u8 heap[16384];
void *kmalloc(u32 n) { return n == sizeof(heap) ? heap : 0; }
#include "../../net/host_mailbox.c"
#include PRODUCT_SOURCE
/* No FD is opened by this mount/session unit. D6 owns permanent FD marking. */
void vfs_fd_invalidate_mount(void *ctx) { (void)ctx; }
static u8 response[SFS_BUFFER_FRAME];
static u32 rx_at, rx_end, hello_count, bye_count, seen_deadline, seen_nonce;
static u32 bye_deadline;
static int during_hello, bye_ticks;
static int put(void *ctx, const u8 *buf, u32 n)
{
    SfsDec dec;
    SfsFrame f;
    (void)ctx;
    sfs_dec_reset(&dec);
    for (u32 i = 0; i < n; i++) {
        if (sfs_dec_feed(&dec, buf[i], &f) != 1) continue;
        if (f.type == SFS_T_BYE) { bye_count++; hs_advance(bye_ticks); }
        if (f.type == SFS_T_HELLO) {
            u8 pl[SFS_HELLO_RSP_LEN] = {0};
            hello_count++;
            sfs_put32(pl + SFS_HELLO_RSP_NONCE, sfs_get32(f.payload + SFS_HELLO_REQ_NONCE));
            sfs_put16(pl + SFS_HELLO_RSP_VER, SFS_VERSION);
            sfs_put16(pl + SFS_HELLO_RSP_MAX, SFS_LAN_MAX_PAYLOAD);
            rx_at = 0;
            rx_end = sfs_encode_limit(response, SFS_T_HELLO | SFS_T_RESP,
                        hello_count, 0, pl, sizeof(pl), SFS_LAN_MAX_PAYLOAD);
        }
    }
    return 0;
}
static int get(void *ctx) { (void)ctx; return rx_at < rx_end ? response[rx_at++] : -1; }
static u32 now(void *ctx) { (void)ctx; return hs_now(); }
static void idle(void *ctx) { (void)ctx; hs_advance(1); }
static int can_wait(void *ctx) { (void)ctx; return hs_if(); }
static int put_until(void *ctx, const u8 *buf, u32 n, u32 deadline)
{
    /* Model a busy TX: the deadline stops waiting before any frame is sent. */
    if ((i32)(hs_now() + (u32)bye_ticks - deadline) >= 0) {
        while ((i32)(hs_now() - deadline) < 0) hs_advance(1);
        return OS32_ERR_TIMEOUT;
    }
    return put(ctx, buf, n);
}
const SfsIo hs_sfs_io = {put, get, now, idle, can_wait, put_until};
int sfs_client_oneway_deadline(SfsClient *c, u8 type, const u8 *payload,
                               u16 len, u32 deadline)
{
    u16 n = sfs_encode_limit(c->tx, type, c->sid, 0, payload, len, c->max_payload);
    bye_deadline = deadline;
    return c->io->put_until(c->ctx, c->tx, n, deadline);
}
int sfs_client_hello_deadline(SfsClient *c, u32 value, u32 deadline)
{
    int rc;
    seen_deadline = deadline;
    if (value == seen_nonce) __builtin_trap();
    seen_nonce = value;
    if (hs_failure_take("sfs.reply", &rc)) {
        while ((i32)(hs_now() - deadline) < 0) hs_advance(1);
        return rc;
    }
    if (during_hello) {
        u32 gen = 9;
        host_mailbox_put(gen, 1, 1, &gen, sizeof(gen), 1);
        during_hello = 0;
    }
    return sfs_client_hello(c, value);
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int suite(void)
{
    struct SfsPersistentConfig cfg = {&hs_sfs_io, 0, 1};
    static const struct vfs_device_name names[] = {
        {SERIALFS_DEVNAME, VFS_NAME_EXACT, VFS_DEV_SERIAL, 0, 1}
    };
    const SfsClient *client;
    void *ctx;
    char rel[VFS_MAX_PATH];
    u32 gen, sid, count, value = 7;
    HostNotice notice;
    hs_reset(0xFFFFFFF0u);
    CHECK(!sfs_persistent_client() && !sfs_persistent_enabled());
    CHECK(sfs_ensure_session(hs_now() + 100) == OS32_ERR_HOSTDOWN);
    CHECK(sfs_persistent_scope_begin() == OS32_ERR_INVAL);
    CHECK(sfs_persistent_scope_end(0) == OS32_ERR_INVAL);
    CHECK(sfs_persistent_start(0) == OS32_ERR_INVAL);
    CHECK(host_mailbox_init() == 0);
    vfs_device_names_register(names, 1);
    CHECK(serialfs_init() == 0);
    /* An existing /host must remain untouched, including its gate. */
    SfsClient other;
    sfs_client_init(&other, &hs_sfs_io, 0, 115200);
    serialfs_permit(&other);
    CHECK(vfs_mount(SERIALFS_PREFIX, SERIALFS_DEVNAME, SERIALFS_NAME) == 0);
    CHECK(sfs_persistent_start(&cfg) == OS32_ERR_BUSY);
    CHECK(vfs_route("/host/a", rel, sizeof(rel), &ctx) && ctx == &other);
    vfs_umount(SERIALFS_PREFIX);
    serialfs_forbid();
    CHECK(sfs_persistent_start(&cfg) == 0);
    cfg.lan_enabled = 0; cfg.io = 0; /* config is copied */
    CHECK(sfs_persistent_start(&cfg) == OS32_ERR_BUSY);
    client = sfs_persistent_client();
    CHECK(client && !client->sid && hello_count == 0);
    CHECK(vfs_route("/host/a", rel, sizeof(rel), &ctx) && ctx == client);
    CHECK(sfs_persistent_scope_begin() == 0);
    CHECK(sfs_persistent_scope_begin() == OS32_ERR_BUSY);
    CHECK(sfs_persistent_scope_end(23) == 0);
    CHECK(sfs_persistent_scope_end(0) == OS32_ERR_INVAL);
    CHECK(vfs_is_mounted(SERIALFS_PREFIX) && !bye_count && !hello_count);
    CHECK(sfs_ensure_session(hs_now() + 100) == 0);
    CHECK(hello_count == 1 && client->sid && client->max_payload == SFS_LAN_MAX_PAYLOAD);
    gen = sfs_persistent_generation(); sid = client->sid;
    CHECK(sfs_ensure_session(hs_now() + 100) == 0 && client->sid == sid);
    hs_advance(2999);
    CHECK(sfs_ensure_session(hs_now() + 100) == 0 && hello_count == 1);
    hs_advance(3000);
    CHECK(sfs_ensure_session(hs_now() + 100) == 0 && hello_count == 2);
    CHECK(sfs_persistent_generation() == gen && !bye_count);
    /* Application consumption must not hide ALIVE from the resident owner. */
    CHECK(host_mailbox_put(7, 1, 1, &value, sizeof(value), 1) == 0);
    CHECK(host_mailbox_stage(7, 1, &notice) == 0);
    host_mailbox_commit(7, notice.serial);
    CHECK(sfs_ensure_session(hs_now() + 100) == 0 && hello_count == 3);
    CHECK(sfs_persistent_generation() == gen);
    CHECK(host_mailbox_put(7, 1, 1, &value, sizeof(value), 1) == 0);
    CHECK(sfs_ensure_session(hs_now() + 100) == 0 && hello_count == 4);
    CHECK(sfs_persistent_generation() == gen);
    value = 8;
    CHECK(host_mailbox_put(8, 1, 1, &value, sizeof(value), 1) == 0);
    during_hello = 1;
    CHECK(sfs_ensure_session(hs_now() + 100) == 0);
    CHECK(sfs_persistent_generation() == gen + 1);
    CHECK(sfs_ensure_session(hs_now() + 100) == 0);
    CHECK(sfs_persistent_generation() == gen + 2);
    /* seq==limit is usable; next request after crossing does BYE/HELLO. */
    resident.client.seq = 0xF000;
    count = hello_count;
    CHECK(sfs_ensure_session(hs_now() + 100) == 0 && hello_count == count);
    resident.client.seq++;
    CHECK(sfs_ensure_session(hs_now() + 100) == 0 && hello_count == count + 1);
    CHECK(bye_count == 1 && bye_deadline == seen_deadline);
    CHECK(sfs_persistent_generation() == gen + 2);
    sfs_persistent_stale();
    CHECK(sfs_persistent_generation() == gen + 3 && !client->sid);
    hs_fail("sfs.reply", OS32_ERR_HOSTDOWN);
    u32 deadline = hs_now() + 100;
    CHECK(sfs_ensure_session(deadline) == OS32_ERR_HOSTDOWN);
    CHECK(seen_deadline == deadline && hs_now() == deadline && !client->sid);
    CHECK(sfs_ensure_session(hs_now() + 100) == 0 && client->sid);
    hs_set_if(0);
    CHECK(sfs_ensure_session(hs_now() + 100) == OS32_ERR_IO);
    hs_set_if(1);
    count = hello_count;
    CHECK(sfs_ensure_session(hs_now()) == OS32_ERR_HOSTDOWN && hello_count == count);
    /* Previous counterexample: TX wants 100 ticks with only 1 remaining. */
    resident.client.seq = 0xF001; bye_ticks = 100;
    deadline = hs_now() + 1;
    CHECK(sfs_ensure_session(deadline) == OS32_ERR_HOSTDOWN);
    CHECK(bye_deadline == deadline && hs_now() == deadline && bye_count == 1);
    CHECK(hello_count == count && !client->sid);
    bye_ticks = 0;
    CHECK(sfs_ensure_session(hs_now() + 100) == 0 && client->sid);
    CHECK(sfs_persistent_generation() == gen + 3);
    CHECK(vfs_is_mounted(SERIALFS_PREFIX));
    return 0;
}
void _start(void)
{
    int rc = suite();
    /* Exit with 1 for every assertion, retaining the line in eax before exit. */
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
