/* Real loop_dev.c on ILP32; only the external VFS/device/log boundaries are stubbed.
 * A small resident prefix plus patterned sparse bytes models the 2 GiB NHD. */
#include "loop_dev.c"
#include "cmd_mnt.c"

KernelAPI *g_api;
int strcmp(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
int strncmp(const char *a, const char *b, unsigned long n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (u8)*a - (u8)*b : 0;
}
int atoi(const char *s)
{ int n = 0; while (*s >= '0' && *s <= '9') n = n * 10 + *s++ - '0'; return n; }

static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static int failures;
#define CHECK(x) do { if (!(x)) { say("FAIL: " #x "\n"); failures++; } } while (0)
static u8 image[1500000];
static u32 file_size, position, last_read, last_write;
static int reads, writes, read_limit, fail_read_at;
static u32 fail_position;

static void fill(void *p, u8 value, u32 n)
{ u8 *b = p; while (n--) *b++ = value; }
static int equal(const void *a, const void *b, u32 n)
{
    const u8 *p = a, *q = b;
    while (n--) if (*p++ != *q++) return 0;
    return 1;
}
static u8 pattern(u32 pos) { return (u8)((pos / 512) * 37 + pos % 251); }
static void reset(u32 size)
{
    loop_dev_init();
    file_size = size; position = 0;
    reads = writes = 0; read_limit = -2; fail_read_at = 0;
    for (u32 i = 0; i < sizeof(image); i++) image[i] = pattern(i);
}
int dev_register(Device *dev) { (void)dev; return 0; }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
int vfs_open(const char *path, int mode) { (void)path; (void)mode; return 3; }
void vfs_close(int fd) { (void)fd; }
int vfs_fd_set_pinned(int fd, int on) { (void)fd; (void)on; return 0; }
u32 vfs_get_size(int fd) { (void)fd; return file_size; }
int vfs_seek(int fd, int offset, int whence)
{
    (void)fd;
    /* SEEK_SET follows fs/vfs_fd.c: a negative int leaves the position intact. */
    CHECK(whence == 0);
    if (offset < 0) return -1;
    position = (u32)offset;
    return offset;
}
int vfs_read_fd(int fd, void *buf, u32 len)
{
    (void)fd;
    reads++; last_read = position;
    if (fail_read_at && position == fail_position) return -1;
    if (read_limit == -1) return -1;
    if (position >= file_size) return 0;
    if (len > file_size - position) len = file_size - position;
    if (read_limit >= 0 && len > (u32)read_limit) len = (u32)read_limit;
    for (u32 i = 0; i < len; i++)
        ((u8 *)buf)[i] = position + i < sizeof(image) ? image[position + i] : pattern(position + i);
    position += len;
    return (int)len;
}
int vfs_write_fd(int fd, const void *buf, u32 len)
{
    (void)fd;
    writes++; last_write = position;
    if (position >= file_size) return 0;
    if (len > file_size - position) len = file_size - position;
    for (u32 i = 0; i < len && position + i < sizeof(image); i++)
        image[position + i] = ((const u8 *)buf)[i];
    position += len;
    return (int)len;
}
static void put16(u32 off, u16 v)
{ image[off] = (u8)v; image[off + 1] = (u8)(v >> 8); }
static void put32(u32 off, u32 v)
{ put16(off, (u16)v); put16(off + 2, (u16)(v >> 16)); }
static void nhd(u32 c, u16 h, u16 s, u16 bps)
{
    fill(image, 0, 512);
    for (u32 i = 0; i < sizeof(NHD_SIG); i++) image[i] = NHD_SIG[i];
    put32(NHD_OFF_HDRSIZE, 512); put32(NHD_OFF_CYLS, c);
    put16(NHD_OFF_HEADS, h); put16(NHD_OFF_SPT, s); put16(NHD_OFF_BPS, bps);
}
static void test_seek(void)
{
    u8 buf[512], before[512];
    reset(2147484160UL); nhd(8192, 16, 32, 512);
    CHECK(loop_dev_attach("disk.nhd", 0) == 0);
    CHECK(loop_dev_read_lba(0, 0, 1, buf) == 0 && position == 1024);
    for (u32 i = 0; i < sizeof(before); i++) before[i] = image[1024 + i];
    fill(buf, 0x5a, sizeof(buf));
    int old_writes = writes;
    CHECK(loop_dev_write_lba(0, 4194303, 1, buf) == -1);
    CHECK(writes == old_writes && equal(before, image + 1024, 512));
    CHECK(loop_dev_read_lba(0, 0, 1, buf) == 0);
    int old_reads = reads;
    CHECK(loop_dev_read_lba(0, 4194303, 1, buf) == -1 && reads == old_reads);
    CHECK(loop_dev_read_lba(0, 0, 1, buf) == 0);
    old_reads = reads;
    CHECK(loop_slots[0].dev.blk_read(&loop_slots[0].dev, 4194303, 1, buf) == -1);
    CHECK(reads == old_reads);
    CHECK(loop_dev_read_lba(0, 0, 1, buf) == 0);
    old_reads = reads;
    CHECK(loop_dev_read_chs(0, 8191, 15, 32, buf) == -1 && reads == old_reads);
    CHECK(loop_dev_read_lba(0, 0, 1, buf) == 0);
    old_writes = writes;
    CHECK(loop_dev_write_chs(0, 8191, 15, 32, buf) == -1 && writes == old_writes);
    CHECK(loop_dev_read_lba(0, 4194302, 1, buf) == 0 && last_read == 0x7ffffe00UL);
    CHECK(loop_dev_write_lba(0, 4194302, 1, buf) == 0 && last_write == 0x7ffffe00UL);
    CHECK(loop_dev_read_chs(0, 8191, 15, 31, buf) == 0 && last_read == 0x7ffffe00UL);
    CHECK(loop_dev_write_chs(0, 8191, 15, 31, buf) == 0 && last_write == 0x7ffffe00UL);
    say("DONE loop seek\n");
}
static void test_raw_reads(void)
{
    u8 buf[512];
    reset(655360);
    CHECK(loop_dev_attach("disk.img", 0) == 0);
    const int limits[] = {-1, 0, 511, 512};
    for (u32 i = 0; i < 4; i++) {
        read_limit = limits[i]; fill(buf, 0xcc, sizeof(buf));
        int rc = loop_slots[0].dev.blk_read(&loop_slots[0].dev, 0, 1, buf);
        CHECK(rc == (limits[i] == 512 ? 0 : -1));
        if (limits[i] == 512) CHECK(equal(buf, image, 512));
    }
    say("DONE loop short read\n");
}
static u8 dd_buffer[512], dd_output[512];
static int dd_writes, dd_closed, dd_freed;
static void *dd_alloc(u32 n) { CHECK(n == 512); return dd_buffer; }
static void dd_free(void *p) { CHECK(p == dd_buffer); dd_freed++; }
static int dd_open(const char *path, int mode)
{ CHECK(strcmp(path, "out") == 0 && mode == 0x0301); return 4; }
static void dd_close(int fd) { CHECK(fd == 4); dd_closed++; }
static int dd_write(int fd, const void *buf, u32 n)
{
    CHECK(fd == 4 && n == 512); dd_writes++;
    for (u32 i = 0; i < n; i++) dd_output[i] = ((const u8 *)buf)[i];
    return (int)n;
}
static int dd_read(const char *dev, u32 lba, int count, void *buf)
{
    CHECK(strcmp(dev, "lo0") == 0);
    return loop_slots[0].dev.blk_read(&loop_slots[0].dev, (int)lba, count, buf);
}
static void test_dd(void)
{
    static KernelAPI api;
    static const u8 zeros[512];
    g_api = &api; api.kprintf = kprintf; api.mem_alloc = dd_alloc; api.mem_free = dd_free;
    api.loop_status = loop_dev_status; api.dev_blk_read = dd_read;
    api.sys_open = dd_open; api.sys_close = dd_close; api.sys_write = dd_write;
    char *args[] = {"dd", "lo0", "lba=0", "count=1", "file=out", "noerr"};
    reset(655360); CHECK(loop_dev_attach("disk.img", 0) == 0);
    const int limits[] = {-1, 0, 511, 512};
    for (u32 i = 0; i < 4; i++) {
        for (int noerr = 0; noerr <= 1; noerr++) {
            read_limit = limits[i]; dd_writes = dd_closed = dd_freed = 0;
            int expected = limits[i] == 512 || noerr ? 0 : SH_STATUS_ERROR;
            CHECK(cmd_dd(5 + noerr, args) == expected);
            CHECK(dd_closed == 1 && dd_freed == 1);
            CHECK(dd_writes == (expected == 0));
            if (dd_writes) {
                if (limits[i] == 512) CHECK(equal(dd_output, image, 512));
                else CHECK(equal(dd_output, zeros, 512));
            }
        }
    }
    say("DONE loop dd normal/noerr\n");
}
static void test_raw_geometry(void)
{
    u8 buf[512]; u16 c, b; u8 h, s; u32 total;
    reset(163840);
    CHECK(loop_dev_attach("disk.img", 0) == 0);
    CHECK(loop_dev_get_geometry(0, &c, &h, &s, &b, &total) == 0);
    CHECK(c == 40 && h == 1 && s == 8 && b == 512 && total == 320);
    CHECK((u32)c * h * s * b == file_size);
    const u16 cyls[] = {0, 0, 39}; const u8 sects[] = {1, 2, 8};
    const u32 offsets[] = {0, 512, 163328};
    for (u32 i = 0; i < 3; i++) {
        fill(buf, 0xcc, sizeof(buf));
        CHECK(loop_dev_read_chs(0, cyls[i], 0, sects[i], buf) == 0);
        CHECK(last_read == offsets[i] && equal(buf, image + offsets[i], 512));
    }
    say("DONE loop 160KiB geometry\n");
}
static void test_nhd_bounds(void)
{
    reset(4261413375UL + 512); nhd(65535, 255, 255, 1);
    CHECK(loop_dev_attach_fd(3, 0, LOOP_FMT_NHD) == 0);
    CHECK(loop_slots[0].total_lba == 4261413375UL);
    loop_dev_detach(0); put16(NHD_OFF_BPS, 2);
    CHECK(loop_dev_attach_fd(3, 0, LOOP_FMT_NHD) == LOOP_ERR_FORMAT);
    nhd(65536, 1, 1, 512);
    CHECK(loop_dev_attach_fd(3, 0, LOOP_FMT_NHD) == LOOP_ERR_FORMAT);
    nhd(1, 256, 1, 512);
    CHECK(loop_dev_attach_fd(3, 0, LOOP_FMT_NHD) == LOOP_ERR_FORMAT);
    nhd(1, 1, 256, 512);
    CHECK(loop_dev_attach_fd(3, 0, LOOP_FMT_NHD) == LOOP_ERR_FORMAT);
    nhd(1, 1, 1, 512); file_size = 1023;
    CHECK(loop_dev_attach_fd(3, 0, LOOP_FMT_NHD) == LOOP_ERR_FORMAT);
    file_size = 1024;
    CHECK(loop_dev_attach_fd(3, 0, LOOP_FMT_NHD) == 0);
}
static void test_chs_bounds(void)
{
    u8 buf[512];
    const int fmts[] = {LOOP_FMT_RAW, LOOP_FMT_FDI, LOOP_FMT_HDI, LOOP_FMT_NHD};
    for (u32 i = 0; i < 4; i++) {
        u32 hdr = fmts[i] == LOOP_FMT_RAW ? 0 : fmts[i] == LOOP_FMT_NHD ? 512 : 4096;
        reset(hdr + 655360);
        if (fmts[i] == LOOP_FMT_NHD) nhd(80, 2, 8, 512);
        else if (hdr) {
            put32(8, hdr); put32(12, 655360); put32(16, 512);
            put32(20, 8); put32(24, 2); put32(28, 80);
        }
        CHECK(loop_dev_attach_fd(3, 0, fmts[i]) == 0);
        CHECK(loop_dev_read_chs(0, 79, 1, 8, buf) == 0 && last_read == file_size - 512);
        CHECK(equal(buf, image + file_size - 512, 512));
        fill(buf, 0x7a, sizeof(buf));
        CHECK(loop_dev_write_chs(0, 79, 1, 8, buf) == 0 && last_write == file_size - 512);
        CHECK(equal(buf, image + file_size - 512, 512));
        loop_slots[0].file_size--;
        int old_reads = reads, old_writes = writes;
        CHECK(loop_dev_read_chs(0, 79, 1, 8, buf) == -1 && reads == old_reads);
        CHECK(loop_dev_write_chs(0, 79, 1, 8, buf) == -1 && writes == old_writes);
        CHECK(loop_dev_read_chs(0, 0, 0, 0, buf) == -1);
        CHECK(loop_dev_read_chs(0, 80, 0, 1, buf) == -1);
    }
}
static u32 d88_sector(u32 off, u8 head, u8 r, u16 len, u8 value)
{
    fill(image + off, 0, 16);
    image[off + 1] = head; image[off + 2] = r; image[off + 3] = 2;
    put16(off + 4, 3); put16(off + 14, len);
    fill(image + off + 16, value, len);
    return off + 16 + len;
}
static void compare_d88(int lba, int expected_rc)
{
    u8 block[512], chs[512];
    fill(block, 0xcc, 512); fill(chs, 0xcc, 512);
    CHECK(loop_slots[0].dev.blk_read(&loop_slots[0].dev, lba, 1, block) == expected_rc);
    CHECK(loop_dev_read_chs(0, 0, (u8)(lba / 3), (u8)(lba % 3 + 1), chs) == expected_rc);
    CHECK(equal(block, chs, 512));
}
static void test_d88(void)
{
    reset(8192); fill(image, 0, 688); image[D88_OFF_MEDIA] = D88_MEDIA_2DD;
    put32(D88_OFF_SIZE, file_size); put32(D88_OFF_TRACKS, 688);
    u32 off = d88_sector(688, 0, 1, 512, 0x11);
    u32 short_hdr = off;
    off = d88_sector(off, 0, 2, 128, 0x22);
    off = d88_sector(off, 0, 3, 768, 0x33);
    put32(D88_OFF_TRACKS + 4, off);
    off = d88_sector(off, 1, 1, 512, 0x44);
    off = d88_sector(off, 1, 2, 512, 0x55);
    d88_sector(off, 1, 3, 512, 0x66);
    CHECK(loop_dev_attach("disk.d88", 0) == 0);
    for (int i = 0; i < 6; i++) compare_d88(i, 0);
    u8 multi[4 * 512], expected[4 * 512];
    CHECK(loop_slots[0].dev.blk_read(&loop_slots[0].dev, 1, 4, multi) == 0);
    fill(expected, 0x22, 128); fill(expected + 128, 0, 384);
    fill(expected + 512, 0x33, 512); fill(expected + 1024, 0x44, 512);
    fill(expected + 1536, 0x55, 512); CHECK(equal(multi, expected, sizeof(multi)));
    u8 buf[512]; fill(buf, 0x77, sizeof(buf));
    CHECK(loop_dev_write_chs(0, 0, 0, 2, buf) == 0 && last_write == short_hdr + 16);
    CHECK(equal(image + short_hdr + 16, buf, 128));
    CHECK(loop_dev_write_chs(0, 0, 0, 3, buf) == 0);
    CHECK(image[last_write + 512] == 0x33); /* long sector tail survives */
    image[short_hdr + 2] = 9; compare_d88(1, -1); /* absent ID */
    image[short_hdr + 2] = 2;
    fail_read_at = 1; fail_position = short_hdr + 16; compare_d88(1, -1);
    fail_position = short_hdr; compare_d88(1, -1); fail_read_at = 0;
    put16(short_hdr + 14, 0); compare_d88(1, -1); /* zero data_len is skipped */
    loop_slots[0].track_offset[1] = 0; compare_d88(3, -1);
    CHECK(loop_slots[0].dev.blk_read(&loop_slots[0].dev, -1, 1, buf) == -1);
    CHECK(loop_slots[0].dev.blk_read(&loop_slots[0].dev, 0, 0, buf) == -1);
    CHECK(loop_slots[0].dev.blk_read(&loop_slots[0].dev, 479, 2, multi) == -1);
    say("DONE loop D88 and format boundaries\n");
}
void _start(void)
{
    test_seek(); test_raw_reads(); test_raw_geometry(); test_dd();
    test_nhd_bounds(); test_chs_bounds(); test_d88();
    if (!failures) say("PASS loop real source\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failures ? 1 : 0) : "memory");
    for (;;) {}
}
