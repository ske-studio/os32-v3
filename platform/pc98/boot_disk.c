/* PC-9821 IDE geometry adapter; IDE retains ownership of its state. */
#include "boot_disk.h"
#include "../../init/boot_disk.h"
#include "../../drivers/ata/ide.h"
#include "../../drivers/ata/ide_addr.h"

STATIC_ASSERT(HDD_AMODE_LBA28 == IDE_AMODE_LBA28 &&
              HDD_AMODE_CHS_CUR == IDE_AMODE_CHS_CUR &&
              HDD_AMODE_CHS_DEF == IDE_AMODE_CHS_DEF, hdd_amode_same);
static int pc98_disk_valid(int drive)
{
    return drive >= 0 && drive < IDE_MAX_DRIVES;
}

/* IDE ドライブ番号 → BIOS の DA。バンク 0 のマスタ = 80h、スレーブ = 81h。
 * バンク 1 (ドライブ 2/3) は BIOS の HDD として数えない (0 = 無し)。 */
static int pc98_disk_bios_da(int ide_drive)
{
    if (ide_drive == 0) return BOOTINFO_DA_HDD0;
    if (ide_drive == 1) return BOOTINFO_DA_HDD0 + 1;
    return 0;
}

static int pc98_disk_report_ata(int drive, struct bootinfo_ata *out)
{
    IdeGeom g;

    if (ide_get_geom(drive, &g) != IDE_OK) return -1;
    out->def_cyl   = g.def_cyl;
    out->def_heads = g.def_heads;
    out->def_spt   = g.def_spt;
    out->cur_cyl   = g.cur_cyl;
    out->cur_heads = g.cur_heads;
    out->cur_spt   = g.cur_spt;
    out->cur_valid = (u8)((g.w53 & 0x0001) ? 1 : 0);
    out->lba       = (u8)((g.w49 & IDE_ID_CAP_LBA) ? 1 : 0);
    out->total     = g.total;
    return 0;
}

static int pc98_disk_default_geom(int drive, u16 *heads, u16 *spt)
{
    IdeInfo info;
    if (ide_get_info(drive, &info) != IDE_OK) return -1;
    if (info.heads == 0 || info.sectors == 0) return -1;
    if (heads) *heads = info.heads;
    if (spt) *spt = info.sectors;
    return 0;
}

static void pc98_disk_fill_ata(int drive, HddGeom *out)
{
    IdeGeom g;
    if (ide_get_geom(drive, &g) == IDE_OK && ide_drive_present(drive)) {
        out->ata_present   = 1;
        out->ata_def_cyl   = g.def_cyl;
        out->ata_def_heads = g.def_heads;
        out->ata_def_spt   = g.def_spt;
        out->ata_cur_cyl   = g.cur_cyl;
        out->ata_cur_heads = g.cur_heads;
        out->ata_cur_spt   = g.cur_spt;
        out->ata_w49       = g.w49;
        out->ata_w53       = g.w53;
        out->ata_total     = g.total;
        out->addr_mode     = (u8)ide_addr_mode_of(drive);
    }
}

static const struct boot_disk_ops pc98_disk_ops = {
    pc98_disk_valid, pc98_disk_bios_da, pc98_disk_report_ata,
    pc98_disk_default_geom, pc98_disk_fill_ata
};

void pc98_boot_disk_bind(void)
{
    bootinfo_disk_register(&pc98_disk_ops);
}
