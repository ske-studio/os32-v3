/* Product FatFs and its VFS adapter read a 1.44 MB FAT12 floppy. */
#include "fat/fatfs/ff.h"
#include "fat/fatfs/diskio.h"
#include "kstring.h"
#include "loop_contract.h"
#include "vfs.h"

extern int fatfs_init(void);
extern const u8 fat_image[];
DSTATUS disk_initialize(BYTE drive) { return drive == 0 ? 0 : STA_NODISK; }
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }
void diskio_set_fdd_drive(int drv) { if (drv != 0) __builtin_trap(); }

DRESULT disk_read(BYTE drive, BYTE *buf, LBA_t sector, UINT count)
{
    if (drive || sector >= 2880 || count > 2880 - sector) return RES_PARERR;
    kmemcpy(buf, fat_image + sector * 512, count * 512);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drive, BYTE cmd, void *buf)
{
    if (drive) return RES_PARERR;
    if (cmd == GET_SECTOR_SIZE) *(WORD *)buf = 512;
    else if (cmd != CTRL_SYNC) return RES_PARERR;
    return RES_OK;
}

int probe_fat(void)
{
    if (fatfs_init() || vfs_mount("/fat", "fd0", "fat") ||
        loop_dev_attach("/fat/A.IMG", 0)) return 26;
    if (loop_dev_attach("/fat/a.img", 1) != LOOP_ERR_BUSY) return 27;
    loop_dev_detach(0);
    return vfs_umount_checked("/fat") ? 26 : 0;
}
