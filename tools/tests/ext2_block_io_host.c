/* ILP32 product superblock I/O against a Device model; unused mount code is
 * discarded by the linker, not replaced by another block algorithm. */
#include "ext2_priv.h"
#include "kprintf.h"

static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) { }
}

static void report(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}

static void fail(unsigned line)
{
    char number[12];
    unsigned i = sizeof(number) - 1;
    number[i] = 0;
    do { number[--i] = '0' + line % 10; line /= 10; } while (line);
    report("FAIL ext2 block I/O line "); report(number + i); report("\n");
    finish(1);
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)

void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

#define DISK_SECTORS 256u
static u8 disk[DISK_SECTORS * EXT2_SECTOR_SIZE];
static struct { u8 before[8]; u8 data[32 * EXT2_BLOCK_SIZE]; u8 after[8]; } buffer;
static Device device;
static Ext2Ctx ctx;
static unsigned calls, writes, fail_call, fail_after;
static u32 last_lba;
static int last_count;
static int map_high_lba;
static const int device_error = -71;

static int transfer(Device *dev, u32 lba, int count, void *dst, const void *src)
{
    u32 i, n = (u32)count * EXT2_SECTOR_SIZE;
    int failed;
    CHECK(dev == &device && count > 0);
    calls++; writes += src != 0;
    last_lba = lba; last_count = count;
    if (map_high_lba && lba == U32_MAX - 1) lba = 0;
    CHECK(lba < DISK_SECTORS && (u32)count <= DISK_SECTORS - lba);
    failed = calls == fail_call;
    if (failed && n > fail_after) n = fail_after;
    for (i = 0; i < n; i++) {
        if (src) disk[lba * EXT2_SECTOR_SIZE + i] = ((const u8 *)src)[i];
        else ((u8 *)dst)[i] = disk[lba * EXT2_SECTOR_SIZE + i];
    }
    return failed ? device_error : 0;
}

int dev_blk_read_lba(Device *dev, u32 lba, int count, void *dst)
{
    return transfer(dev, lba, count, dst, 0);
}

int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *src)
{
    return transfer(dev, lba, count, 0, src);
}

static void reset(void)
{
    u32 i;
    ctx.dev = &device; ctx.base_lba = 7; ctx.part_len = 160;
    ctx.mounted = 0; ctx.fs_error = 0;
    calls = writes = fail_call = fail_after = map_high_lba = 0;
    for (i = 0; i < sizeof(disk); i++) disk[i] = (u8)(i * 31u + (i >> 9));
    for (i = 0; i < sizeof(buffer); i++) ((u8 *)&buffer)[i] = 0xCD;
}

static void canaries(u32 bytes)
{
    u32 i;
    for (i = 0; i < sizeof(buffer.before); i++) CHECK(buffer.before[i] == 0xCD);
    for (i = bytes; i < sizeof(buffer.data); i++) CHECK(buffer.data[i] == 0xCD);
    for (i = 0; i < sizeof(buffer.after); i++) CHECK(buffer.after[i] == 0xCD);
}

static void ranges(void)
{
    static const u32 sizes[] = { 1, 2, 32 };
    u32 k, i;
    for (k = 0; k < 3; k++) {
        u32 n = sizes[k], bytes = n * EXT2_BLOCK_SIZE;
        reset();
        CHECK(ext2_read_data_blocks(&ctx, 3, n, buffer.data) == 0);
        CHECK(calls == 1 && !writes && last_lba == 13 && last_count == (int)n * 2);
        for (i = 0; i < bytes; i++) CHECK(buffer.data[i] == disk[13 * EXT2_SECTOR_SIZE + i]);
        canaries(bytes);
        calls = 0; disk[13 * EXT2_SECTOR_SIZE] ^= 0x5A;
        CHECK(ext2_read_data_blocks(&ctx, 3, n, buffer.data) == 0 && calls == 1);
        CHECK(buffer.data[0] == disk[13 * EXT2_SECTOR_SIZE]);
        for (i = 0; i < bytes; i++) buffer.data[i] = (u8)(i * 53u + 7u);
        calls = 0;
        CHECK(ext2_write_data_blocks(&ctx, 3, n, buffer.data) == 0);
        CHECK(calls == 1 && writes == 1 && last_lba == 13 && last_count == (int)n * 2);
        for (i = 0; i < bytes; i++) CHECK(disk[13 * EXT2_SECTOR_SIZE + i] == buffer.data[i]);
        CHECK(disk[13 * EXT2_SECTOR_SIZE - 1] == (u8)((13 * EXT2_SECTOR_SIZE - 1) * 31u + 12));
        CHECK(disk[13 * EXT2_SECTOR_SIZE + bytes] == (u8)((13 * EXT2_SECTOR_SIZE + bytes) * 31u + 13 + n * 2));
        canaries(bytes);
    }
    reset(); CHECK(ext2_read_data_block(&ctx, 3, buffer.data) == 0 && calls == 1 && last_count == 2);
    reset(); CHECK(ext2_read_block(&ctx, 3, buffer.data) == 0 && calls == 1 && last_count == 2);
    /* Existing block writes keep their ordered two single-sector calls. */
    reset(); CHECK(ext2_write_data_block(&ctx, 3, buffer.data) == 0 && calls == 2 && last_lba == 14 && last_count == 1);
    reset(); CHECK(ext2_write_block(&ctx, 3, buffer.data) == 0 && calls == 2);
    reset(); CHECK(ext2_read_sector(&ctx, 3, 1, buffer.data) == 0 && calls == 1 && last_lba == 14 && last_count == 1);
    reset(); CHECK(ext2_write_sector(&ctx, 3, 0, buffer.data) == 0 && calls == 1 && last_lba == 13 && last_count == 1);
}

static void reject(u32 first, u32 blocks)
{
    CHECK(ext2_read_data_blocks(&ctx, first, blocks, buffer.data) == EXT2_ERR_IO);
    CHECK(ext2_write_data_blocks(&ctx, first, blocks, buffer.data) == EXT2_ERR_IO);
    CHECK(!calls && !writes && !ctx.fs_error);
}

static void bounds(void)
{
    reset(); reject(3, 0); reject(3, 33); reject(80, 1); reject(79, 2);
    reject(U32_MAX, 1); reject(0x80000000UL, 2);
    ctx.part_len = 161; reject(80, 1);
    ctx.base_lba = U32_MAX - 1;
    reject(1, 1); reject(0, 2);
    CHECK(ext2_read_sector(&ctx, 1, 0, buffer.data) == EXT2_ERR_IO);
    CHECK(ext2_write_sector(&ctx, 1, 0, buffer.data) == EXT2_ERR_IO);
    CHECK(!calls);
    CHECK(ext2_read_sector(&ctx, 0, 2, buffer.data) == EXT2_ERR_IO);
    CHECK(ext2_write_sector(&ctx, 0, 2, buffer.data) == EXT2_ERR_IO && !calls);
    /* A range ending at inclusive U32_MAX is valid. Map just that address in
     * the Device model, while still asserting the actual submitted LBA. */
    map_high_lba = 1;
    CHECK(ext2_read_data_blocks(&ctx, 0, 1, buffer.data) == 0);
    CHECK(calls == 1 && last_lba == U32_MAX - 1 && last_count == 2);
    CHECK(ext2_write_data_blocks(&ctx, 0, 1, buffer.data) == 0 && calls == 2);
}

static void errors(void)
{
    u32 i;
    reset(); fail_call = 1; fail_after = 513;
    CHECK(ext2_read_data_blocks(&ctx, 3, 32, buffer.data) == device_error);
    CHECK(calls == 1 && !ctx.fs_error);
    canaries(513);
    reset(); fail_call = 1; fail_after = 513;
    CHECK(ext2_write_data_blocks(&ctx, 3, 32, buffer.data) == device_error);
    CHECK(calls == 1 && !ctx.fs_error);
    for (i = 0; i < 513; i++) CHECK(disk[13 * EXT2_SECTOR_SIZE + i] == 0xCD);
    CHECK(disk[13 * EXT2_SECTOR_SIZE + 513] != 0xCD);
    calls = fail_call = 0;
    CHECK(ext2_read_data_blocks(&ctx, 3, 1, buffer.data) == 0 && calls == 1);
    CHECK(buffer.data[513] == disk[13 * EXT2_SECTOR_SIZE + 513]);
    reset(); fail_call = 1;
    CHECK(ext2_read_data_block(&ctx, 3, buffer.data) == device_error && !ctx.fs_error);
    reset(); fail_call = 1;
    CHECK(ext2_read_block(&ctx, 3, buffer.data) == device_error && ctx.fs_error);
    reset(); fail_call = 2; fail_after = 1;
    CHECK(ext2_write_data_block(&ctx, 3, buffer.data) == device_error);
    CHECK(calls == 2 && !ctx.fs_error && disk[13 * EXT2_SECTOR_SIZE] == 0xCD);
    reset(); fail_call = 2;
    CHECK(ext2_write_block(&ctx, 3, buffer.data) == device_error && ctx.fs_error);
    reset(); fail_call = 1;
    CHECK(ext2_read_sector(&ctx, 3, 1, buffer.data) == device_error && !ctx.fs_error);
    reset(); fail_call = 1;
    CHECK(ext2_write_sector(&ctx, 3, 1, buffer.data) == device_error && !ctx.fs_error);
    reset(); fail_call = 1; ctx.mounted = 1;
    le16_wr(disk + (ctx.base_lba + EXT2_SUPER_LBA) * EXT2_SECTOR_SIZE + EXT2_SB_STATE_OFF, EXT2_VALID_FS);
    CHECK(ext2_read_block(&ctx, 3, buffer.data) == device_error && ctx.fs_error);
    CHECK(calls == 3 && writes == 1);
    CHECK(le16_rd(disk + (ctx.base_lba + EXT2_SUPER_LBA) * EXT2_SECTOR_SIZE + EXT2_SB_STATE_OFF) & EXT2_ERROR_FS);
}

void _start(void)
{
    ranges(); bounds(); errors();
    report("ext2_block_io: PASS\n");
    finish(0);
}
