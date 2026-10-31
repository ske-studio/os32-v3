/* D4: product VFS + unmodified FatFs, memory disk boundary and API counters. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "vfs.h"
#include "platform_device.h"
#include "dev.h"
#include "os_time.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)
#define CLUSTERS 1024
#define MAX_SECTOR 1024
#define TRACE_CAP 200000
static const char *file_path = "/ONE/TWO/THREE/FILE.BIN";
static const char *full_path = "0:/ONE/TWO/THREE/FILE.BIN";
static BYTE *media[2];
static WORD sector_size = 512;
static LBA_t sectors;
static u32 generation[2];
static int generation_calls, last_drive, advance_on_read;
static int disk_fail_after = -1, partial_seen;
static int fail_initialize, fail_sync;
static FRESULT open_error, seek_error, read_error, close_error, mount_error;
static int opens, closes, reads, seeks, mounts, close_after_detach;
static unsigned long chain_refs, disk_calls, disk_sectors, data_calls, meta_calls;
static void *get_fat_address;
struct request { LBA_t lba; UINT count; };
static struct request trace[TRACE_CAP];
static unsigned trace_len;
static BYTE data_lba[70000];

void *test_get_fat_address(void);
void __cyg_profile_func_enter(void *fn, void *caller)
{ (void)caller; if (fn == get_fat_address) chain_refs++; }
void __cyg_profile_func_exit(void *fn, void *caller) { (void)fn; (void)caller; }

FRESULT real_f_open(FIL *, const TCHAR *, BYTE);
FRESULT real_f_close(FIL *);
FRESULT real_f_read(FIL *, void *, UINT, UINT *);
FRESULT real_f_lseek(FIL *, FSIZE_t);
FRESULT real_f_mount(FATFS *, const TCHAR *, BYTE);
FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode)
{
    opens++;
    if (open_error) { FRESULT e = open_error; open_error = FR_OK; return e; }
    return real_f_open(fp, path, mode);
}
FRESULT f_close(FIL *fp)
{
    closes++;
    if (!fp->obj.fs || !fp->obj.fs->fs_type) close_after_detach++;
    FRESULT fr = real_f_close(fp);
    if (close_error) { FRESULT e = close_error; close_error = FR_OK; return e; }
    return fr;
}
FRESULT f_read(FIL *fp, void *buf, UINT size, UINT *br)
{
    reads++;
    if (read_error) { FRESULT e = read_error; read_error = FR_OK; *br = 0; return e; }
    FRESULT fr = real_f_read(fp, buf, size, br);
    if (fr != FR_OK && *br) partial_seen++;
    return fr;
}
FRESULT f_lseek(FIL *fp, FSIZE_t offset)
{
    seeks++;
    if (seek_error) { FRESULT e = seek_error; seek_error = FR_OK; return e; }
    return real_f_lseek(fp, offset);
}
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt)
{
    mounts++;
    if (fs && mount_error) {
        /* Like FatFs: registration precedes an immediate-mount failure. */
        CHECK(real_f_mount(fs, path, 0) == FR_OK);
        return mount_error;
    }
    return real_f_mount(fs, path, opt);
}

DSTATUS disk_initialize(BYTE drv) { (void)drv; return fail_initialize ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE drv) { (void)drv; return 0; }
DRESULT disk_read(BYTE drv, BYTE *buf, LBA_t lba, UINT count)
{
    CHECK(drv < 2 && lba + count <= sectors);
    disk_calls++; disk_sectors += count;
    if (trace_len < TRACE_CAP) trace[trace_len++] = (struct request){lba, count};
    if (data_lba[lba]) data_calls++; else meta_calls++;
    if (advance_on_read) { generation[1]++; advance_on_read = 0; }
    if (disk_fail_after == 0) { disk_fail_after = -1; return RES_ERROR; }
    if (disk_fail_after > 0) disk_fail_after--;
    memcpy(buf, media[drv] + (size_t)lba * sector_size, count * sector_size);
    return RES_OK;
}
DRESULT disk_write(BYTE drv, const BYTE *buf, LBA_t lba, UINT count)
{
    CHECK(drv < 2 && lba + count <= sectors);
    memcpy(media[drv] + (size_t)lba * sector_size, buf, count * sector_size);
    if (drv == 0) generation[1]++;
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drv, BYTE cmd, void *buf)
{
    (void)drv;
    switch (cmd) {
    case CTRL_SYNC: return fail_sync ? RES_ERROR : RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buf = sectors; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buf = sector_size; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buf = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
DWORD get_fattime(void) { return (DWORD)(2026 - 1980) << 25 | 1 << 21 | 1 << 16; }
u32 fdc_media_gen(int drive)
{ generation_calls++; last_drive = drive; return generation[drive]; }
void diskio_set_fdd_drive(int drv) { CHECK(drv == 1); }
void diskio_set_hdd_drive(int drv) { (void)drv; }
void diskio_set_hdd_partition(u32 offset) { (void)offset; }
void diskio_set_hdd_sector_size(u16 size) { (void)size; }
void diskio_set_hdd_ide_phys_size(u16 size) { (void)size; }
int platform_disk_geometry_get(int drv, struct platform_disk_geometry *info)
{ (void)drv; (void)info; return -1; }
Device *dev_find(const char *name) { (void)name; return NULL; }
int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf) { return -1; }
int bootinfo_part_geom(int drive, u16 *heads, u16 *spt) { *heads = 8; *spt = 17; return 2; }
os_time_t dos_time_to_epoch(u16 date, u16 time) { return (os_time_t)date * 65536 + time; }
int vfs_register_fs(VfsOps *ops) { return VFS_OK; }
void *kzalloc(u32 size) { return calloc(1, size); }
void kfree(void *p) { free(p); }
void kprintf(u8 attr, const char *fmt, ...) { }
void *kmemset(void *dst, int value, u32 n) { return memset(dst, value, n); }
u32 kstrlen(const char *s) { return strlen(s); }
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
char *kstrncpy(char *dst, const char *src, u32 n)
{
    if (n) { size_t len = strlen(src); if (len >= n) len = n - 1;
        memcpy(dst, src, len); dst[len] = 0; }
    return dst;
}
char *kstrncat(char *dst, const char *src, u32 n)
{ u32 len = strlen(dst); if (len < n) kstrncpy(dst + len, src, n - len); return dst; }
#include "../../drivers/block/pc98pt.c"
#include "../../fs/fat/fatfs_vfs.c"

static void reset_counts(void)
{
    opens = closes = reads = seeks = mounts = 0;
    generation_calls = 0;
    chain_refs = disk_calls = disk_sectors = data_calls = meta_calls = 0;
    trace_len = 0;
}
static void pattern(BYTE *buf, unsigned index)
{ for (unsigned j = 0; j < sector_size; j++) buf[j] = (BYTE)(index * 17 + j * 13); }
static void create_file(const char *path, BYTE value, UINT size)
{
    FIL fil; UINT bw; BYTE buf[MAX_SECTOR];
    memset(buf, value, sizeof(buf));
    CHECK(f_open(&fil, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    while (size) { UINT n = size > sector_size ? sector_size : size;
        CHECK(f_write(&fil, buf, n, &bw) == FR_OK && bw == n); size -= n; }
    CHECK(f_close(&fil) == FR_OK);
}
static FatFsCtx *prepare(int type, int ss, int fragmented)
{
    MKFS_PARM opt = {0}; BYTE work[4096], buf[MAX_SECTOR];
    FIL a, b; UINT bw;
    sector_size = ss;
    sectors = type == FS_FAT12 ? 3500 : type == FS_FAT16 ? 12000 : 70000;
    media[0] = calloc(sectors, ss); CHECK(media[0]);
    memset(data_lba, 0, sizeof(data_lba));
    opt.fmt = (type == FS_FAT32 ? FM_FAT32 : FM_FAT) | FM_SFD;
    opt.n_fat = 1; opt.align = 1; opt.n_root = 512; opt.au_size = ss;
    CHECK(f_mkfs("0:", &opt, work, sizeof(work)) == FR_OK);
    FatFsCtx *fc = fatfs_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_FD, 1));
    CHECK(fc && fc->fatfs.fs_type == type && fc->fatfs.csize == 1);
    CHECK(f_mkdir("0:/ONE") == FR_OK);
    CHECK(f_mkdir("0:/ONE/TWO") == FR_OK);
    CHECK(f_mkdir("0:/ONE/TWO/THREE") == FR_OK);
    CHECK(f_open(&a, full_path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    if (fragmented) CHECK(f_open(&b, "0:/GAPS.BIN", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    for (unsigned i = 0; i < CLUSTERS; i++) {
        pattern(buf, i);
        CHECK(f_write(&a, buf, ss, &bw) == FR_OK && bw == (UINT)ss);
        data_lba[fc->fatfs.database + (a.clust - 2)] = 1;
        if (fragmented) CHECK(f_write(&b, buf, ss, &bw) == FR_OK && bw == (UINT)ss);
    }
    CHECK(f_close(&a) == FR_OK);
    if (fragmented) CHECK(f_close(&b) == FR_OK);
    create_file("0:/EMPTY", 0, 0);
    for (int i = 0; i < 5; i++) { char path[] = "0:/ALT0"; path[6] += i;
        create_file(path, 'A' + i, ss * 3); }
    /* A fresh shared window for both reference and retained passes. */
    CHECK(f_mount(NULL, "0:", 0) == FR_OK);
    CHECK(f_mount(&fc->fatfs, "0:", 1) == FR_OK);
    return fc;
}
static void finish(FatFsCtx *fc)
{
    fatfs_vfs_umount(fc);
    CHECK(close_after_detach == 0);
    free(media[0]); media[0] = NULL;
    free(media[1]); media[1] = NULL;
}
/* Original contract: every request opens, explicitly seeks for stream, reads, closes. */
static int reference(const char *path, void *buf, UINT size, FSIZE_t offset)
{
    FIL fil; UINT br; FRESULT fr;
    fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK) return ff_to_vfs(fr);
    fr = f_lseek(&fil, offset);
    if (fr == FR_OK) fr = f_read(&fil, buf, size, &br);
    f_close(&fil);
    return fr == FR_OK ? (int)br : ff_to_vfs(fr);
}
static void performance(int type, int ss, int fragmented)
{
    FatFsCtx *fc = prepare(type, ss, fragmented);
    BYTE *expected = malloc(CLUSTERS * ss), buf[MAX_SECTOR]; CHECK(expected);
    reset_counts();
    for (unsigned i = 0; i < CLUSTERS; i++)
        CHECK(reference(full_path, expected + i * ss, ss, i * ss) == ss);
    CHECK(opens == CLUSTERS && seeks == CLUSTERS && reads == CLUSTERS && closes == CLUSTERS);
    CHECK(chain_refs == CLUSTERS * (CLUSTERS - 1UL) / 2);
    unsigned long old_calls = disk_calls, old_meta = meta_calls, old_sectors = disk_sectors;
    CHECK(data_calls == CLUSTERS && trace_len == disk_calls);
    CHECK(f_mount(NULL, "0:", 0) == FR_OK);
    CHECK(f_mount(&fc->fatfs, "0:", 1) == FR_OK);
    reset_counts();
    for (unsigned i = 0; i < CLUSTERS; i++) {
        CHECK(fatfs_vfs_read_stream(fc, file_path, buf, ss, i * ss) == ss);
        CHECK(memcmp(expected + i * ss, buf, ss) == 0);
    }
    printf("MEASURE FAT%d ss=%d fragmented=%d open=%d seek=%d read=%d chain=%lu "
           "disk_read=%lu->%lu sectors=%lu->%lu metadata=%lu->%lu data=%lu->%lu\n",
           type == FS_FAT12 ? 12 : type == FS_FAT16 ? 16 : 32, ss, fragmented,
           opens, seeks, reads, chain_refs, old_calls, disk_calls, old_sectors,
           disk_sectors, old_meta, meta_calls, (unsigned long)CLUSTERS, data_calls);
    fflush(stdout);
    CHECK(opens == 1 && seeks == 0 && reads == CLUSTERS && closes == 0);
    CHECK(chain_refs == CLUSTERS - 1 && data_calls == CLUSTERS);
    CHECK(meta_calls < old_meta && disk_calls < old_calls && trace_len == disk_calls);
    for (unsigned i = 0; i < trace_len; i++) CHECK(trace[i].count > 0);
    fatfs_vfs_umount(fc);
    CHECK(closes == 1 && close_after_detach == 0);
    free(expected); free(media[0]); media[0] = NULL;
}

static void compare_request(FatFsCtx *fc, const char *path, UINT size, FSIZE_t offset)
{
    BYTE a[MAX_SECTOR * 3], b[MAX_SECTOR * 3]; char full[VFS_MAX_PATH];
    CHECK(size <= sizeof(a));
    CHECK(ff_make_path(fc, path, full, sizeof(full)) == VFS_OK);
    int ref = reference(full, a, size, offset);
    int got = fatfs_vfs_read_stream(fc, path, b, size, offset);
    CHECK(ref == got);
    if (got >= 0) CHECK(memcmp(a, b, got) == 0);
}
static void positions(void)
{
    FatFsCtx *fc = prepare(FS_FAT12, 512, 1); BYTE buf[MAX_SECTOR];
    for (unsigned i = 0; i < 20; i++) compare_request(fc, file_path, 128, i * 128);
    const unsigned offsets[] = {0, 499, 513, 1020, 17, 40000, 0, CLUSTERS * 512 - 19,
                                CLUSTERS * 512, CLUSTERS * 512 + 500};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
        compare_request(fc, file_path, 1500, offsets[i]);
    compare_request(fc, file_path, 0, 100);
    compare_request(fc, "/EMPTY", 512, 0);
    compare_request(fc, "/EMPTY", 0, 100);
    compare_request(fc, file_path, 128, 0);   /* same path, FD A */
    compare_request(fc, file_path, 128, 999); /* FD B */
    compare_request(fc, file_path, 128, 128); /* FD A */
    CHECK(fatfs_vfs_read(fc, file_path, buf, 512) == 512);
    BYTE expected[MAX_SECTOR]; pattern(expected, 0);
    CHECK(memcmp(buf, expected, 512) == 0);
    finish(fc);
    puts("PASS positions, boundaries, EOF, empty, size=0, shared path");
}

static void lru_and_aliases(void)
{
    FatFsCtx *fc = prepare(FS_FAT12, 512, 0); BYTE buf[MAX_SECTOR];
    for (int i = 0; i < 4; i++) { char path[] = "/ALT0"; path[4] += i;
        CHECK(fatfs_vfs_read(fc, path, buf, 128) == 128); CHECK(buf[0] == 'A' + i); }
    reset_counts();
    for (int i = 0; i < 4; i++) { char path[] = "/ALT0"; path[4] += i;
        CHECK(fatfs_vfs_read_stream(fc, path, buf, 128, 128) == 128); }
    CHECK(opens == 0 && seeks == 0 && closes == 0);
    CHECK(fatfs_vfs_read_stream(fc, "/ALT0", buf, 128, 256) == 128);
    CHECK(fatfs_vfs_read(fc, "/ALT4", buf, 128) == 128);
    CHECK(opens == 1 && closes == 1);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 128) == 128);
    CHECK(opens == 1); /* ALT0 was refreshed, so ALT1 is the victim. */
    CHECK(fatfs_vfs_read(fc, "/ALT1", buf, 128) == 128);
    CHECK(opens == 2 && closes == 2);
    reset_counts();
    CHECK(fatfs_vfs_read(fc, "/alt1", buf, 128) == 128 && buf[0] == 'B');
    CHECK(opens == 1); /* keys compare bytes, FatFs itself folds case */
    reset_counts();
    CHECK(fatfs_vfs_write(fc, "/aLt1", "new", 3) == 3);
    CHECK(closes == 5); /* four retained reads, then the writer */
    CHECK(fatfs_vfs_read(fc, "/ALT1", buf, sizeof(buf)) == 3 && !memcmp(buf, "new", 3));
    CHECK(fatfs_vfs_read(fc, "/alt1", buf, sizeof(buf)) == 3 && !memcmp(buf, "new", 3));
    finish(fc);
    puts("PASS four-way LRU, fifth eviction, aliases and invalidation");
}

static void mutations(void)
{
    FatFsCtx *fc = prepare(FS_FAT12, 512, 0); BYTE buf[MAX_SECTOR];
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == 512);
    reset_counts();
    CHECK(fatfs_vfs_write_stream(fc, "/ALT0", "xyz", 3, 0) == 3);
    CHECK(closes == 2);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 3) == 3 && !memcmp(buf, "xyz", 3));
    CHECK(fatfs_vfs_write_stream(fc, "/ALT0", "end", 3, 2000) == 3);
    CHECK(fatfs_vfs_read_stream(fc, "/ALT0", buf, 512, 2000) == 3 && !memcmp(buf, "end", 3));
    CHECK(fatfs_vfs_write(fc, "/ALT0", "tiny", 4) == 4);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == 4 && !memcmp(buf, "tiny", 4));
    CHECK(fatfs_vfs_write(fc, "/ALT0", "", 0) == 0);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == 0);
    CHECK(fatfs_vfs_unlink(fc, "/ALT0") == VFS_OK);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == VFS_ERR_NOTFOUND);
    CHECK(fatfs_vfs_write(fc, "/ALT0", "reborn", 6) == 6);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == 6 && !memcmp(buf, "reborn", 6));
    CHECK(fatfs_vfs_read(fc, file_path, buf, 128) == 128);
    CHECK(fatfs_vfs_rename(fc, "/ONE", "/RENAMED") == VFS_OK);
    CHECK(fatfs_vfs_read(fc, file_path, buf, 128) == VFS_ERR_NOTFOUND);
    CHECK(fatfs_vfs_read(fc, "/RENAMED/TWO/THREE/FILE.BIN", buf, 128) == 128);
    reset_counts();
    CHECK(fatfs_vfs_mkdir(fc, "/NEW") == VFS_OK);
    CHECK(closes == 1);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == 6);
    reset_counts();
    CHECK(fatfs_vfs_rmdir(fc, "/NEW") == VFS_OK && closes == 1);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == 6);
    reset_counts();
    CHECK(fatfs_vfs_rmdir(fc, "/ALT0") == VFS_ERR_NOTDIR && closes == 0);
    CHECK(fatfs_vfs_write(fc, "/BAD NAME", "x", 1) == VFS_ERR_INVAL && closes == 0);
    CHECK(fatfs_vfs_rename(fc, "/ALT0", "/BAD NAME") == VFS_ERR_INVAL && closes == 0);
    CHECK(fatfs_vfs_unlink(fc, "/MISSING") == VFS_ERR_NOTFOUND && closes == 1);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == 6);
    reset_counts(); open_error = FR_DENIED;
    CHECK(fatfs_vfs_write(fc, "/ALT0", "lost", 4) == VFS_ERR_IO && closes == 1);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == 6 && opens == 2);
    reset_counts();
    CHECK(fatfs_vfs_rename(fc, "/MISSING", "/OTHER") == VFS_ERR_NOTFOUND && closes == 1);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 512) == 6);
    reset_counts();
    CHECK(fatfs_vfs_mkdir(fc, "/ONE/MISSING/NEW") == VFS_ERR_NOTFOUND && closes == 1);
    finish(fc);
    puts("PASS overwrite, extend, truncate, recreate, ancestor rename, mkdir/rmdir, failed mutation");
}

static void lru_error_gaps(void)
{
    FatFsCtx *fc = prepare(FS_FAT12, 512, 0); BYTE buf[MAX_SECTOR];
    for (int i = 0; i < 4; i++) {
        char path[] = "/ALT0"; path[4] += i;
        CHECK(fatfs_vfs_read(fc, path, buf, 128) == 128);
    }
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 128) == 128);
    CHECK(fatfs_vfs_read(fc, "/ALT1", buf, 128) == 128);
    for (int i = 0; i < 10; i++) {
        read_error = FR_DENIED;
        CHECK(fatfs_vfs_read(fc, "/ALT1", buf, 128) == VFS_ERR_IO);
        CHECK(fatfs_vfs_read(fc, "/ALT1", buf, 128) == 128);
    }
    reset_counts();
    CHECK(fatfs_vfs_read(fc, "/ALT4", buf, 128) == 128 && opens == 1);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 128) == 128 && opens == 1);
    CHECK(fatfs_vfs_read(fc, "/ALT2", buf, 128) == 128 && opens == 2);
    finish(fc);
    puts("PASS LRU order after repeated nonfatal read failures");
}

static void errors(void)
{
    FatFsCtx *fc = prepare(FS_FAT12, 512, 1); BYTE buf[MAX_SECTOR * 3];
    const FRESULT critical[] = { FR_DISK_ERR, FR_NOT_READY, FR_INVALID_OBJECT, FR_INT_ERR };
    for (unsigned i = 0; i < sizeof(critical) / sizeof(critical[0]); i++) {
        CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 128) == 128);
        CHECK(fatfs_vfs_read(fc, "/ALT1", buf, 128) == 128);
        reset_counts(); open_error = critical[i];
        CHECK(fatfs_vfs_read(fc, "/MISSING", buf, 128) == VFS_ERR_IO);
        CHECK(closes == 2 && mounts == 0);
        CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 128) == 128 && opens == 2);
        CHECK(fatfs_vfs_read(fc, "/ALT1", buf, 128) == 128);
        reset_counts(); seek_error = critical[i];
        CHECK(fatfs_vfs_read_stream(fc, "/ALT0", buf, 128, 10) == VFS_ERR_IO);
        CHECK(closes == 2 && mounts == 0);
        CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 128) == 128 && opens == 1);
        CHECK(fatfs_vfs_read(fc, "/ALT1", buf, 128) == 128);
        reset_counts(); read_error = critical[i];
        CHECK(fatfs_vfs_read_stream(fc, "/ALT0", buf, 128, 128) == VFS_ERR_IO);
        CHECK(closes == 2 && mounts == 0);
    }
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 128) == 128);
    CHECK(fatfs_vfs_read(fc, "/ALT1", buf, 128) == 128);
    reset_counts(); read_error = FR_DENIED;
    CHECK(fatfs_vfs_read_stream(fc, "/ALT0", buf, 128, 128) == VFS_ERR_IO && closes == 1);
    CHECK(fatfs_vfs_read_stream(fc, "/ALT1", buf, 128, 128) == 128 && opens == 0);
    /* A real partial read: first data block succeeds, the next data I/O fails. */
    CHECK(fatfs_vfs_read(fc, file_path, buf, 512) == 512);
    disk_fail_after = 2;
    CHECK(fatfs_vfs_read(fc, file_path, buf, 1536) == VFS_ERR_IO);
    CHECK(partial_seen == 1);
    reset_counts();
    CHECK(fatfs_vfs_read(fc, file_path, buf, 512) == 512 && opens == 1);
    close_error = FR_DISK_ERR;
    CHECK(fatfs_vfs_mkdir(fc, "/CLOSE") == VFS_OK);
    reset_counts();
    CHECK(fatfs_vfs_read(fc, file_path, buf, 512) == 512 && opens == 1);
    fail_sync = 1;
    CHECK(fatfs_vfs_write(fc, "/ALT0", "error", 5) == VFS_ERR_IO);
    fail_sync = 0;
    CHECK(mounts == 2 && close_after_detach == 0);
    reset_counts();
    CHECK(fatfs_vfs_read(fc, file_path, buf, 512) == 512 && opens == 1);
    fail_sync = 1; mount_error = FR_NOT_READY;
    CHECK(fatfs_vfs_write(fc, "/ALT0", "error", 5) == VFS_ERR_IO);
    fail_sync = 0; mount_error = FR_OK;
    reset_counts();
    CHECK(fatfs_vfs_read(fc, file_path, buf, 512) == 512 && opens == 1);
    finish(fc);
    puts("PASS open/seek/read faults, partial read, close errors and write recovery");
}

static void generations_and_lifetime(void)
{
    FatFsCtx *fc = prepare(FS_FAT12, 512, 0); BYTE buf[MAX_SECTOR];
    reset_counts();
    CHECK(fatfs_vfs_read(fc, file_path, buf, 512) == 512);
    CHECK(generation_calls == 1 && last_drive == 1);
    generation[1]++;
    CHECK(fatfs_vfs_read_stream(fc, file_path, buf, 512, 512) == 512);
    CHECK(opens == 2 && closes == 1 && mounts == 0);
    advance_on_read = 1;
    CHECK(fatfs_vfs_read_stream(fc, file_path, buf, 512, 1024) == 512);
    CHECK(opens == 2 && mounts == 0);
    CHECK(fatfs_vfs_read_stream(fc, file_path, buf, 512, 1536) == 512);
    CHECK(opens == 3 && closes == 2 && mounts == 0);
    CHECK(fatfs_vfs_read(fc, "/ALT0", buf, 128) == 128);
    CHECK(fatfs_vfs_write(fc, "/COPY", buf, 128) == 128);
    reset_counts();
    CHECK(fatfs_vfs_read(fc, "/COPY", buf, 128) == 128 && buf[0] == 'A' && opens == 1);
    media[1] = malloc((size_t)sectors * sector_size); CHECK(media[1]);
    memcpy(media[1], media[0], (size_t)sectors * sector_size);
    FatFsCtx *hd = fatfs_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_HD, 0)); CHECK(hd);
    reset_counts();
    CHECK(fatfs_vfs_read(hd, "/ALT0", buf, 128) == 128);
    CHECK(generation_calls == 0);
    for (int i = 0; i < 100; i++) {
        char path[] = "/ALT0"; path[4] += i % 5;
        CHECK(fatfs_vfs_read(hd, path, buf, 128) == 128 && buf[0] == 'A' + i % 5);
        CHECK(opens - closes <= 4);
    }
    CHECK(fatfs_vfs_write(hd, "/COPY", "hd", 2) == 2);
    reset_counts();
    CHECK(fatfs_vfs_read(fc, "/COPY", buf, 128) == 128 && buf[0] == 'A' && opens == 0);
    CHECK(fatfs_vfs_read(hd, "/COPY", buf, 128) == 2 && !memcmp(buf, "hd", 2));
    CHECK(generation_calls == 1);
    fatfs_vfs_umount(hd);
    fatfs_vfs_umount(fc); CHECK(close_after_detach == 0);
    fc = fatfs_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_FD, 1)); CHECK(fc);
    reset_counts();
    CHECK(fatfs_vfs_read(fc, "/COPY", buf, 128) == 128 && opens == 1);
    fatfs_vfs_umount(fc);
    fail_initialize = 1;
    CHECK(fatfs_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_FD, 1)) == NULL);
    fail_initialize = 0;
    fc = fatfs_vfs_mount(VFS_MOUNT_DEV_ENCODE(VFS_DEV_FD, 1)); CHECK(fc);
    reset_counts();
    CHECK(fatfs_vfs_read(fc, "/COPY", buf, 128) == 128 && opens == 1);
    finish(fc);
    puts("PASS physical FD generation, mid-read generation, two mounts, bounded HDD lifetime, remount");
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    get_fat_address = test_get_fat_address();
    if (!strcmp(which, "all") || !strcmp(which, "performance"))
        for (int type = FS_FAT12; type <= FS_FAT32; type++)
            for (int ss = 512; ss <= 1024; ss *= 2)
                for (int fragmented = 0; fragmented < 2; fragmented++)
                    performance(type, ss, fragmented);
    if (!strcmp(which, "all") || !strcmp(which, "positions")) positions();
    if (!strcmp(which, "all") || !strcmp(which, "lru")) lru_and_aliases();
    if (!strcmp(which, "all") || !strcmp(which, "lru")) lru_error_gaps();
    if (!strcmp(which, "all") || !strcmp(which, "mutations")) mutations();
    if (!strcmp(which, "all") || !strcmp(which, "errors")) errors();
    if (!strcmp(which, "all") || !strcmp(which, "generations")) generations_and_lifetime();
    puts("PASS fatfs_read_cache");
    return 0;
}
