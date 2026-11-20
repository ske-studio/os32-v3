#include "pgalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include "platform/powermac/root_boot.h"
#include "fs/vfs.h"
#include "kernel/dev/ram_ro.h"
#include "fs/isofs/iso9660.h"
#include "fs/ramfs/ramfs.h"
#include "lib/kstring.h"

volatile u32 tick_count;
void *kmalloc(u32 n) { return malloc(n); }
void *kzalloc(u32 n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
void vfs_fd_stale_mount(void *p) { (void)p; }
void vfs_fd_invalidate_mount(void *p) { (void)p; }
static int step, fail_at = -1;
int __real_ram_ro_register(struct ram_ro *ram, const struct boot_root_image *image);
int __wrap_ram_ro_register(struct ram_ro *ram, const struct boot_root_image *image)
{
    if (fail_at == -2) return -1;
    return __real_ram_ro_register(ram, image);
}
int __real_vfs_register_fs(VfsOps *ops);
int __real_vfs_mount(const char *path, const char *dev, const char *fs);
int __wrap_vfs_register_fs(VfsOps *ops)
{
    if (step == fail_at) return -1;
    if ((step == 0 && ops == &iso9660_ops) ||
        (step == 2 && ops == &ramfs_ops)) step++;
    else return -1;
    return __real_vfs_register_fs(ops);
}
int __wrap_vfs_mount(const char *path, const char *dev, const char *fs)
{
    if (step == fail_at) return -1;
    if ((step == 1 && !kstrcmp(path, "/") && !kstrcmp(dev, "rd0") && !kstrcmp(fs, "iso9660")) ||
        (step == 3 && !kstrcmp(path, "/tmp") && !kstrcmp(dev, "rd1") && !kstrcmp(fs, "ramfs"))) step++;
    else return -1;
    return __real_vfs_mount(path, dev, fs);
}
static int frame_owned = 1, output_owned = 1;
enum { FRAME_PFN = 0x580, FRAME_PAGES = 80, OUTPUT_PFN = 0x800 };
u32 pgalloc_free_pages(void)
{
    return 1024 - frame_owned * FRAME_PAGES - output_owned * 2;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int pages)
{
    if (owner != LEDGER_OWNER_BOOT) return 0;
    if (frame_owned && pfn == FRAME_PFN && pages == FRAME_PAGES) {
        frame_owned = 0;
        return 1;
    }
    if (output_owned && pfn == OUTPUT_PFN && pages == 2) {
        output_owned = 0;
        return 1;
    }
    return 0;
}
static int release_ok(int registered)
{
    struct boot_root_image image = {P2V(OUTPUT_PFN * PAGE_SIZE), PAGE_SIZE + 1};
    u32 before, after;
    return !pm_root_release(&image, registered, FRAME_PFN, FRAME_PAGES, &before, &after)
        && after == before + FRAME_PAGES && !frame_owned && output_owned == registered;
}
int main(int argc, char **argv)
{
    struct boot_root_image image;
    char out[64];
    FILE *f;
    long size;
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) return 1;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) return 1;
    image.base = malloc((size_t)size);
    image.bytes = (u32)size;
    if (!image.base || fread((void *)image.base, 1, (size_t)size, f) != (size_t)size) return 1;
    fclose(f);
    int registered = -1;
    if (argc == 3) {
        fail_at = atoi(argv[2]);
        return pm_root_boot(&image, &registered) && registered == (fail_at > 0) && release_ok(registered) ? 0 : 6;
    }
    if (pm_root_boot(&image, &registered) || !registered) return 2;
    if (vfs_read("/etc/motd", out, sizeof(out)) != 11 || kstrncmp(out, "RAM root!\n!", 11)) return 3;
    if (step != 4 || vfs_write("/tmp/f", "hello", 5) != 5 || vfs_read("/tmp/f", out, sizeof(out)) != 5 || kstrncmp(out, "hello", 5)) return 4;
    if (vfs_write("/etc/motd", "x", 1) >= 0) return 5;
    return release_ok(registered) ? 0 : 7;
}
