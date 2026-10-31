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
    void *result = dst;
    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return result;
}

void *kmemset(void *dst, int val, u32 n)
{
    void *result = dst;
    __asm__ volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"(val) : "memory");
    return result;
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
static int writes_allowed(const void *buf);
static void observe_workspace(void);
static int fault_at = -1, fault_cut, persistent, fault_fired;
static int stopped_valid;
static u8 stopped_disk[DISK_SECTORS * 512u];

static int transfer(Device *dev, u32 lba, int count, void *buf, int write)
{
    u32 e = event_count++;
    if (event_count == EVENTS_MAX) event_count = 0;
    int i;
    u8 *disk = dev == &second_dev ? second_disk : g_disk;
    CHECK(e < EVENTS_MAX && dev != (Device *)0 && count > 0);
    events[e].dev = dev; events[e].lba = lba; events[e].count = count;
    events[e].write = write; events[e].rc = 0;
    observe_workspace();
    if (write) { CHECK(cache_slots_invalid()); CHECK(writes_allowed(buf)); }
    if (!write) { g_rd_calls++; g_rd_lba = lba; g_rd_count = count; }
    for (i = 0; i <= count; i++) {
        if (((int)e == fault_at && i == fault_cut) ||
            (persistent && fault_fired)) {
            if (!fault_fired) {
                kmemcpy(stopped_disk, g_disk, sizeof(g_disk));
                stopped_valid = 1;
            }
            fault_fired = 1;
            events[e].rc = -1;
            return -1;
        }
        if (i == count) break;
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

static Ext2Ctx ctx;
#ifndef BASELINE
static Ext2Ctx saved_ctx;
#endif
static int seen_workspace;
static void observe_workspace(void)
{
#ifndef BASELINE
    if (ext2_write_work.active && ext2_write_work.ctx == &ctx) seen_workspace = 1;
#endif
}
static int writes_allowed(const void *buf)
{
    return !seen_workspace || !ctx.fs_error || buf == ext2_g_err_sect;
}
static u32 target, guard, upper, lower;
static u8 input[600u * EXT2_BLOCK_SIZE + 2], output[sizeof(input)];
static u8 neighbour[128];
static const char *dump_dir;

static u32 block_lba(u32 b) { return ctx.base_lba + b * EXT2_SECTORS_PER_BLOCK; }
static u8 *media_block(u32 b) { return g_disk + block_lba(b) * EXT2_SECTOR_SIZE; }
static u8 *raw_inode(u32 ino)
{
    u32 g = (ino - 1) / ctx.sb_info.inodes_per_group;
    u32 index = (ino - 1) % ctx.sb_info.inodes_per_group;
    return media_block(ctx.gd_table[g].inode_table) + index * ctx.sb_info.inode_size;
}
static void same(const void *a, const void *b, u32 n)
{
    const u8 *aa = a, *bb = b;
    u32 i;
    for (i = 0; i < n; i++) CHECK(aa[i] == bb[i]);
}
static void trace_reset(void) { io_reset(); event_count = 0; seen_workspace = 0; }
static void prepare(int populated)
{
    Ext2Inode inode;
    u32 i;
    fault_at = -1; fault_fired = 0; persistent = 0; stopped_valid = 0;
    fail_lba = U32_MAX;
    kmemset(&ctx, 0, sizeof(ctx));
    kmemset(g_disk, 0, sizeof(g_disk));
    CHECK(pt_fake_write(g_disk, DISK_BASE_LBA, DISK_SECTORS - DISK_BASE_LBA) == 0);
    kmemset(&g_hd0, 0, sizeof(g_hd0));
    g_hd0.name = "hd0"; g_hd0.type = DEV_BLOCK; g_hd0.bus_type = DEV_BUS_IDE;
    g_hd0.sect_size = 512; g_hd0.total_sects = DISK_SECTORS;
    g_hd0.heads = 8; g_hd0.spt = 17;
    CHECK(ext2_format(0, DISK_FS_SECTORS) == 0);
    CHECK(ext2_mount(&ctx, 0) == 0);
    for (i = 0; i < sizeof(input); i++) input[i] = (u8)(i * 7u + i / 257u + 3u);
    CHECK(ext2_create(&ctx, EXT2_ROOT_INO, "target", input, 0) == 0);
    CHECK(ext2_lookup(&ctx, "/target", &target) == 0);
    CHECK(ext2_create(&ctx, EXT2_ROOT_INO, "guard", input, 1536) == 0);
    CHECK(ext2_lookup(&ctx, "/guard", &guard) == 0);
    if (populated) {
        CHECK(ext2_write_stream(&ctx, target, input, EXT2_BLOCK_SIZE,
              (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK) * EXT2_BLOCK_SIZE) == EXT2_BLOCK_SIZE);
    }
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0);
    inode.uid = 55; inode.gid = 66; inode.generation = 123;
    CHECK(ext2_write_inode(&ctx, target, &inode) == 0);
    upper = inode.block[EXT2_DIND_BLOCK];
    lower = upper ? le32_rd(media_block(upper)) : 0;
    kmemcpy(neighbour, raw_inode(guard), sizeof(neighbour));
    /* Cold metadata; prepare never leaves a retained raw read behind. */
    ext2_read_cache_reset();
    trace_reset();
}
static void read_check(u32 offset, u32 size)
{
    Ext2Inode inode;
    kmemset(output, 0xee, sizeof(output));
    CHECK(ext2_read_stream(&ctx, target, output + 1, size, offset) == (int)size);
    same(output + 1, input, size);
    CHECK(output[0] == 0xee && output[size + 1] == 0xee);
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0);
    CHECK(inode.uid == 55 && inode.gid == 66 && inode.generation == 123);
    same(neighbour, raw_inode(guard), sizeof(neighbour));
}
static int sys3(int nr, long a, long b, long c)
{
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(nr), "b"(a), "c"(b), "d"(c) : "memory");
    return ret;
}
static void dump_image(const char *name, const u8 *disk)
{
    char path[512];
    u32 i = 0, j = 0, left = DISK_FS_SECTORS * 512u;
    const u8 *src = disk + DISK_BASE_LBA * 512u;
    int fd;
    if (!dump_dir) return;
    while (dump_dir[i]) { path[i] = dump_dir[i]; i++; CHECK(i < 400); }
    path[i++] = '/';
    while (name[j]) path[i++] = name[j++];
    path[i] = 0;
    fd = sys3(5, (long)path, 01 | 0100 | 01000, 0600);
    CHECK(fd >= 0);
    while (left) {
        int n = sys3(4, fd, (long)src, left);
        CHECK(n > 0); src += n; left -= (u32)n;
    }
    CHECK(sys3(6, fd, 0, 0) == 0);
}
static void sample(int clean)
{
    char ack;
    dump_image("final.img", g_disk);
    if (stopped_valid) dump_image("stop.img", stopped_disk);
    report("@@IMAGE "); report_u((u32)clean); report(" ");
    report_u((u32)stopped_valid); report("\n");
    if (dump_dir) CHECK(sys3(3, 0, (long)&ack, 1) == 1 && ack == '\n');
}
static u32 write_event(u32 block)
{
    u32 i, found = event_count;
    for (i = 0; i < event_count; i++) {
        if (!events[i].write || events[i].lba != block_lba(block)) continue;
        CHECK(found == event_count); /* Each block has exactly one write request. */
        found = i;
    }
    CHECK(found < event_count);
    return found;
}
static void case_counts(void)
{
    u32 i, writes = 0, free, bitmap_event, lower_event, inode_event, data_writes = 0;
    u32 offset = (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK + 1) * EXT2_BLOCK_SIZE;
    prepare(1);
#ifndef BASELINE
    CHECK(sizeof(Ext2WriteWork) <= EXT2_WRITE_WORK_MAX_BYTES);
    report("write workspace bytes="); report_u(sizeof(Ext2WriteWork)); report("\n");
#endif
    free = ctx.sb_info.free_blocks_count;
    CHECK(ext2_write_stream(&ctx, target, input, 64u * EXT2_BLOCK_SIZE, offset) == 65536);
    for (i = 0; i < event_count; i++) if (events[i].write) writes++;
    report("64KiB read_KiB="); report_u(g_rd_sect / 2);
    report(" write_KiB="); report_u(g_wr_sect / 2);
    report(" read_requests="); report_u(g_rd_calls);
    report(" write_requests="); report_u(writes); report("\n");
    CHECK(g_rd_sect == 12 && g_wr_sect == 138 && g_rd_calls == 6 && writes == 69);
    for (i = 0; i < event_count; i++) if (events[i].write)
        CHECK(events[i].count == EXT2_SECTORS_PER_BLOCK);
    CHECK(ctx.sb_info.free_blocks_count == free - 64);
    /* Every new data block precedes bitmap; bitmap precedes lower, inode, SB, GDT.
     * No unchanged upper table is written. L3 writes one count=2 request per block. */
    bitmap_event = write_event(ctx.gd_table[0].block_bitmap);
    lower_event = write_event(lower);
    inode_event = write_event(ctx.gd_table[0].inode_table +
          (target - 1) * ctx.sb_info.inode_size / EXT2_BLOCK_SIZE);
    for (i = 0; i < bitmap_event; i++) {
        if (events[i].write) data_writes++;
    }
    CHECK(data_writes == 64);
    CHECK(bitmap_event < lower_event && lower_event < inode_event);
    read_check(offset, 65536);
    sample(1);
}
#ifndef BASELINE
static void case_edges(void)
{
    static const u32 lengths[] = {0, 1, 1023, 65536, 65537, 140000, 600000};
    static const u32 positions[] = {0, 13, 12u * 1024u - 1, 268u * 1024u - 1,
                                   524u * 1024u - 1};
    u32 i, j;
    for (i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
        for (j = 0; j < sizeof(lengths) / sizeof(lengths[0]); j++) {
            Ext2Inode inode;
            prepare(0);
            CHECK(ext2_write_stream(&ctx, target, input, lengths[j], positions[i]) == (int)lengths[j]);
            CHECK(!ext2_write_work.active);
            read_check(positions[i], lengths[j]);
            CHECK(ext2_read_inode(&ctx, target, &inode) == 0);
            CHECK(inode.size == (lengths[j] ? positions[i] + lengths[j] : 0));
            sample(1);
        }
    }
    prepare(1);
    CHECK(ext2_write_stream(&ctx, target, input, 600000, 268u * 1024u) == 600000);
    CHECK(g_rd_calls == 6); /* same existing lower/upper/bitmap across ten windows */
    read_check(268u * 1024u, 600000); sample(1);
    prepare(0);
    CHECK(ext2_write(&ctx, target, input, 600000) == 0);
    read_check(0, 600000); sample(1);
    CHECK(ext2_create(&ctx, EXT2_ROOT_INO, "large", input, 600000) == 0);
    CHECK(ext2_lookup(&ctx, "/large", &target) == 0);
    CHECK(ext2_read_stream(&ctx, target, output, 600000, 0) == 600000);
    same(output, input, 600000); sample(1);
}

static void case_fragment(void)
{
    Ext2Inode inode;
    u32 i, phys, previous = 0, gaps = 0;
    prepare(0);
    CHECK(ext2_write(&ctx, guard, input, 128u * 1024u) == 0);
    CHECK(ext2_read_inode(&ctx, guard, &inode) == 0);
    for (i = 0; i < 128; i += 2) {
        CHECK(ext2_bmap(&ctx, &inode, i, &phys) == 0 && phys != 0);
        if (i < EXT2_NDIR_BLOCKS) inode.block[i] = 0;
        else le32_wr(media_block(inode.block[EXT2_IND_BLOCK]) +
                     (i - EXT2_NDIR_BLOCKS) * EXT2_BLOCK_PTR_SIZE, 0);
        /* Test setup removes references before freeing. */
        inode.blocks -= EXT2_SECTORS_PER_BLOCK;
        CHECK(ext2_write_inode(&ctx, guard, &inode) == 0);
        CHECK(ext2_free_block(&ctx, phys) == 0);
    }
    CHECK(ext2_sync(&ctx) == 0);
    kmemcpy(neighbour, raw_inode(guard), sizeof(neighbour));
    ext2_read_cache_reset(); trace_reset();
    CHECK(ext2_write_stream(&ctx, target, input, 65536, 0) == 65536);
    read_check(0, 65536);
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0);
    for (i = 0; i < 64; i++) {
        CHECK(ext2_bmap(&ctx, &inode, i, &phys) == 0);
        if (i && phys != previous + 1) gaps++;
        previous = phys;
    }
    CHECK(gaps > 32); sample(1);
}

static void case_nospace(void)
{
    u32 filler, offset = 0;
    int n;
    prepare(0);
    CHECK(ext2_create(&ctx, EXT2_ROOT_INO, "fill", input, 0) == 0);
    CHECK(ext2_lookup(&ctx, "/fill", &filler) == 0);
    while (ctx.sb_info.free_blocks_count > 5) {
        u32 blocks = ctx.sb_info.free_blocks_count - 5;
        if (blocks > 500) blocks = 500;
        n = ext2_write_stream(&ctx, filler, input, blocks * 1024u, offset);
        CHECK(n > 0); offset += (u32)n;
    }
    CHECK(ctx.sb_info.free_blocks_count >= 3);
    n = ext2_write_stream(&ctx, target, input, 65536, 268u * 1024u);
    CHECK(n > 0 && n < 65536);
    CHECK(ctx.sb_info.free_blocks_count == 0 && !ctx.fs_error);
    read_check(268u * 1024u, (u32)n); sample(1);
}

static int operation(int op)
{
    if (op == 1) return ext2_create(&ctx, EXT2_ROOT_INO, "new", input, 70000);
    if (op == 2) return ext2_write(&ctx, target, input, 70000);
    if (op == 3) return ext2_write_stream(&ctx, target, input, 4097,
                      (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK + 255) * EXT2_BLOCK_SIZE);
    if (op == 4) return ext2_write_stream(&ctx, target, input, 70000, 0);
    if (op == 5) return ext2_write_stream(&ctx, target, input, 17,
                      (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK) * EXT2_BLOCK_SIZE + 1);
    return ext2_write_stream(&ctx, target, input, 65536,
                 (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK + 1) * EXT2_BLOCK_SIZE);
}
static u32 expected_offset(int op)
{
    if (op == 3) return (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK + 255) * EXT2_BLOCK_SIZE;
    if (op == 4) return 0;
    if (op == 5) return (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK) * EXT2_BLOCK_SIZE + 1;
    return (EXT2_NDIR_BLOCKS + EXT2_ADDR_PER_BLOCK + 1) * EXT2_BLOCK_SIZE;
}
static void restore(void)
{
    kmemcpy(g_disk, second_disk, sizeof(g_disk));
    ctx = saved_ctx;
    ext2_read_cache_reset();
    fault_fired = 0; stopped_valid = 0; trace_reset();
}
static void case_faults(int op)
{
    static struct { u32 lba; int count, write; } expected[EVENTS_MAX];
    u32 count, i, j;
    int mode;
    prepare(1);
    kmemcpy(second_disk, g_disk, sizeof(g_disk)); saved_ctx = ctx;
    CHECK(operation(op) >= 0);
    count = event_count;
    for (i = 0; i < count; i++) {
        expected[i].lba = events[i].lba;
        expected[i].count = events[i].count;
        expected[i].write = events[i].write;
    }
    sample(1);
    for (i = 0; i < count; i++) {
        for (j = 0; j <= (u32)expected[i].count; j++) {
            for (mode = 0; mode < 2; mode++) {
                int result;
                restore(); fault_at = (int)i; fault_cut = (int)j; persistent = mode;
                result = operation(op);
                CHECK(fault_fired);
                CHECK(!ext2_write_work.active);
                /* Metadata faults never report a positive prefix. Data faults
                 * retain a rereadable prefix and allow the next write. */
                if (ctx.fs_error) CHECK(result == EXT2_ERR_IO);
                if (result > 0) {
                    CHECK(op != 1 && op != 2);
                    fault_at = -1; persistent = 0;
                    read_check(expected_offset(op), (u32)result);
                }
                same(neighbour, raw_inode(guard), sizeof(neighbour));
                sample(0);
                fault_at = -1; persistent = 0;
            }
        }
    }
    report("fault requests="); report_u(count); report("\n");
}

static void case_reservations(void)
{
    Ext2Inode inode;
    Ext2WriteRecord *r;
    u32 free, i, phys;
    prepare(0);
    free = ctx.sb_info.free_blocks_count;
    CHECK(ext2_write_begin(&ctx, target, 0) == 0);
    CHECK(ext2_write_reserve(&ctx, 268, &r) == 0);
    CHECK(ctx.sb_info.free_blocks_count == free && ext2_write_work.table_count == 2);
    CHECK(ext2_write_commit_window(&ctx) == 0); /* no data_done: cancel all */
    CHECK(ctx.sb_info.free_blocks_count == free && g_wr_sect == 2);
    CHECK(ext2_write_finish(&ctx) == 0);
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0 && inode.blocks == 0);
    /* NOSPC between data reservation and table reservation leaves no roots. */
    ctx.gd_table[0].free_blocks = 1; ctx.sb_info.free_blocks_count = 1;
    CHECK(ext2_write_stream(&ctx, target, input, 1024, 268u * 1024u) == 0);
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0 && inode.blocks == 0);
    CHECK(!inode.block[EXT2_DIND_BLOCK]);
    /* Exercise every retained group and the partial last-group bound with a
     * small synthetic geometry; allocation I/O still uses the product Device. */
    prepare(0);
    ctx.num_groups = EXT2_MAX_GROUPS;
    ctx.sb_info.first_data_block = 1; ctx.sb_info.blocks_per_group = 8;
    ctx.sb_info.total_blocks = 1 + 31 * 8 + 3;
    for (i = 0; i < EXT2_MAX_GROUPS; i++) {
        ctx.gd_table[i].block_bitmap = 500 + i;
        ctx.gd_table[i].free_blocks = 1;
        kmemset(media_block(500 + i), 0xff, 1024);
        media_block(500 + i)[0] = i == 31 ? 0xfbu : 0x7fu;
    }
    CHECK(ext2_write_begin(&ctx, target, 0) == 0);
    for (i = 0; i < EXT2_MAX_GROUPS; i++) {
        CHECK(ext2_write_bitmap_reserve(&ctx, &phys) == 0);
        CHECK(phys == 1 + i * 8 + (i == 31 ? 2 : 7));
    }
    CHECK(ext2_write_bitmap_reserve(&ctx, &phys) == EXT2_ERR_NOSPC);
    CHECK(g_rd_calls == 33); /* raw inode once, 32 distinct bitmap reads */
    ext2_write_abort(&ctx);
}
#endif
void start_c(long *sp);
__asm__(".text\n.globl _start\n_start:\n"
        "movl %esp, %eax\nandl $-16, %esp\npushl %eax\ncall start_c\nhlt\n");
void start_c(long *sp)
{
    const char *name = ((char **)&sp[1])[1];
    CHECK(sp[0] >= 2);
    if (sp[0] >= 3) dump_dir = ((char **)&sp[1])[2];
    if (!kstrcmp(name, "counts")) case_counts();
#ifndef BASELINE
    else if (!kstrcmp(name, "edges")) case_edges();
    else if (!kstrcmp(name, "reservations")) case_reservations();
    else if (!kstrcmp(name, "fragment")) case_fragment();
    else if (!kstrcmp(name, "nospace")) case_nospace();
    else if (name[0] == 'f' && name[1] >= '0' && name[1] <= '5') case_faults(name[1] - '0');
#endif
    else CHECK(0);
    report("PASS\n"); die(0);
}
