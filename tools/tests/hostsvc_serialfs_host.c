/* PACK ppc-sfs-deadline PM conditions A/B; LAN_OPERATOR_DESIGN §5 (:132).
 * Each CALL gets 1000ms, same-generation ALIVE preserves the session.
 * Real VFS, resident owner and deadline client; virtual wire/clock only. */
#include "hostsvc_fixture.h"
#include "fs/serialfs/serialfs.h"
#include "fs/vfs_device.h"
#include "net/host_mailbox.h"
#include "os32_kapi_shared.h"
#define IO_H
static unsigned int irq_save(void) { unsigned int f = hs_if(); hs_set_if(0); return f; }
static void irq_restore(unsigned int f) { hs_set_if(f); }
static u8 heap[16384];
void *kmalloc(u32 n) { return n == sizeof(heap) ? heap : 0; }
#include "../../net/host_mailbox.c"
#include "../../fs/serialfs/serialfs_persistent.c"
void vfs_fd_invalidate_mount(void *ctx) { (void)ctx; }
void vfs_fd_stale_mount(void *ctx) { (void)ctx; }
static u8 response[SFS_BUFFER_FRAME];
static u32 pos, end, calls, hellos, budget, delay, hello_delay, disappear_after;
static int status, silent, unknown, malformed, short_stat, bad_budget, extra, paged;
static u16 advertised = SFS_LAN_MAX_PAYLOAD;
static int put(void *ctx, const u8 *buf, u32 n)
{
    SfsDec dec;
    SfsFrame f;
    (void)ctx;
    sfs_dec_reset(&dec);
    dec.max_payload = SFS_LAN_MAX_PAYLOAD;
    for (u32 i = 0; i < n; i++) {
        if (sfs_dec_feed(&dec, buf[i], &f) != 1) continue;
        u8 pl[SFS_LAN_MAX_PAYLOAD] = {0};
        u16 len = SFS_STATUS_LEN;
        u32 sid = f.sid;
        u8 type = f.type | SFS_T_RESP;
        if (silent || (disappear_after && calls >= disappear_after)) return 0;
        if (f.type == SFS_T_HELLO) {
            hellos++; sid = hellos;
            hs_advance(hello_delay);
            sfs_put16(pl + SFS_HELLO_RSP_VER, SFS_VERSION);
            sfs_put16(pl + SFS_HELLO_RSP_MAX, advertised);
            sfs_put32(pl + SFS_HELLO_RSP_NONCE, sfs_get32(f.payload + SFS_HELLO_REQ_NONCE));
            len = SFS_HELLO_RSP_LEN;
        } else {
            calls++;
            hs_advance(delay);
            sfs_put32(pl, (u32)status);
            if (!status && f.type == SFS_T_STAT) {
                len += SFS_STAT_RSP_LEN - short_stat;
                pl[SFS_STATUS_LEN] = SFS_KIND_FILE;
                sfs_put32(pl + SFS_STATUS_LEN + 1, 2048);
            }
            if (!status && f.type == SFS_T_READ) {
                u16 want = sfs_get16(f.payload + SFS_READ_COUNT_OFF);
                if (want > advertised - SFS_STATUS_LEN) __builtin_trap();
                sfs_put32(pl, want); len += want;
                for (u16 j = SFS_STATUS_LEN; j < len; j++) pl[j] = 0x5a;
            }
            if (!status && f.type == SFS_T_WRITE) {
                u16 count = f.len - SFS_WRITE_HDR_LEN - f.payload[SFS_WRITE_HDR_LEN] - 1;
                sfs_put32(pl, count);
            }
            if (!status && f.type == SFS_T_LIST) {
                len += SFS_LIST_RSP_COOKIE_LEN;
                u32 cookie = sfs_get32(f.payload);
                if (paged && cookie < 3) sfs_put32(pl + SFS_STATUS_LEN, cookie + 1);
            }
            if (extra) len++;
            if (unknown) {
                type = SFS_T_ERR;
                sfs_put32(pl, (u32)OS32_ERR_STALE);
                if (unknown == 2) unknown = 0;
            }
            if (malformed) len = 1;
        }
        pos = 0;
        end = sfs_encode_limit(response, type, sid, f.seq, pl, len, SFS_LAN_MAX_PAYLOAD);
    }
    return 0;
}
static int get(void *ctx) { (void)ctx; return pos < end ? response[pos++] : -1; }
static u32 now(void *ctx) { (void)ctx; return hs_now(); }
static void idle(void *ctx) { (void)ctx; hs_advance(1); }
static int can_wait(void *ctx) { (void)ctx; return hs_if(); }
static int put_until(void *ctx, const u8 *buf, u32 n, u32 deadline)
{
    if (!silent && !(disappear_after && calls >= disappear_after) &&
        deadline != hs_now() + 100) bad_budget = 1;
    budget = deadline;
    return put(ctx, buf, n);
}
const SfsIo hs_sfs_io = {put, get, now, idle, can_wait, put_until};
static void cb(const VfsDirEntry *e, void *ctx) { (void)e; (void)ctx; }
#define CHECK(e) do { if (!(e)) return __LINE__; } while (0)
static int suite(void)
{
    static const struct vfs_device_name names[] = {
        {SERIALFS_DEVNAME, VFS_NAME_EXACT, VFS_DEV_SERIAL, 0, 1}
    };
    struct SfsPersistentConfig cfg = {.io = &hs_sfs_io, .lan_enabled = 1};
    OS32_Stat st;
    static u8 data[65536];
    u32 gen, before;
    hs_reset(0xfffffff0u);
    CHECK(host_mailbox_init() == 0);
    vfs_device_names_register(names, 1);
    CHECK(serialfs_init() == 0 && sfs_persistent_start(&cfg) == 0);
    VfsOps *ops = serialfs_get_ops();
    void *ctx = (void *)sfs_persistent_client();
    CHECK(ops->read_stream(ctx, "/a", data, 0, 0) == 0);
    CHECK(ops->write_stream(ctx, "/a", data, 0, 0) == 0);
    CHECK(!hellos && !calls);
    CHECK(ops->stat(ctx, "/a", &st) == 0 && st.st_size == 2048);
    CHECK(hellos == 1 && calls == 1 && budget == 84);
    CHECK(vfs_stat("/host/a", &st) == 0);
    int errors[] = {OS32_ERR_HOSTDOWN, OS32_ERR_TIMEOUT, OS32_ERR_STALE, OS32_ERR_ROFS, -1234};
    for (u32 i = 0; i < sizeof(errors)/sizeof(errors[0]); i++) {
        status = errors[i];
        int want = status == -1234 ? OS32_ERR_IO : status;
        CHECK(ops->stat(ctx, "/a", &st) == want);
        CHECK(ops->mkdir(ctx, "/a") == want);
        CHECK(ops->rmdir(ctx, "/a") == want);
        CHECK(ops->unlink(ctx, "/a") == want);
        CHECK(ops->rename(ctx, "/a", "/b") == want);
        CHECK(ops->get_file_size(ctx, "/a", &gen) == want);
        CHECK(ops->list_dir(ctx, "/a", cb, 0) == want);
        CHECK(ops->read_file(ctx, "/a", data, 1) == want);
        CHECK(ops->write_file(ctx, "/a", data, 1) == want);
    }
    status = 0; short_stat = 1;
    CHECK(ops->stat(ctx, "/a", &st) == OS32_ERR_IO);
    short_stat = 0; extra = 1;
    CHECK(ops->mkdir(ctx, "/a") == OS32_ERR_IO);
    CHECK(ops->rename(ctx, "/a", "/b") == OS32_ERR_IO);
    CHECK(ops->write_file(ctx, "/a", data, 1) == OS32_ERR_IO);
    extra = 0; malformed = 1;
    CHECK(ops->stat(ctx, "/a", &st) == OS32_ERR_IO);
    malformed = 0; silent = 1; budget = 0; before = hs_now();
    CHECK(ops->stat(ctx, "/a", &st) == OS32_ERR_HOSTDOWN);
    CHECK(hs_now() - before == 100);
    silent = 0; budget = 0;
    CHECK(ops->stat(ctx, "/a", &st) == 0);
    gen = sfs_persistent_generation(); unknown = 2; budget = 0;
    u32 old_sid = sfs_persistent_client()->sid;
    CHECK(ops->stat(ctx, "/a", &st) == 0);
    CHECK(sfs_persistent_generation() == gen + 1 && sfs_persistent_client()->sid != old_sid);
    gen = sfs_persistent_generation(); unknown = 1;
    CHECK(ops->write_file(ctx, "/a", data, 1) == OS32_ERR_STALE);
    CHECK(sfs_persistent_generation() == gen + 2 && !sfs_persistent_client()->sid);
    unknown = 0;
    CHECK(ops->resource_refresh(ctx) == 0 && sfs_persistent_client()->sid);
    CHECK(ops->resource_check(ctx, &gen) == 0 && gen == sfs_persistent_generation());
    sfs_alive_observe(1); sfs_alive_observe(2);
    CHECK(ops->resource_check(ctx, &before) == 0 && before == gen + 1);
    silent = 1; budget = 0; before = hs_now(); sfs_persistent_stale();
    CHECK(ops->resource_check(ctx, &gen) == OS32_ERR_HOSTDOWN);
    CHECK(hs_now() - before == 100);
    budget = 0; before = hs_now();
    CHECK(ops->resource_refresh(ctx) == OS32_ERR_HOSTDOWN);
    CHECK(hs_now() - before == 100);
    silent = 0;
    /* Slow CALLs exceed an operation budget, including wrap and HELLO. */
    sfs_persistent_stale(); budget = 0; hello_delay = 40; delay = 40;
    bad_budget = 0;
    before = hs_now();
    CHECK(ops->read_stream(ctx, "/a", data, 2048, 0) == 2048);
    CHECK(hs_now() - before > 100 && !bad_budget);
    CHECK(data[0] == 0x5a && data[2047] == 0x5a);
    CHECK(ops->write_file(ctx, "/a", data, 2048) == 2048);
    CHECK(ops->read_stream(ctx, "/a", data, sizeof(data), 0) == sizeof(data));
    CHECK(data[65535] == 0x5a);
    CHECK(ops->write_file(ctx, "/a", data, sizeof(data)) == sizeof(data));
    CHECK(!bad_budget);
    paged = 1; before = hs_now();
    CHECK(ops->list_dir(ctx, "/a", cb, 0) == 0);
    CHECK(hs_now() - before == 160 && !bad_budget);
    paged = 0;
    /* Disappearance after two successful chunks: exactly one fresh budget. */
    for (int writing = 0; writing < 2; writing++) {
        u32 initial_calls = calls;
        disappear_after = calls + 2; before = hs_now();
        int rc = writing ? ops->write_file(ctx, "/a", data, sizeof(data)) :
            ops->read_stream(ctx, "/a", data, sizeof(data), 0);
        CHECK(rc == OS32_ERR_HOSTDOWN);
        CHECK(calls == initial_calls + 2 && hs_now() - before == 180);
        CHECK(!bad_budget);
        disappear_after = 0;
    }
    hello_delay = delay = 0;
    /* First ALIVE, repeated ALIVE, and changed generation. */
    CHECK(ops->resource_refresh(ctx) == 0);
    sfs_alive_observe(42);
    CHECK(ops->resource_refresh(ctx) == 0);
    u32 saved_hellos = hellos, saved_gen = sfs_persistent_generation();
    sfs_alive_observe(42);
    CHECK(ops->resource_refresh(ctx) == 0 && hellos == saved_hellos);
    CHECK(sfs_persistent_generation() == saved_gen);
    sfs_alive_observe(43);
    CHECK(ops->resource_refresh(ctx) == 0 && hellos == saved_hellos + 1);
    CHECK(sfs_persistent_generation() == saved_gen + 1);
    delay = 0; budget = 0; advertised = SFS_MAX_PAYLOAD; sfs_persistent_stale();
    CHECK(ops->read_stream(ctx, "/a", data, sizeof(data), 0) == sizeof(data));
    CHECK(ops->write_file(ctx, "/a", data, sizeof(data)) == sizeof(data));
    /* UART keeps the legacy call, with no resident ensure/clock budget. */
    SfsClient uart;
    sfs_client_init(&uart, &hs_sfs_io, 0, 115200); uart.sid = 77;
    budget = 0;
    CHECK(ops->stat(&uart, "/a", &st) == 0 && !budget);
    CHECK(ops->resource_check(&uart, &gen) == 0 && !gen);
    CHECK(ops->resource_refresh(&uart) == 0);
    return 0;
}
void _start(void)
{
    int rc = suite();
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
