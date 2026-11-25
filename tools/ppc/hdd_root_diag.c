/* Compiled only into the disposable guest image, after root/path setup. */
#include "sys/fs/vfs.h"
#include "dev.h"
#include "kstring.h"
static void hdd_root_report(void)
{
    char id[16] = {0};
    Device *dev = dev_find("hd0");
    int n = vfs_read("/identity", id, sizeof(id) - 1);
    kprintf(7, "ROOT-TEST dev=%s id=%s bytes=%d\r\n", vfs_devname("/"), id, n);
    if (dev && dev->total_sects == 8192 && n == 8 && !kstrcmp(id, "selected")) {
        u8 data[1024];
        kmemset(data, 0x73, sizeof(data));
        int last = dev_blk_write_lba(dev, dev->total_sects - 1, 1, data);
        kmemset(data, 0xa9, sizeof(data));
        int beyond = dev_blk_write_lba(dev, dev->total_sects - 1, 2, data);
        kprintf(7, "ROOT-TEST last=%d beyond=%d\r\n", last, beyond);
    }
    kprintf(7, "ROOT-TEST done\r\n");
    arch_cpu_stop();
}
