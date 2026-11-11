#include <stdio.h>
#include <stdlib.h>
#include "platform/powermac/root_boot.h"
#include "fs/vfs.h"
#include "lib/kstring.h"

volatile u32 tick_count;
void *kmalloc(u32 n) { return malloc(n); }
void *kzalloc(u32 n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
void vfs_fd_stale_mount(void *p) { (void)p; }
void vfs_fd_invalidate_mount(void *p) { (void)p; }
int main(int argc, char **argv)
{
    struct boot_root_image image;
    char out[64];
    FILE *f;
    long size;
    if (argc != 2 || !(f = fopen(argv[1], "rb"))) return 1;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) return 1;
    image.base = malloc((size_t)size);
    image.bytes = (u32)size;
    if (!image.base || fread((void *)image.base, 1, (size_t)size, f) != (size_t)size) return 1;
    fclose(f);
    if (pm_root_boot(&image)) return 2;
    if (vfs_read("/etc/motd", out, sizeof(out)) != 11 || kstrncmp(out, "RAM root!\n!", 11)) return 3;
    if (vfs_write("/tmp/f", "hello", 5) != 5 || vfs_read("/tmp/f", out, sizeof(out)) != 5 || kstrncmp(out, "hello", 5)) return 4;
    if (vfs_write("/etc/motd", "x", 1) >= 0) return 5;
    return 0;
}
