/* BLOCK: real protocol/client/deadlines, virtual SFS host and C1 boundary. */
#include "hostsvc_fixture.h"
#include "fs/serialfs/serialfs.h"
#include "fs/serialfs/sfs_block.h"
#include "os32_kapi_shared.h"
#define REQUIRE(e) do { if (!(e)) return __LINE__; } while (0)
static SfsClient client;
static u8 rx[SFS_BUFFER_FRAME];
static u32 pos, end, sends, ensured, sent_deadline, renew, delay, stale;
static u32 size_lo, size_hi, mtime_lo, mtime_hi, expected_lba;
static u16 expected_count;
static int status, adjust, unknown, silent, wire_error;
static int put(void *ctx, const u8 *buf, u32 n)
{
    SfsDec dec;
    SfsFrame f;
    u8 data[SFS_BREAD_RSP_MAX] = {0};
    u16 len = SFS_STATUS_LEN;
    (void)ctx;
    sfs_dec_reset(&dec);
    int ready = 0;
    for (u32 i = 0; i < n; i++) ready = sfs_dec_feed(&dec, buf[i], &f);
    if (ready != 1) __builtin_trap();
    sends++;
    if (silent) return 0;
    sfs_put32(data, (u32)status);
    if (f.type == SFS_T_BOPEN) {
        if (f.len != 7 || f.payload[0] != 'i' || f.payload[6] != 'l') wire_error++;
        if (!status) {
            len = SFS_BOPEN_RSP_LEN;
            sfs_put32(data + 4, 0x12345678);
            sfs_put32(data + 8, size_lo);
            sfs_put32(data + 12, size_hi);
            sfs_put32(data + 16, mtime_lo);
            sfs_put32(data + 20, mtime_hi);
        }
    } else if (f.type == SFS_T_BREAD) {
        if (f.len != 10 || sfs_get32(f.payload) != 0x12345678 ||
            sfs_get32(f.payload + 4) != expected_lba ||
            sfs_get16(f.payload + 8) != expected_count) wire_error++;
        if (!status) {
            len += expected_count * SFS_LOGICAL_BLOCK_SIZE;
            for (u16 i = 4; i < len; i++) data[i] = (u8)(i * 7);
        }
    } else if (f.type == SFS_T_BCLOSE) {
        if (f.len != 4 || sfs_get32(f.payload) != 0x12345678) wire_error++;
    } else wire_error++;
    pos = 0;
    end = sfs_encode_limit(rx, unknown ? SFS_T_ERR : f.type | SFS_T_RESP,
                          f.sid, f.seq, data, len + adjust, SFS_LAN_MAX_PAYLOAD);
    return 0;
}
static int get(void *ctx) { (void)ctx; return pos < end ? rx[pos++] : -1; }
static u32 now(void *ctx) { (void)ctx; return hs_now(); }
static void idle(void *ctx) { (void)ctx; hs_advance(1); }
static int can_wait(void *ctx) { (void)ctx; return hs_if(); }
static int put_until(void *ctx, const u8 *buf, u32 n, u32 deadline)
{ sent_deadline = deadline; return put(ctx, buf, n); }
const SfsIo hs_sfs_io = {put, get, now, idle, can_wait, put_until};
int sfs_ensure_session(u32 deadline)
{
    ensured = deadline;
    hs_advance(delay);
    if (renew) client.sid = renew;
    return 0;
}
const SfsClient *sfs_persistent_client(void) { return &client; }
void sfs_persistent_stale(void) { stale++; }
static void reset(void)
{
    hs_reset(0xfffffff0u);
    sfs_client_init(&client, &hs_sfs_io, 0, 115200);
    client.sid = 77;
    client.max_payload = SFS_LAN_MAX_PAYLOAD;
    client.dec.max_payload = SFS_LAN_MAX_PAYLOAD;
    pos = end = sends = ensured = sent_deadline = renew = delay = stale = 0;
    size_lo = 4096; size_hi = 0; mtime_lo = 0x89abcdef; mtime_hi = 0x12345678;
    expected_lba = 2; expected_count = 2;
    status = adjust = unknown = silent = wire_error = 0;
}
static int suite(void)
{
    struct SfsBlock b, other;
    u8 data[SFS_LOGICAL_BLOCK_SIZE * 2 + 1];
    u32 deadline;
    reset(); deadline = hs_now() + 100;
    REQUIRE(sfs_bopen("install", 7, &b, deadline) == 0);
    REQUIRE(b.sid == 77 && b.handle == 0x12345678 && b.size[0] == 4096 && !b.size[1]);
    REQUIRE(b.mtime[0] == 0x89abcdef && b.mtime[1] == 0x12345678);
    REQUIRE(ensured == deadline && sent_deadline == deadline && !wire_error);
    size_lo = 8192; size_hi = 1; mtime_lo++; mtime_hi++;
    REQUIRE(!sfs_bopen("install", 7, &other, deadline));
    REQUIRE(other.size[0] == 8192 && other.size[1] == 1 &&
            other.mtime[0] == 0x89abcdf0 && other.mtime[1] == 0x12345679);
    data[1024] = 0xa5;
    REQUIRE(!sfs_bread(&b, 2, 2, data, 1024, deadline));
    for (u32 i = 0; i < 1024; i++) REQUIRE(data[i] == (u8)((i + 4) * 7));
    REQUIRE(data[1024] == 0xa5 && !wire_error);
    expected_count = 1;
    REQUIRE(!sfs_bread(&b, 2, 1, data, 512, deadline));
    REQUIRE(!sfs_bclose(&b, deadline) && !wire_error);
    u32 before = sends;
    REQUIRE(sfs_bopen("", 0, &other, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bopen("a", 64, &other, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bopen("i\0stall", 7, &other, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bopen(0, 7, &other, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bopen("install", 7, 0, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bread(&b, 0, 0, data, 1024, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bread(&b, 0, 3, data, 1024, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bread(&b, 0, 2, data, 1023, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bread(&b, 8, 1, data, 1024, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bread(&b, 7, 2, data, 1024, deadline) == OS32_ERR_INVAL);
    REQUIRE(sfs_bread(&b, 0xffffffff, 2, data, 1024, deadline) == OS32_ERR_INVAL);
    REQUIRE(sends == before);
    /* Maximum representable medium, including unsigned LBA beyond INT_MAX. */
    size_hi = 511; size_lo = 0xfffffe00;
    REQUIRE(!sfs_bopen("install", 7, &other, deadline));
    expected_lba = 0xfffffffe;
    REQUIRE(!sfs_bread(&other, expected_lba, 1, data, 512, deadline));
    size_hi = 512;
    REQUIRE(sfs_bopen("install", 7, &other, deadline) == OS32_ERR_IO);
    size_hi = 0; size_lo = 513;
    REQUIRE(sfs_bopen("install", 7, &other, deadline) == OS32_ERR_IO);
    REQUIRE(other.size[1] == 511 && other.size[0] == 0xfffffe00);
    expected_lba = 2;
    for (int bad = -1; bad <= 1; bad += 2) {
        adjust = bad; data[0] = 0xa5;
        REQUIRE(sfs_bread(&b, 2, 1, data, 512, deadline) == OS32_ERR_IO);
        REQUIRE(data[0] == 0xa5);
    }
    size_lo = 4096;
    for (int bad = -1; bad <= 1; bad += 2) {
        adjust = bad;
        REQUIRE(sfs_bopen("install", 7, &other, deadline) == OS32_ERR_IO);
        REQUIRE(sfs_bclose(&b, deadline) == OS32_ERR_IO);
    }
    status = OS32_ERR_IO; adjust = 1;
    REQUIRE(sfs_bread(&b, 2, 1, data, 512, deadline) == OS32_ERR_IO);
    adjust = 0; status = OS32_ERR_STALE;
    REQUIRE(sfs_bread(&b, 2, 1, data, 512, deadline) == OS32_ERR_STALE);
    status = OS32_ERR_HOSTDOWN;
    REQUIRE(sfs_bopen("install", 7, &other, deadline) == OS32_ERR_HOSTDOWN);
    REQUIRE(sfs_bclose(&b, deadline) == OS32_ERR_HOSTDOWN);
    status = 1;
    REQUIRE(sfs_bclose(&b, deadline) == OS32_ERR_IO);
    status = 0; renew = 88; before = sends;
    REQUIRE(sfs_bread(&b, 2, 1, data, 512, deadline) == OS32_ERR_STALE);
    REQUIRE(sfs_bclose(&b, deadline) == OS32_ERR_STALE && sends == before);
    size_lo = 4096;
    REQUIRE(!sfs_bopen("install", 7, &other, deadline) && other.sid == 88);
    renew = 0; unknown = 1;
    REQUIRE(sfs_bclose(&other, deadline) == OS32_ERR_STALE && stale == 1);
    reset(); delay = 90; silent = 1; deadline = hs_now() + 100;
    REQUIRE(sfs_bopen("install", 7, &other, deadline) == OS32_ERR_HOSTDOWN);
    REQUIRE(hs_now() == deadline && sends == 1 && sent_deadline == deadline && ensured == deadline);
    return 0;
}
void _start(void)
{
    int rc = suite();
    /* Keep line numbers observable through the process exit status. */
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
