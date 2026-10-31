/* D1: product ext2 on disposable ILP32 RAM media; only kernel/device boundaries are replaced. */
#include "ext2_priv.h"
#include "ide_contract.h"
#include "platform_device.h"
#include "kmalloc.h"
#include "hdd_pt_fake.h"   /* LBA 1 に OS32 区画 (票 TASK_HDD_INSTALL 段 1、1088 の廃止) */

/* ======================================================================== */
/*  libc の代わり (-nostdlib)                                               */
/* ======================================================================== */

static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) { }
}

static u32 h_strlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static void report(const char *text)
{
    u32 len = h_strlen(text);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static void report_u(u32 v)
{
    char buf[16];
    int i = 15;
    buf[i] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    while (v > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    report(&buf[i]);
}

static int g_failures;

static void fail(const char *what, int line)
{
    report("FAIL line ");
    report_u((u32)line);
    report(": ");
    report(what);
    report("\n");
    g_failures++;
    die(1);
}

#define CHECK(x) do { if (!(x)) fail(#x, __LINE__); } while (0)

/* ======================================================================== */
/*  kstring / kprintf / kmalloc の境界                                      */
/* ======================================================================== */

void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst; const u8 *s = (const u8 *)src; u32 i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *kmemset(void *dst, int val, u32 n)
{
    u8 *d = (u8 *)dst; u32 i;
    for (i = 0; i < n; i++) d[i] = (u8)val;
    return dst;
}

u32 kstrlen(const char *s) { return h_strlen(s); }

int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

int kstrncmp(const char *a, const char *b, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i = 0;
    if (n == 0) return dst;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}

/* gcc が構造体コピー等で呼ぶことがある */
void *memcpy(void *dst, const void *src, u32 n) { return kmemcpy(dst, src, n); }
void *memset(void *dst, int val, u32 n) { return kmemset(dst, val, n); }
u32 strlen(const char *s) { return h_strlen(s); }
int strcmp(const char *a, const char *b) { return kstrcmp(a, b); }

void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

/* ---- kzalloc / kfree: 固定スロットの贋物 (Ext2Ctx 専用) ---- */
#define HEAP_SLOTS  8
static struct { int in_use; Ext2Ctx ctx; } g_slots[HEAP_SLOTS];

void *kzalloc(u32 size)
{
    int i;
    if (size > sizeof(Ext2Ctx)) return (void *)0;
    for (i = 0; i < HEAP_SLOTS; i++) {
        if (!g_slots[i].in_use) {
            g_slots[i].in_use = 1;
            kmemset(&g_slots[i].ctx, 0, sizeof(Ext2Ctx));
            return (void *)&g_slots[i].ctx;
        }
    }
    return (void *)0;
}

void kfree(void *p)
{
    int i;
    for (i = 0; i < HEAP_SLOTS; i++) {
        if (p == (void *)&g_slots[i].ctx) { g_slots[i].in_use = 0; return; }
    }
}

/* ---- VFS 登録の境界 (ext2_init から呼ばれるだけ) ---- */
int vfs_dir_needs_metadata(vfs_dir_cb cb, void *ctx) { (void)cb; (void)ctx; return 1; }
int vfs_register_fs(VfsOps *ops) { (void)ops; return VFS_OK; }

/* ======================================================================== */
/*  RAM ディスク + セクタ I/O 計数                                          */
/* ======================================================================== */

/* 8MB の ext2。base_lba は LBA 1 の区画表 (hdd_pt_fake.h が標準配置で書く) の
 * 開始 = 1088。区画はシリンダ単位なのでディスクは FS をシリンダへ切り上げた分。 */
#define DISK_FS_SECTORS   16384u
#define DISK_BASE_LBA     1088u
#define DISK_SECTORS      (DISK_BASE_LBA + PT_FAKE_ROUNDUP(DISK_FS_SECTORS))

static u8 g_disk[DISK_SECTORS * 512u];
static u8 second_disk[DISK_SECTORS * 512u];
static Device second_dev;
static u32 g_rd_sect;     /* 512B セクタ読み出し回数 */
static u32 g_wr_sect;     /* 512B セクタ書き込み回数 */
static u32 g_rd_calls, g_rd_lba;
static int g_rd_count;

static void io_reset(void) { g_rd_sect = 0; g_wr_sect = 0; g_rd_calls = 0; }

static Device g_hd0;

Device *dev_find(const char *name)
{
    if (name && name[0] == 'h' && name[1] == 'd' && name[2] == '0' && !name[3])
        return &g_hd0;
    return (Device *)0;
}

#define EVENTS_MAX 4096
static struct { Device *dev; u32 lba; int count, write, rc; } events[EVENTS_MAX];
static u32 event_count;
static u32 fail_lba = U32_MAX, fail_hits;
static int fail_write;
static int cache_slots_invalid(void);

static int transfer(Device *dev, u32 lba, int count, void *buf, int write)
{
    u32 e = event_count++;
    if (event_count == EVENTS_MAX) event_count = 0;
    int i;
    u8 *disk = dev == &second_dev ? second_disk : g_disk;
    CHECK(e < EVENTS_MAX && dev != (Device *)0 && count > 0);
    events[e].dev = dev; events[e].lba = lba; events[e].count = count;
    events[e].write = write; events[e].rc = 0;
    if (write) CHECK(cache_slots_invalid());
    if (!write) { g_rd_calls++; g_rd_lba = lba; g_rd_count = count; }
    for (i = 0; i < count; i++) {
        u32 cur = lba + (u32)i;
        if (cur >= DISK_SECTORS || (cur == fail_lba && write == fail_write)) {
            if (cur == fail_lba) fail_hits++;
            events[e].rc = -1;
            return -1;
        }
        if (write) {
            kmemcpy(disk + cur * 512u, (u8 *)buf + i * 512, 512);
            g_wr_sect++;
        } else {
            kmemcpy((u8 *)buf + i * 512, disk + cur * 512u, 512);
            g_rd_sect++;
        }
    }
    return 0;
}
int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf)
{ return transfer(dev, lba, count, buf, 0); }
int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *buf)
{ return transfer(dev, lba, count, (void *)buf, 1); }

/* ---- IDE の境界 (ext2_super.c / ext2_fmt.c が存在確認とジオメトリに使う) */
int platform_ide_drive_present(int drive) { return (drive & 3) == 0; }

int platform_disk_geometry_get(int drive, struct platform_disk_geometry *info)
{
    if ((drive & 3) != 0) return IDE_ERR_NO_DRIVE;
    if (info) {
        kmemset(info, 0, sizeof(*info));
        info->cylinders = 1024;
        info->heads = 8;
        info->sectors = 17;
        info->phys_sector_size = 512;
        info->total_sectors = DISK_SECTORS;
    }
    return IDE_OK;
}


#include "../../fs/ext2/ext2_super.c"
#include "../../fs/ext2/ext2_inode.c"
#include "../../fs/ext2/ext2_dir.c"
#include "../../fs/ext2/ext2_file.c"
#include "../../fs/ext2/ext2_fmt.c"
#include "../../fs/ext2/ext2_vfs.c"

static int cache_slots_invalid(void)
{
    return !ext2_inode_cache.valid && !ext2_indirect_upper.valid &&
           !ext2_indirect_lower.valid && !ext2_data_cache.valid;
}

#define TEST_INO 16u
#define DATA_FIRST 1000u
#define SINGLE_TABLE 800u
#define UPPER_TABLE 801u
#define LOWER_TABLE 802u
#define FILE_BLOCKS (EXT2_NDIR_BLOCKS + 2u * EXT2_ADDR_PER_BLOCK + 1u)
static Ext2Ctx ctx;
static u8 output[FILE_BLOCKS * EXT2_BLOCK_SIZE + 2];
static u8 input[FILE_BLOCKS * EXT2_BLOCK_SIZE];

static void trace_reset(void) { io_reset(); event_count = 0; }
static u32 block_lba(u32 block) { return ctx.base_lba + block * EXT2_SECTORS_PER_BLOCK; }
static u8 *media_block(u32 block) { return g_disk + block_lba(block) * EXT2_SECTOR_SIZE; }
static u8 pattern(u32 block, u32 off) { return (u8)(block * 13u + off * 7u + off / 256u); }
static void prepare(void)
{
    Ext2Inode inode;
    u32 b, i;
    kmemset(&ctx, 0, sizeof(ctx));
    kmemset(g_disk, 0, sizeof(g_disk));
    CHECK(pt_fake_write(g_disk, DISK_BASE_LBA, DISK_SECTORS - DISK_BASE_LBA) == 0);
    kmemset(&g_hd0, 0, sizeof(g_hd0));
    g_hd0.name = "hd0"; g_hd0.type = DEV_BLOCK; g_hd0.bus_type = DEV_BUS_IDE;
    g_hd0.sect_size = 512; g_hd0.total_sects = DISK_SECTORS;
    g_hd0.heads = 8; g_hd0.spt = 17;
    fail_lba = U32_MAX;
    CHECK(ext2_format(0, DISK_FS_SECTORS) == 0);
    CHECK(ext2_mount(&ctx, 0) == 0);
    kmemset(&inode, 0, sizeof(inode));
    inode.mode = EXT2_S_IFREG; inode.links_count = 1;
    inode.size = FILE_BLOCKS * EXT2_BLOCK_SIZE;
    for (b = 0; b < EXT2_NDIR_BLOCKS; b++) inode.block[b] = DATA_FIRST + b;
    inode.block[EXT2_IND_BLOCK] = SINGLE_TABLE;
    inode.block[EXT2_DIND_BLOCK] = UPPER_TABLE;
    for (b = 0; b < EXT2_ADDR_PER_BLOCK; b++) {
        le32_wr(media_block(SINGLE_TABLE) + b * EXT2_BLOCK_PTR_SIZE,
                DATA_FIRST + EXT2_NDIR_BLOCKS + b);
        le32_wr(media_block(LOWER_TABLE) + b * EXT2_BLOCK_PTR_SIZE,
                DATA_FIRST + EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK + b);
    }
    le32_wr(media_block(UPPER_TABLE), LOWER_TABLE);
    le32_wr(media_block(UPPER_TABLE) + EXT2_BLOCK_PTR_SIZE, LOWER_TABLE + 1);
    le32_wr(media_block(LOWER_TABLE + 1), DATA_FIRST + FILE_BLOCKS - 1);
    for (b = 0; b < FILE_BLOCKS; b++)
        for (i = 0; i < EXT2_BLOCK_SIZE; i++) {
            u8 v = pattern(DATA_FIRST + b, i);
            media_block(DATA_FIRST + b)[i] = v;
            input[b * EXT2_BLOCK_SIZE + i] = v;
        }
    CHECK(ext2_write_inode(&ctx, TEST_INO, &inode) == 0);
    ext2_unmount(&ctx);
    CHECK(ext2_mount(&ctx, 0) == 0);
    trace_reset();
}
static void bytes_check(u32 offset, u32 size)
{
    u32 i;
    CHECK(output[0] == 0xee);
    for (i = 0; i < size; i++) CHECK(output[i + 1] == input[offset + i]);
    for (i = size + 1; i < sizeof(output); i++) CHECK(output[i] == 0xee);
}
static void read_check(u32 offset, u32 size)
{
    kmemset(output, 0xee, sizeof(output));
    CHECK(ext2_read_stream(&ctx, TEST_INO, output + 1, size, offset) == (int)size);
    bytes_check(offset, size);
}
static void count_report(const char *tag)
{
    report(tag); report(" calls="); report_u(g_rd_calls);
    report(" sectors="); report_u(g_rd_sect); report("\n");
}
static void case_partial(void)
{
    u32 i;
    prepare();
    CHECK(sizeof(ext2_inode_cache) + sizeof(ext2_indirect_upper) +
          sizeof(ext2_indirect_lower) + sizeof(ext2_data_cache) == 3248);
    report("retained BSS=3248 bytes\n");
    for (i = 0; i < 8; i++) read_check(i * 128u, 128);
    count_report("partial128x8");
    CHECK(g_rd_calls == 2 && g_rd_sect == 4);
    trace_reset(); read_check(0, 108); CHECK(g_rd_calls == 0);
}
static void case_single(void)
{
    prepare();
    read_check(EXT2_NDIR_BLOCKS * EXT2_BLOCK_SIZE, EXT2_ADDR_PER_BLOCK * EXT2_BLOCK_SIZE);
    count_report("single256");
    CHECK(g_rd_calls == 10 && g_rd_sect == 516);
}
static void case_double(void)
{
    prepare();
    read_check((EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK) * EXT2_BLOCK_SIZE,
               EXT2_ADDR_PER_BLOCK * EXT2_BLOCK_SIZE);
    count_report("double256");
    CHECK(g_rd_calls == 11 && g_rd_sect == 518);
}
static void case_run33(void)
{
    prepare();
    read_check(0, 33u * EXT2_BLOCK_SIZE);
    count_report("run33");
    CHECK(g_rd_calls == 4 && g_rd_sect == 70);
    CHECK(events[2].lba == block_lba(DATA_FIRST) && events[2].count == 64);
    CHECK(events[3].lba == block_lba(DATA_FIRST + 32) && events[3].count == 2);
}

#define DOUBLE_START (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK)
static void warm_all(void)
{
    read_check(DOUBLE_START * EXT2_BLOCK_SIZE, 128);
}
static void expect_cold(void)
{
    trace_reset(); warm_all(); CHECK(g_rd_calls == 4 && g_rd_sect == 8);
}
static void case_boundaries(void)
{
    Ext2Inode inode;
    u32 phys;
    prepare();
    read_check((EXT2_NDIR_BLOCKS - 1) * EXT2_BLOCK_SIZE, 2 * EXT2_BLOCK_SIZE);
    CHECK(g_rd_calls == 3);  /* inode, single table, one contiguous run */
    ext2_read_cache_reset(); trace_reset();
    read_check((DOUBLE_START - 1) * EXT2_BLOCK_SIZE, 2 * EXT2_BLOCK_SIZE);
    CHECK(g_rd_calls == 5);  /* inode, three tables, one run */
    ext2_read_cache_reset(); trace_reset();
    read_check((DOUBLE_START + EXT2_ADDR_PER_BLOCK - 1) * EXT2_BLOCK_SIZE,
               2 * EXT2_BLOCK_SIZE);
    CHECK(g_rd_calls == 5);  /* inode, upper + two lower, one run */
    CHECK(ext2_read_inode(&ctx, TEST_INO, &inode) == 0);
    trace_reset();
    CHECK(ext2_bmap(&ctx, &inode, DOUBLE_START + EXT2_ADDR_PER_BLOCK, &phys) == 0);
    CHECK(phys == DATA_FIRST + FILE_BLOCKS - 1 && g_rd_calls == 0);
    CHECK(ext2_bmap(&ctx, &inode, DOUBLE_START, &phys) == 0);
    CHECK(g_rd_calls == 1 && events[0].lba == block_lba(LOWER_TABLE));
    CHECK(ext2_bmap(&ctx, &inode, EXT2_NDIR_BLOCKS, &phys) == 0);
    CHECK(g_rd_calls == 2 && events[1].lba == block_lba(SINGLE_TABLE));
    CHECK(ext2_bmap(&ctx, &inode, DOUBLE_START, &phys) == 0);
    CHECK(g_rd_calls == 3 && events[2].lba == block_lba(LOWER_TABLE));
}
static void case_full_blocks(void)
{
    prepare(); read_check(0, 128); trace_reset();
    read_check(0, EXT2_BLOCK_SIZE);
    CHECK(g_rd_calls == 1 && events[0].lba == block_lba(DATA_FIRST));
    read_check(EXT2_BLOCK_SIZE, EXT2_BLOCK_SIZE);
    CHECK(g_rd_calls == 2);
    trace_reset(); read_check(0, 108); CHECK(g_rd_calls == 0);
    ext2_read_cache_reset(); trace_reset();
    read_check(0, EXT2_BLOCK_SIZE); CHECK(g_rd_calls == 2);
    trace_reset(); read_check(0, 128); CHECK(g_rd_calls == 1); /* not registered */
    /* Shared scratch is not retained storage. */
    kmemset(ext2_g_blk, 0xcc, EXT2_BLOCK_SIZE);
    kmemset(ext2_g_aux, 0xcc, EXT2_BLOCK_SIZE);
    kmemset(ext2_g_dat, 0xcc, EXT2_BLOCK_SIZE);
    trace_reset(); read_check(128, 128); CHECK(g_rd_calls == 0);
}
static void case_contexts(void)
{
    Ext2Ctx other;
    Ext2Inode inode, neighbour;
    u8 b;
    prepare();
    CHECK(ext2_read_inode(&ctx, TEST_INO, &inode) == 0);
    neighbour = inode; neighbour.size = 17;
    CHECK(ext2_write_inode(&ctx, TEST_INO + 1, &neighbour) == 0);
    CHECK(ext2_read_inode(&ctx, TEST_INO, &inode) == 0);
    CHECK(inode.size == sizeof(input)); /* neighbour RMW preserved our inode */
    inode.size = 0; inode.block[0] = 0; /* caller's copy is independent */
    read_check(0, 128);
    trace_reset();
    CHECK(ext2_read_inode(&ctx, TEST_INO + 1, &neighbour) == 0);
    CHECK(neighbour.size == 17 && g_rd_calls == 1);
    read_check(0, 128); CHECK(g_rd_calls == 2); /* inode replaced, data retained */
    warm_all();
    other = ctx; other.dev = &second_dev;
    kmemcpy(second_disk, g_disk, sizeof(g_disk));
    second_disk[block_lba(DATA_FIRST + DOUBLE_START) * EXT2_SECTOR_SIZE] ^= 0xff;
    trace_reset();
    CHECK(ext2_read_stream(&other, TEST_INO, &b, 1,
                          DOUBLE_START * EXT2_BLOCK_SIZE) == 1);
    CHECK(b == (u8)(input[DOUBLE_START * EXT2_BLOCK_SIZE] ^ 0xff));
    CHECK(g_rd_calls == 4 && events[0].dev == &second_dev);
    expect_cold();
}
static void case_ranges(void)
{
    static const u32 sizes[] = { 1, 108, 128, 1023, 1024, 2049 };
    Ext2Inode inode;
    u32 i, ret;
    prepare();
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        read_check(0, sizes[i]); read_check(EXT2_BLOCK_SIZE - 17, sizes[i]);
    }
    kmemset(output, 0xee, sizeof(output));
    CHECK(ext2_read_stream(&ctx, TEST_INO, output + 1, 100, sizeof(input) - 10) == 10);
    bytes_check(sizeof(input) - 10, 10);
    CHECK(ext2_read_stream(&ctx, TEST_INO, output + 1, 100, sizeof(input)) == 0);
    trace_reset(); read_check(0, 0); CHECK(g_rd_calls == 0);
    CHECK(ext2_read_inode(&ctx, TEST_INO, &inode) == 0);
    /* Discontinuity, then a hole: two requests, never crossing either. */
    inode.block[1] = DATA_FIRST + 4; inode.block[3] = 0;
    CHECK(ext2_write_inode(&ctx, TEST_INO, &inode) == 0);
    trace_reset(); kmemset(output, 0xee, sizeof(output));
    ret = (u32)ext2_read_stream(&ctx, TEST_INO, output + 1, 5 * EXT2_BLOCK_SIZE, 0);
    CHECK(ret == 3 * EXT2_BLOCK_SIZE && g_rd_calls == 4);
    CHECK(events[1].lba == block_lba(DATA_FIRST) && events[1].count == 2);
    CHECK(events[2].lba == block_lba(DATA_FIRST + 4) && events[2].count == 2);
    CHECK(events[3].lba == block_lba(DATA_FIRST + 2) && events[3].count == 2);
    for (i = 0; i < ret; i++) {
        u32 phys = i / EXT2_BLOCK_SIZE == 1 ? DATA_FIRST + 4 : DATA_FIRST + i / EXT2_BLOCK_SIZE;
        CHECK(output[i + 1] == pattern(phys, i % EXT2_BLOCK_SIZE));
    }
    CHECK(output[0] == 0xee && output[ret + 1] == 0xee);
    /* Size=0, nonregular stream, triple-indirect unsupported. */
    inode.size = 0; CHECK(ext2_write_inode(&ctx, TEST_INO, &inode) == 0);
    CHECK(ext2_read_file(&ctx, TEST_INO, output + 1, EXT2_BLOCK_SIZE) == 0);
    inode.size = EXT2_BLOCK_SIZE; inode.mode = EXT2_S_IFDIR;
    CHECK(ext2_write_inode(&ctx, TEST_INO, &inode) == 0);
    CHECK(ext2_read_stream(&ctx, TEST_INO, output + 1, 1, 0) == EXT2_ERR_ISDIR);
    inode.mode = EXT2_S_IFREG;
    inode.size = (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK +
                  EXT2_ADDR_PER_BLOCK * EXT2_ADDR_PER_BLOCK + 1) * EXT2_BLOCK_SIZE;
    CHECK(ext2_write_inode(&ctx, TEST_INO, &inode) == 0);
    CHECK(ext2_read_stream(&ctx, TEST_INO, output + 1, 1, inode.size - EXT2_BLOCK_SIZE) == 0);
}
static void case_write_failures(void)
{
    u32 k, block = DATA_FIRST + DOUBLE_START, off;
    u8 replacement[2 * EXT2_BLOCK_SIZE];
    for (k = 0; k < 6; k++) {
        int ret;
        prepare(); warm_all();
        kmemset(replacement, 0x55, sizeof(replacement));
        fail_lba = block_lba(block) + (k == 1 || k == 3 || k == 5 ? 1u : 0u);
        fail_write = 1; fail_hits = 0;
        report("PATH write_failures k="); report_u(k); report("\n");
        if (k < 2) ret = ext2_write_data_block(&ctx, block, replacement);
        else if (k < 4) ret = ext2_write_sector(&ctx, block, k - 2, replacement);
        else ret = ext2_write_data_blocks(&ctx, block, 2, replacement);
        CHECK(ret != 0 && fail_hits == 1 && !ctx.fs_error);
        fail_lba = U32_MAX;
        /* Check the actual landed bytes, including the half-written block. */
        off = DOUBLE_START * EXT2_BLOCK_SIZE;
        kmemcpy(input + off, media_block(block), EXT2_BLOCK_SIZE);
        expect_cold();
        read_check(off + 512, 128); /* second sector too */
    }
    /* Successful writes must also invalidate before they return. */
    for (k = 0; k < 3; k++) {
        prepare(); warm_all(); kmemset(replacement, 0x66, sizeof(replacement));
        if (k == 0) CHECK(ext2_write_data_block(&ctx, block, replacement) == 0);
        if (k == 1) CHECK(ext2_write_sector(&ctx, block, 0, replacement) == 0);
        if (k == 2) CHECK(ext2_write_data_blocks(&ctx, block, 2, replacement) == 0);
        kmemcpy(input + DOUBLE_START * EXT2_BLOCK_SIZE, media_block(block), EXT2_BLOCK_SIZE);
        expect_cold();
    }
}
static void case_read_failures(void)
{
    u32 k, phys;
    Ext2Inode inode;
    for (k = 0; k < 7; k++) {
        int ret;
        u32 block = DATA_FIRST + DOUBLE_START;
        prepare(); warm_all();
        CHECK(ext2_read_inode(&ctx, TEST_INO, &inode) == 0);
        if (k == 0) {
            /* Force an inode miss, keeping other slots warm. */
            fail_lba = block_lba(ctx.gd_table[0].inode_table +
                         (TEST_INO * ctx.sb_info.inode_size) / EXT2_BLOCK_SIZE);
        } else if (k == 1) { block = UPPER_TABLE + 10; inode.block[EXT2_DIND_BLOCK] = block; }
        else if (k == 2) { block = LOWER_TABLE + 10; le32_wr(media_block(UPPER_TABLE), block);
                          /* Use a different upper block so the changed pointer is read. */
                          kmemcpy(media_block(UPPER_TABLE + 10), media_block(UPPER_TABLE), EXT2_BLOCK_SIZE);
                          inode.block[EXT2_DIND_BLOCK] = UPPER_TABLE + 10; }
        else if (k == 3) block++; /* partial miss */
        if (k != 0) fail_lba = block_lba(block) + 1; /* fail after one landed sector */
        fail_write = 0; fail_hits = 0;
        report("PATH read_failures k="); report_u(k); report("\n");
        if (k == 0) ret = ext2_read_inode(&ctx, TEST_INO + 1, &inode);
        else if (k < 3) ret = ext2_bmap(&ctx, &inode, DOUBLE_START, &phys);
        else if (k == 3) ret = ext2_read_stream(&ctx, TEST_INO, output, 128,
                                               (DOUBLE_START + 1) * EXT2_BLOCK_SIZE);
        else if (k == 4) ret = ext2_read_data_blocks(&ctx, block, 2, output);
        else if (k == 5) ret = ext2_read_data_block(&ctx, block, output);
        else ret = ext2_read_sector(&ctx, block, 1, output);
        CHECK(ret == EXT2_ERR_IO && fail_hits == 1);
        CHECK(ctx.fs_error == (k < 3 ? 1 : 0));
        fail_lba = U32_MAX;
        if (k == 2) le32_wr(media_block(UPPER_TABLE), LOWER_TABLE);
        /* Error state continues reading, but never matches or registers. */
        trace_reset(); warm_all(); CHECK(g_rd_calls == 4);
        trace_reset(); warm_all(); CHECK(g_rd_calls == (k < 3 ? 4u : 0u));
        if (k < 3) {
            ext2_unmount(&ctx); CHECK(ext2_mount(&ctx, 0) == 0);
            expect_cold(); trace_reset(); warm_all(); CHECK(g_rd_calls == 0);
        }
    }
    /* Explicit error entry invalidates even when already in error. */
    prepare(); warm_all(); ext2_fs_error(&ctx); CHECK(cache_slots_invalid());
    trace_reset(); warm_all(); CHECK(g_rd_calls == 4);
    ext2_fs_error(&ctx); trace_reset(); warm_all(); CHECK(g_rd_calls == 4);
}
static void case_lifetime(void)
{
    Ext2Ctx other;
    u32 k;
    for (k = 0; k < 2; k++) {
        prepare(); warm_all();
        if (k) { ctx.meta_dirty = 1; fail_write = 1;
                 fail_lba = ctx.base_lba + EXT2_SUPER_LBA; fail_hits = 0; }
        ext2_unmount(&ctx);
        CHECK(!ctx.mounted && cache_slots_invalid());
        if (k) CHECK(fail_hits > 0);
        fail_lba = U32_MAX;
        /* Media changes only while unmounted, reusing the exact same ctx address. */
        media_block(DATA_FIRST + DOUBLE_START)[0] ^= 0xff;
        input[DOUBLE_START * EXT2_BLOCK_SIZE] ^= 0xff;
        CHECK(ext2_mount(&ctx, 0) == 0); expect_cold();
    }
    prepare(); warm_all(); kmemset(&other, 0, sizeof(other));
    CHECK(ext2_mount(&other, 1) == EXT2_ERR_IO); expect_cold();
}
static void case_updates(void)
{
    Ext2Inode inode, neighbour;
    u32 ino, got, phys;
    u8 b, replacement[EXT2_BLOCK_SIZE + 1];
    prepare();
    /* Updating the single-indirect table must invalidate its old mapping. */
    read_check(EXT2_NDIR_BLOCKS * EXT2_BLOCK_SIZE, 128);
    CHECK(ext2_read_inode(&ctx, TEST_INO, &inode) == 0);
    CHECK(ext2_bmap_set(&ctx, &inode, EXT2_NDIR_BLOCKS, DATA_FIRST + 40) == 0);
    CHECK(ext2_bmap(&ctx, &inode, EXT2_NDIR_BLOCKS, &phys) == 0);
    CHECK(phys == DATA_FIRST + 40);
    CHECK(ext2_read_stream(&ctx, TEST_INO, &b, 1, EXT2_NDIR_BLOCKS * EXT2_BLOCK_SIZE) == 1);
    CHECK(b == pattern(DATA_FIRST + 40, 0));
    kmemset(replacement, 0x37, sizeof(replacement));
    CHECK(ext2_create(&ctx, EXT2_ROOT_INO, "live", replacement, EXT2_BLOCK_SIZE) == 0);
    CHECK(ext2_lookup(&ctx, "/live", &ino) == 0);
    CHECK(ext2_read_stream(&ctx, ino, &b, 1, 0) == 1 && b == 0x37);
    replacement[0] = 0x78;
    CHECK(ext2_write_stream(&ctx, ino, replacement, 1, 0) == 1);
    CHECK(ext2_read_stream(&ctx, ino, &b, 1, 0) == 1 && b == 0x78);
    CHECK(ext2_write_stream(&ctx, ino, replacement, 1, EXT2_BLOCK_SIZE) == 1);
    CHECK(ext2_get_size_ino(&ctx, ino, &got) == 0 && got == EXT2_BLOCK_SIZE + 1);
    CHECK(ext2_read_stream(&ctx, ino, &b, 1, EXT2_BLOCK_SIZE) == 1 && b == 0x78);
    CHECK(ext2_read_inode(&ctx, ino, &inode) == 0);
    CHECK(ext2_read_inode(&ctx, ino + 1, &neighbour) == 0);
    inode.mtime = 12345;
    CHECK(ext2_write_inode(&ctx, ino, &inode) == 0);
    CHECK(ext2_read_inode(&ctx, ino, &inode) == 0 && inode.mtime == 12345);
    { Ext2Inode n; CHECK(ext2_read_inode(&ctx, ino + 1, &n) == 0);
      CHECK(n.size == neighbour.size && n.mode == neighbour.mode); }
    CHECK(ext2_write(&ctx, ino, replacement, 1) == 0); /* truncate then write */
    CHECK(ext2_get_size_ino(&ctx, ino, &got) == 0 && got == 1);
    CHECK(ext2_read_stream(&ctx, ino, &b, 1, 0) == 1 && b == 0x78);
    CHECK(ext2_unlink(&ctx, EXT2_ROOT_INO, "live") == 0);
    replacement[0] = 0x19;
    CHECK(ext2_create(&ctx, EXT2_ROOT_INO, "reuse", replacement, 1) == 0);
    CHECK(ext2_lookup(&ctx, "/reuse", &got) == 0 && got == ino);
    CHECK(ext2_read_stream(&ctx, got, &b, 1, 0) == 1 && b == 0x19);
    CHECK(ext2_rename(&ctx, EXT2_ROOT_INO, "reuse", EXT2_ROOT_INO, "renamed") == 0);
    CHECK(ext2_lookup(&ctx, "/reuse", &got) == EXT2_ERR_NOTFOUND);
    CHECK(ext2_lookup(&ctx, "/renamed", &got) == 0 && got == ino);
    CHECK(ext2_read_stream(&ctx, got, &b, 1, 0) == 1 && b == 0x19);
}
static void run_case(const char *name)
{
    if (!kstrcmp(name, "partial")) case_partial();
    else if (!kstrcmp(name, "single")) case_single();
    else if (!kstrcmp(name, "double")) case_double();
    else if (!kstrcmp(name, "run33")) case_run33();
    else if (!kstrcmp(name, "boundaries")) case_boundaries();
    else if (!kstrcmp(name, "full")) case_full_blocks();
    else if (!kstrcmp(name, "contexts")) case_contexts();
    else if (!kstrcmp(name, "ranges")) case_ranges();
    else if (!kstrcmp(name, "write_failures")) case_write_failures();
    else if (!kstrcmp(name, "read_failures")) case_read_failures();
    else if (!kstrcmp(name, "lifetime")) case_lifetime();
    else if (!kstrcmp(name, "updates")) case_updates();
    else CHECK(0);
}
void start_c(long *sp);
__asm__(".text\n.globl _start\n_start:\n"
        "movl %esp, %eax\nandl $-16, %esp\npushl %eax\ncall start_c\nhlt\n");
void start_c(long *sp)
{
    CHECK(sp[0] == 2);
    run_case(((char **)&sp[1])[1]);
    report("PASS\n"); die(0);
}
