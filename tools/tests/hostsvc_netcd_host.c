/* D3 BLOCK: real C1/C3/D2 and netcd, virtual wire/clock/ALIVE/device table. */
#include "hostsvc_fixture.h"
#include "fs/serialfs/serialfs.h"
#include "net/host_mailbox.h"
#include "lib/kstring.h"
#include "os32_kapi_shared.h"
#include PRODUCT_SOURCE
#define REQUIRE(e) do { if (!(e)) return __LINE__; } while (0)
static struct HostAlive alive;
void host_alive_snapshot(struct HostAlive *out) { *out = alive; }
int vfs_is_mounted(const char *p) { (void)p; return 0; }
int serialfs_is_mounted(void) { return 0; }
void serialfs_permit(SfsClient *c) { (void)c; }
void serialfs_forbid(void) { }
int vfs_mount(const char *p, const char *d, const char *fs)
{ (void)p; (void)d; (void)fs; return 0; }
static Device *device;
static int reg_error;
Device *dev_find(const char *name)
{
    if (kstrcmp(name, "cd0")) __builtin_trap();
    return reg_error == OS32_ERR_EXIST ? &cd_device : device;
}
int dev_register(Device *dev)
{
    if (reg_error) return -1;
    device = dev;
    return 0;
}
static u8 response[SFS_BUFFER_FRAME];
static u32 pos, end, hello_count, opens, closes, reads, bye_count;
static u32 host_sid, handle, size_lo = 8192, size_hi, mtime_lo = 123, mtime_hi = 456;
static u32 first_deadline, wire_error, read_lba, total_read;
static int silent, reply_status, short_read, delay, inject_alive, reuse_sid, restart;
static int put(void *arg, const u8 *buf, u32 n)
{
    SfsDec dec;
    SfsFrame f;
    u8 data[SFS_BREAD_RSP_MAX] = {0};
    u16 len = SFS_STATUS_LEN;
    (void)arg;
    sfs_dec_reset(&dec);
    int ready = 0;
    for (u32 i = 0; i < n; i++) ready = sfs_dec_feed(&dec, buf[i], &f);
    if (ready != 1) __builtin_trap();
    if (f.type == SFS_T_BYE) { bye_count++; return 0; }
    if (silent) return 0;
    sfs_put32(data, reply_status);
    if (f.type == SFS_T_HELLO) {
        hello_count++;
        if (!reuse_sid) host_sid = hello_count;
        handle = 0;
        len = SFS_HELLO_RSP_LEN;
        sfs_put16(data + SFS_HELLO_RSP_VER, SFS_VERSION);
        sfs_put16(data + SFS_HELLO_RSP_MAX, SFS_LAN_MAX_PAYLOAD);
        sfs_put32(data + SFS_HELLO_RSP_NONCE, sfs_get32(f.payload + SFS_HELLO_REQ_NONCE));
    } else {
        if (f.sid != host_sid) wire_error++;
        if (f.type == SFS_T_BOPEN) {
            opens++;
            if (f.len != 7 || kstrncmp((const char *)f.payload, "install", 7)) wire_error++;
            if (!reply_status) {
                if (handle) wire_error++; /* no leaked same-session handle */
                handle = opens;
                len = SFS_BOPEN_RSP_LEN;
                sfs_put32(data + 4, handle);
                sfs_put32(data + 8, size_lo); sfs_put32(data + 12, size_hi);
                sfs_put32(data + 16, mtime_lo); sfs_put32(data + 20, mtime_hi);
            }
            if (inject_alive) { alive.observation++; inject_alive = 0; }
        } else if (f.type == SFS_T_BCLOSE) {
            closes++;
            if (!handle) sfs_put32(data, OS32_ERR_STALE);
            else if (sfs_get32(f.payload) != handle) wire_error++;
            handle = 0;
        } else if (f.type == SFS_T_BREAD) {
            u32 lba = sfs_get32(f.payload + 4);
            u16 count = sfs_get16(f.payload + 8);
            reads++;
            if (restart) {
                restart = 0; handle = 0;
                pos = 0;
                end = sfs_encode_limit(response, SFS_T_ERR, f.sid, f.seq,
                                       data, SFS_STATUS_LEN, SFS_LAN_MAX_PAYLOAD);
                return 0;
            }
            if (sfs_get32(f.payload) != handle || lba != read_lba || count < 1 || count > 2)
                wire_error++;
            read_lba += count;
            total_read += count;
            if (!reply_status) {
                len += count * SFS_LOGICAL_BLOCK_SIZE;
                for (u16 i = 4; i < len; i++) data[i] = (u8)(lba * 13 + i - 4);
                len -= short_read;
            }
        } else wire_error++;
    }
    pos = 0;
    end = sfs_encode_limit(response, f.type | SFS_T_RESP,
                          f.type == SFS_T_HELLO ? host_sid : f.sid,
                          f.seq, data, len, SFS_LAN_MAX_PAYLOAD);
    return 0;
}
static int get(void *arg) { (void)arg; return pos < end ? response[pos++] : -1; }
static u32 now(void *arg) { (void)arg; return hs_now(); }
static void idle(void *arg) { (void)arg; hs_advance(1); }
static int can_wait(void *arg) { (void)arg; return hs_if(); }
static int put_until(void *arg, const u8 *buf, u32 n, u32 deadline)
{
    if (!first_deadline) first_deadline = deadline;
    if (deadline != first_deadline) wire_error++;
    if ((i32)(hs_now() + delay - deadline) >= 0) {
        hs_advance(deadline - hs_now());
        return OS32_ERR_TIMEOUT;
    }
    hs_advance(delay);
    return put(arg, buf, n);
}
const SfsIo hs_sfs_io = {put, get, now, idle, can_wait, put_until};
static int read_at(u32 lba, int count, void *buf)
{
    first_deadline = 0; read_lba = lba; total_read = 0;
    return netcd_read(device, lba, count, buf);
}
static int check(u32 *gen)
{
    first_deadline = 0;
    return netcd_media_check(device, gen);
}
static int suite(void)
{
    struct SfsPersistentConfig cfg = {&hs_sfs_io, 0, 1};
    u8 buf[2049];
    u32 gen, old_opens, old_hello, tick;
    hs_reset(0xffffff00u);
    REQUIRE(netcd_register(0) == OS32_ERR_INVAL);
    REQUIRE(netcd_register("") == OS32_ERR_INVAL);
    char too_long[SFS_BLOCK_ALIAS_MAX + 2];
    for (u32 i = 0; i < sizeof(too_long) - 1; i++) too_long[i] = 'a';
    too_long[sizeof(too_long) - 1] = 0;
    REQUIRE(netcd_register(too_long) == OS32_ERR_INVAL);
    reg_error = OS32_ERR_FULL;
    REQUIRE(netcd_register("install") == OS32_ERR_FULL);
    reg_error = OS32_ERR_EXIST;
    REQUIRE(netcd_register("install") == OS32_ERR_EXIST);
    reg_error = 0;
    char alias[] = "install";
    REQUIRE(!netcd_register(alias)); alias[0] = 'x';
    REQUIRE(device && device->sect_size == 512 && device->total_sects == 0);
    REQUIRE(device->blk_read_u32 == netcd_read && device->media_check == netcd_media_check);
    REQUIRE(!device->blk_read && !hello_count && !opens);
    REQUIRE(netcd_register("install") == OS32_ERR_EXIST);
    REQUIRE(device->blk_write(device, 0, 1, buf) == OS32_ERR_ROFS);
    REQUIRE(check(&gen) == OS32_ERR_HOSTDOWN && gen == 1);
    REQUIRE(!sfs_persistent_start(&cfg));
    buf[2048] = 0xa5;
    REQUIRE(!read_at(0, 4, buf));
    REQUIRE(reads == 2 && total_read == 4 && device->total_sects == 16 && !wire_error);
    for (u32 i = 0; i < 2048; i++) REQUIRE(buf[i] == (u8)((i / 1024) * 26 + i % 1024));
    REQUIRE(buf[2048] == 0xa5 && hello_count == 1 && opens == 1);
    REQUIRE(!check(&gen) && gen == 1 && opens == 1);
    REQUIRE(read_at(16, 1, buf) == OS32_ERR_INVAL);
    REQUIRE(read_at(15, 2, buf) == OS32_ERR_INVAL);
    u32 old_reads = reads;
    REQUIRE(read_at(13, 4, buf) == OS32_ERR_INVAL && reads == old_reads);
    REQUIRE(read_at(0xffffffff, 2, buf) == OS32_ERR_INVAL);
    REQUIRE(read_at(0, 0, buf) == OS32_ERR_INVAL);
    /* Host restart rejects BREAD with T_ERR; retry same image in one budget. */
    old_opens = opens; old_hello = hello_count; restart = 1;
    REQUIRE(!read_at(0, 4, buf));
    REQUIRE(hello_count == old_hello + 1 && opens == old_opens + 1);
    REQUIRE(total_read == 4 && !wire_error && !check(&gen) && gen == 1);
    /* Same image after exact idle threshold, including tick wrap. */
    hs_advance(3000); old_hello = hello_count;
    REQUIRE(!check(&gen) && gen == 1 && opens == old_opens + 2 && hello_count == old_hello + 1);
    /* Same-generation ALIVE; arrival during BOPEN remains for next access. */
    alive = (struct HostAlive){7, 1, 1}; inject_alive = 1; reuse_sid = 1;
    REQUIRE(!check(&gen) && gen == 1 && cd.media.alive_seen == 1);
    old_opens = opens;
    REQUIRE(!check(&gen) && opens == old_opens + 1 && cd.media.alive_seen == 2);
    old_opens = opens;
    REQUIRE(!check(&gen) && opens == old_opens);
    reuse_sid = 0;
    /* Each identity word independently changes the media generation. */
    for (u32 i = 0; i < 4; i++) {
        if (i == 0) size_lo += 512;
        if (i == 1) size_hi++;
        if (i == 2) mtime_lo++;
        if (i == 3) mtime_hi++;
        alive.observation++;
        REQUIRE(check(&gen) == OS32_ERR_STALE && gen == i + 2);
        REQUIRE(!check(&gen) && gen == i + 2);
    }
    /* Shared /host renewed sid before CD access, without new ALIVE. */
    old_opens = opens;
    sfs_persistent_stale(); first_deadline = 0;
    REQUIRE(!sfs_ensure_session(hs_now() + 100));
    REQUIRE(!check(&gen) && gen == 5 && opens == old_opens + 1);
    /* Real seq rollover under a 100MiB sequential read, same ISO maintained. */
    size_hi = 0; size_lo = 104857600;
    alive.observation++;
    REQUIRE(check(&gen) == OS32_ERR_STALE && gen == 6);
    old_hello = hello_count; reuse_sid = 1;
    for (u32 lba = 0; lba < 204800; lba += 4) REQUIRE(!read_at(lba, 4, buf));
    REQUIRE(hello_count > old_hello && bye_count && !wire_error);
    reuse_sid = 0;
    REQUIRE(!check(&gen) && gen == 6);
    /* u32 maximum sector count and unsigned LBA. */
    size_hi = 511; size_lo = 0xfffffe00; alive.observation++;
    REQUIRE(check(&gen) == OS32_ERR_STALE && gen == 7);
    REQUIRE(device->total_sects == 0xffffffffu);
    REQUIRE(!read_at(0xfffffffeu, 1, buf));
    REQUIRE(read_at(0xfffffffeu, 2, buf) == OS32_ERR_INVAL);
    /* Warm-session reopen closes the old handle; /host may have used it recently. */
    hs_advance(2999); first_deadline = 0;
    REQUIRE(!sfs_ensure_session(hs_now() + 100));
    hs_advance(1); old_opens = opens;
    REQUIRE(!check(&gen) && opens == old_opens + 1 && closes && !wire_error);
    short_read = 1;
    REQUIRE(read_at(0, 1, buf) == OS32_ERR_IO); short_read = 0;
    old_opens = opens; reply_status = OS32_ERR_STALE;
    REQUIRE(read_at(0, 1, buf) == OS32_ERR_STALE); reply_status = 0;
    REQUIRE(!read_at(0, 1, buf) && gen == 7 && opens == old_opens + 1);
    hs_set_if(0);
    REQUIRE(read_at(0, 1, buf) == OS32_ERR_IO); hs_set_if(1);
    /* One deadline across close/open/two chunks, then recovery on next call. */
    hs_advance(3000); tick = hs_now(); delay = 30;
    REQUIRE(read_at(0, 4, buf) == OS32_ERR_HOSTDOWN && hs_now() - tick == 100);
    delay = 0;
    REQUIRE(!read_at(0, 4, buf));
    alive.observation++; silent = 1; tick = hs_now();
    REQUIRE(check(&gen) == OS32_ERR_HOSTDOWN && hs_now() - tick == 100);
    silent = 0;
    REQUIRE(!check(&gen) && gen == 7 && !wire_error);
    return 0;
}
void _start(void)
{
    int rc = suite();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
