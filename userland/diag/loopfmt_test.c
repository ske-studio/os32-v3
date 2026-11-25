#include "os32api.h"
#include <stdio.h>
#include <string.h>

#define SECTOR 512
#define SECTORS 320
static unsigned char image[SECTORS * SECTOR];

static int read_image(KernelAPI *api, const char *path, int compare)
{
    unsigned char sector[SECTOR];
    int fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return 0;
    int i;
    for (i = 0; i < SECTORS; i++) {
        unsigned char *saved = image + i * SECTOR;
        if (api->sys_read(fd, sector, SECTOR) != SECTOR) break;
        if (compare && memcmp(saved, sector, SECTOR)) break;
        memcpy(saved, sector, SECTOR);
    }
    int ok = i == SECTORS && api->sys_read(fd, sector, 1) == 0;
    api->sys_close(fd);
    return ok;
}

static int filled(const unsigned char *buf, unsigned size)
{
    for (unsigned i = 0; i < size; i++)
        if (buf[i] != 0xab) return 0;
    return 1;
}

static unsigned little32(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static int super_ok(int lba, unsigned blocks)
{
    const unsigned char *sb = image + lba * SECTOR;
    return sb[56] == 0x53 && sb[57] == 0xef &&
           little32(sb + 4) == blocks && little32(sb) == blocks / 4;
}

int main(int argc, char **argv, KernelAPI *api)
{
    int passed = 0;
    u32 total = 0;
    int bps = 0;
    if (argc != 2) {
        printf("loopfmt_test: SKIP need foreground /sbin/cdinst.bin and scratch image\n");
        return 2;
    }
    const char *path = argv[1];
    if (!read_image(api, path, 0) || !filled(image, sizeof(image)) ||
        api->loop_status(0, &total, &bps) != 0 || api->loop_attach(path, 0) != 0) goto done;
    int rc = api->ext2_format_at(OS32_DRV_LOOP(0), 7, 256);
    api->loop_detach(0);
    if (rc || !read_image(api, path, 0) || !filled(image + 6 * SECTOR, SECTOR) ||
        !filled(image + 263 * SECTOR, SECTOR) || !super_ok(9, 128)) goto done;
    passed++;
    if (api->loop_attach(path, 0) != 0) goto done;
    rc = api->ext2_format(OS32_DRV_LOOP(0), 321);
    api->loop_detach(0);
    if (rc || !read_image(api, path, 0) || !super_ok(2, 160)) goto done;
    passed++;
    if (api->ext2_format(OS32_DRV_LOOP(0), 256) != -1 ||
        api->ext2_format_at(OS32_DRV_LOOP(0), 7, 256) != -1 || !read_image(api, path, 1)) goto done;
    passed++;
done:
    printf("readonly RAW / 512B D88: SKIP need samples\n");
    printf("loopfmt_test: %s %d/3\n", passed == 3 ? "PASS" : "FAIL", passed);
    return passed != 3;
}
