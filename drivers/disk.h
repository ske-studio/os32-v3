/* ======================================================================== */
/*  DISK.H — フロッピーディスク定数 (PC-98 1MB 2HD)                         */
/*                                                                          */
/*  FDCドライバ (fdc.h) のパラメータをリエクスポートする。                   */
/*  セクタ読み書きは fdc_read_sector/fdc_write_sector を直接使用すること。   */
/*  Device API 経由では dev_blk_read_lba (dev.h) を使用すること。           */
/* ======================================================================== */

#ifndef DISK_H
#define DISK_H

#include "fdc.h"    /* FDCドライバAPI + u8/u16/u32型 */

/* ======== ディスクパラメータ (fdc.hの定義を再エクスポート) ======== */
#define DISK_CYL       FDC_CYLINDERS
#define DISK_HEAD      FDC_HEADS
#define DISK_SPT       FDC_SPT
#define DISK_SECT_SZ   FDC_SECTOR_SIZE
#define DISK_TOTAL_SEC FDC_TOTAL_SECTORS

/* 旧 LBA API (disk_read_lba, disk_write_lba, disk_lba_to_chs, disk_chs_to_lba)
 * は Phase 4 で削除済み。FDD アクセスは以下の方法を使用すること:
 *   - カーネル内部: fdc_read_sector / fdc_write_sector (CHS ネイティブ)
 *   - Device API 経由: dev_blk_read_lba (LBA → CHS 自動変換) */

#endif /* DISK_H */
