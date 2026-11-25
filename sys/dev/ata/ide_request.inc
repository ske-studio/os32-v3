#include "install_access.h"
/* Public request validation/splitting; no persistent state or retries. */
static int ide_request(int drive, u32 lba, u32 count, void *read_buf,
                       const void *write_buf)
{
    u8 *dst = (u8 *)read_buf;
    const u8 *src = (const u8 *)write_buf;
    u16 spt;
    int mode;

    drive &= IDE_MAX_DRIVES - 1;
    if (!drive_present[drive]) return IDE_ERR_NO_DRIVE;
    if (!ide_addr_range_ok(&drive_geom[drive], lba, count)) return IDE_ERR_RANGE;
    mode = ide_addr_mode(&drive_geom[drive], (u16 *)0, &spt, (u32 *)0);
    while (count) {
        IdeAddr a;
        u32 n = count < IDE_COMMAND_MAX_SECTORS ? count : IDE_COMMAND_MAX_SECTORS;
        int ret;

        if (drive_info[drive].phys_sector_size == SASI_SECTOR_BYTES) {
            /* Preserve the historical 512-byte transfer and LBA+1 command
             * sequence. FatFs itself submits SASI requests at LBA+2. */
            n = 1;
        } else if (mode != IDE_AMODE_LBA28) {
            u32 left_in_track = spt - (lba % spt);
            if (n > left_in_track) n = left_in_track;
        }
        ret = ide_addr_make(&drive_geom[drive], drive, lba, &a);
        if (ret != IDE_OK) return ret;
        ret = ide_pio_command(drive, &a, n, dst, src);
        if (ret != IDE_OK) return ret;
        lba += n;
        count -= n;
        if (src) src += n * SYS_HDD_SECTOR_SIZE;
        else dst += n * SYS_HDD_SECTOR_SIZE;
    }
    return IDE_OK;
}

static int ide_read_one(int drive, u32 lba, void *buf)
{
    IdeAddr a;
    int ret;
    drive &= IDE_MAX_DRIVES - 1;
    if (!drive_present[drive]) return IDE_ERR_NO_DRIVE;
    ret = ide_addr_make(&drive_geom[drive], drive, lba, &a);
    if (ret != IDE_OK) return ret;
    return ide_pio_command(drive, &a, 1, buf, (const void *)0);
}

static int ide_write_one(int drive, u32 lba, const void *buf)
{
    IdeAddr a;
    int ret;
    drive &= IDE_MAX_DRIVES - 1;
    if (!drive_present[drive]) return IDE_ERR_NO_DRIVE;
    ret = ide_addr_make(&drive_geom[drive], drive, lba, &a);
    if (ret != IDE_OK) return ret;
    return ide_pio_command(drive, &a, 1, (void *)0, buf);
}

static int ide_managed_request(int drive, u32 lba, u32 count, void *dst,
                               const void *src, int single, int writing)
{
    struct install_access access;
    int rc;
    if (platform_install_medium()) {
        int handled;
        rc = install_raw_io(&access, drive & (IDE_MAX_DRIVES - 1), lba, count,
                            dst, src, writing, &handled);
        if (handled) return rc;
        if (!install_transfer_allowed(&access, lba, count)) {
            install_complete(&access);
            return IDE_ERR_RANGE;
        }
    }
    if (!single) rc = ide_request(drive, lba, count, dst, src);
    else if (writing) rc = ide_write_one(drive, lba, src);
    else rc = ide_read_one(drive, lba, dst);
    if (platform_install_medium()) install_complete(&access);
    return rc;
}

int ide_read_sector(int drive, u32 lba, void *buf)
{
    return ide_managed_request(drive, lba, 1, buf, 0, 1, 0);
}

int ide_write_sector(int drive, u32 lba, const void *buf)
{
    return ide_managed_request(drive, lba, 1, 0, buf, 1, 1);
}

int ide_read_sectors(int drive, u32 lba, u32 count, void *buf)
{
    return ide_managed_request(drive, lba, count, buf, 0, 0, 0);
}

int ide_write_sectors(int drive, u32 lba, u32 count, const void *buf)
{
    return ide_managed_request(drive, lba, count, 0, buf, 0, 1);
}

static int ide_chs_request(int drive, u16 cyl, u8 head, u8 sect, void *dst,
                           const void *src, enum install_op op)
{
    struct install_access access;
    IdeAddr a;
    int rc;
    drive &= IDE_MAX_DRIVES - 1;
    if (platform_install_medium()) {
        rc = install_admit(&access, drive, op, 0, 0, 0);
        if (rc) return rc;
    }
    ide_chs_addr(drive, cyl, head, sect, &a);
    rc = ide_pio_command(drive, &a, 1, dst, src);
    if (platform_install_medium()) install_complete(&access);
    return rc;
}

int ide_read_sector_chs(int drive, u16 cyl, u8 head, u8 sect, void *buf)
{
    return ide_chs_request(drive, cyl, head, sect, buf, 0, INSTALL_CHS);
}

int ide_write_sector_chs(int drive, u16 cyl, u8 head, u8 sect, const void *buf)
{
    return ide_chs_request(drive, cyl, head, sect, 0, buf, INSTALL_CHS_WRITE);
}
