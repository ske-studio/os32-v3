/* Real formatter, PC-98 partition parser and loop driver run against files in
 * the fixture directory. Trace physical byte ranges including image headers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dev.h"
#include "platform_device.h"
#include "bootinfo.h"
#include "ext2.h"
#include "ext2_medium_contract.h"
#include "loop_contract.h"
#include "vfs.h"

static const char *state;
static FILE *backing;
static const char *opened;
static Device disks[4];
static u32 capacity;
static int recording;

static int mode(const char *value) { return strcmp(state, value) == 0; }
int vfs_dev_mount_count(int drv)
{
    if (drv >= OS32_DRV_LOOP(0) && drv < OS32_DRV_LOOP(4)) return 0;
    if (drv < 0 || drv > 3) abort();
    return mode("mounted");
}
void kprintf(u8 attr, const char *fmt, ...) {}
void *kmemset(void *p, int c, u32 n) { return memset(p, c, n); }
void *kmemcpy(void *p, const void *q, u32 n) { return memcpy(p, q, n); }
u32 ext2_current_time(void) { return 12345; }
void ext2_read_cache_reset(void) { puts("cache"); }

int platform_ide_drive_present(int drv) {
    printf("present %d\n", drv);
    return !mode("absent");
}
int platform_disk_geometry_get(int drv, struct platform_disk_geometry *out)
{
    printf("geometry %d\n", drv);
    out->total_sectors = capacity;
    return mode("geometry-error") ? -1 : 0;
}
int bootinfo_part_geom(int drv, u16 *heads, u16 *spt)
{
    printf("partition-geometry %d\n", drv);
    *heads = 1;
    *spt = 17;
    if (mode("no-bios")) return -1;
    return mode("identify") ? BOOTINFO_GEOM_IDENTIFY : BOOTINFO_GEOM_BIOS;
}
int platform_ide_range_ok(int drv, u32 lba, u32 count)
{
    return drv >= 0 && drv < 4 && lba < capacity && count <= capacity - lba;
}
Device *dev_find(const char *name)
{
    printf("find %s\n", name);
    for (int i = 0; i < 4; i++)
        if (!strcmp(name, disks[i].name)) return &disks[i];
    return NULL;
}

static int hd_io(Device *dev, int lba, int count, void *dst, const void *src)
{
    printf("%c %s %d %d\n", src ? 'W' : 'R', dev->name, lba, count);
    if ((src && mode("write-error")) || (!src && mode("read-error")) ||
        (!src && lba != 1 && mode("data-read-error"))) return -1;
    FILE *file = fopen(dev->name, "r+b");
    if (!file || fseek(file, (long)lba * 512, SEEK_SET)) abort();
    int done = src ? fwrite(src, 512, count, file) : fread(dst, 512, count, file);
    fclose(file);
    return done == count ? 0 : -1;
}
static int hd_read(Device *d, int l, int n, void *p) { return hd_io(d,l,n,p,NULL); }
static int hd_write(Device *d, int l, int n, const void *p) { return hd_io(d,l,n,NULL,p); }
int dev_blk_read_lba(Device *d, u32 l, int n, void *p) { return d->blk_read(d,l,n,p); }
int dev_blk_write_lba(Device *d, u32 l, int n, const void *p) { return d->blk_write(d,l,n,p); }
int dev_register(Device *dev) { return 0; }

int vfs_open(const char *path, int flags)
{
    if (flags && mode("readonly")) return -1;
    backing = fopen(path, flags ? "r+b" : "rb");
    opened = path;
    return backing ? 1 : -1;
}
void vfs_close(int fd) { fclose(backing); }
u32 vfs_get_size(int fd) { fseek(backing, 0, SEEK_END); return ftell(backing); }
int vfs_fd_set_pinned(int fd, int on) { return 0; }
int vfs_seek(int fd, int off, int whence) { return fseek(backing, off, whence); }
int vfs_read_fd(int fd, void *buf, u32 n)
{
    if (recording) printf("R %s %ld %lu\n", opened, ftell(backing), n);
    return fread(buf, 1, n, backing);
}
int vfs_write_fd(int fd, const void *buf, u32 n)
{
    printf("W %s %ld %lu\n", opened, ftell(backing), n);
    return fwrite(buf, 1, n, backing);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "registration")) {
        if (ext2_medium_register(EXT2_MEDIUM_LOOP, &ext2_medium_ide_ops) != -1 ||
            ext2_medium_register(EXT2_MEDIUM_LOOP, &ext2_medium_loop_ops) ||
            ext2_medium_register(EXT2_MEDIUM_LOOP, &ext2_medium_loop_ops) != -1 ||
            ext2_medium_register(EXT2_MEDIUM_HD, NULL) != -1 ||
            ext2_medium_register(EXT2_MEDIUM_HD, &ext2_medium_ide_ops) ||
            ext2_medium_register(EXT2_MEDIUM_LOOP + 1, &ext2_medium_loop_ops) != -1)
            return 1;
        return 0;
    }
    int drv = atoi(argv[1]), providers = atoi(argv[2]), slot = drv - 0x600;
    state = argv[3];
    capacity = strtoul(argv[4], NULL, 10);
    const char *names[] = {"hd0","hd1","hd2","hd3"};
    for (int i = 0; i < 4; i++)
        disks[i] = (Device){.name=names[i], .blk_read=hd_read, .blk_write=hd_write};
    if (providers & 1)
        if (ext2_medium_register(EXT2_MEDIUM_HD, &ext2_medium_ide_ops)) abort();
    if (providers & 2)
        if (ext2_medium_register(EXT2_MEDIUM_LOOP, &ext2_medium_loop_ops)) abort();
    if (strcmp(argv[7], "-")) {
        if (loop_dev_attach(argv[7], slot)) abort();
        if (mode("detach") || mode("reattach")) loop_dev_detach(slot);
        if (mode("reattach") && loop_dev_attach("new.nhd", slot)) abort();
    }
    recording = 1;
    int start = atoi(argv[5]), length = atoi(argv[6]);
    int rc = start < 0 ? ext2_format(drv, length) : ext2_format_at(drv, start, length);
    printf("result %d\n", rc);
    return 0;
}
