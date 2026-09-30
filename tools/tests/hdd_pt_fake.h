/* ======================================================================== */
/*  HDD_PT_FAKE.H — ext2 の RAM ディスク試験に区画表を置く共通の足場        */
/*                                                                          */
/*  票 TASK_HDD_INSTALL 段 1-5 で ext2_find_partition の LBA 1088 への       */
/*  フォールバックを廃止した。以前の試験は「区画表が空 → 1088」に寄りかかって */
/*  いたので、RAM ディスクの LBA 1 に**本物の共有部 (drivers/pc98pt.c) で**  */
/*  OS32 の区画 (標準配置、8/17) を書く。区画はシリンダ単位なので、ディスクは */
/*  PT_FAKE_ROUNDUP で FS の大きさをシリンダへ切り上げた分だけ確保する。     */
/*                                                                          */
/*  使い方: ハーネスの先頭 (ext2_priv.h の後) で #include し、ディスクを     */
/*  0 で埋めた直後に pt_fake_write(g_disk, DISK_BASE_LBA, 長さ) を呼ぶ。     */
/*  bootinfo_part_geom の贋物も置く (カーネルでは BIOS 幾何 → IDENTIFY)。   */
/* ======================================================================== */
#ifndef HDD_PT_FAKE_H
#define HDD_PT_FAKE_H

#include "../../drivers/pc98pt.h"
#include "../../drivers/pc98pt.c"
#include "ext2_layout.c"          /* -I fs (変異では写しの fs) から */

#define PT_FAKE_HEADS   8u
#define PT_FAKE_SPT     17u
#define PT_FAKE_CYL     (PT_FAKE_HEADS * PT_FAKE_SPT)
#define PT_FAKE_ROUNDUP(n) ((((n) + PT_FAKE_CYL - 1u) / PT_FAKE_CYL) * PT_FAKE_CYL)

/* 区画表の CHS の幾何。hd0 だけ 8/17 (NP21/W の NHD と同じ)。 */
int bootinfo_part_geom(int ide_drive, u16 *heads, u16 *spt)
{
    if ((ide_drive & 3) != 0) return -1;
    if (heads) *heads = (u16)PT_FAKE_HEADS;
    if (spt)   *spt = (u16)PT_FAKE_SPT;
    return 1;   /* BOOTINFO_GEOM_BIOS (ext2_format は BIOS 幾何でないと書かない、m3) */
}

/* ATA の方式の上限 (ide.c の ide_range_ok)。これらの試験は ext2_format_at の範囲を
 * 見ないので、hd0 なら通す (範囲は RAM ディスクの dev_blk_* が断る)。 */
int ide_range_ok(int drive, u32 lba, u32 count)
{
    (void)lba; (void)count;
    return (drive & 3) == 0 ? 1 : 0;
}

/* disk の LBA 1 に [base, base+len) の OS32 区画を書く (len はシリンダの倍数)。
 * 0 = 書いた / -1 = 足場の組み違い (呼び手が CHECK する)。 */
static int pt_fake_write(u8 *disk, u32 base, u32 len)
{
    PC98PartEntry e;
    u32 i;

    for (i = 0; i < 512u; i++) disk[512u + i] = 0;
    if (pc98pt_make_os32(&e, base, len, PT_FAKE_HEADS, PT_FAKE_SPT) != PC98PT_OK)
        return -1;
    return pc98pt_put(disk + 512u, 0, &e) == PC98PT_OK ? 0 : -1;
}

#endif /* HDD_PT_FAKE_H */
