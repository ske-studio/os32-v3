#include "drivers/block/ram_ro.h"
#include "os32_kapi_shared.h"
#include <string.h>

void *kmemcpy(void *dst, const void *src, u32 n) { return memcpy(dst, src, n); }
void *kmemset(void *dst, int val, u32 n) { return memset(dst, val, n); }
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }

int main(void)
{
    static u8 image[4 * 2048], out[2 * 2048];
    struct boot_root_image root = {image, sizeof(image)};
    struct ram_ro ram;
    Device *dev;
    u32 generation = 99;
    for (u32 i = 0; i < sizeof(image); i++) image[i] = (u8)(i / 2048 + i % 251);
    if (ram_ro_register(&ram, &root)) return 1;
    dev = dev_find("rd0");
    if (dev != &ram.dev || dev->type != DEV_BLOCK || dev->bus_type != DEV_BUS_NONE ||
        dev->sect_size != 2048 || dev->total_sects != 4 || dev->media_check ||
        dev_media_check(dev, &generation) || generation) return 2;
    if (dev_blk_read_lba(dev, 1, 2, out) || memcmp(out, image + 2048, sizeof(out))) return 3;
    if (!dev_blk_read_lba(dev, 3, 2, out) || !dev_blk_read_lba(dev, 4, 1, out)) return 4;
    if (dev_blk_write_lba(dev, 0, 1, out) != OS32_ERR_ROFS) return 5;
    return 0;
}
