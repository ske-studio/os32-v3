/* ======================================================================== */
/*  DISK.C — フロッピーディスクI/O (レガシー互換スタブ)                     */
/*                                                                          */
/*  旧 disk_read_lba / disk_write_lba / disk_lba_to_chs / disk_chs_to_lba  */
/*  は Phase 4 (CHS ネイティブ化) で削除済み。                              */
/*                                                                          */
/*  FDD セクタ読み書きは以下を使用すること:                                 */
/*    - CHS ネイティブ: fdc_read_sector / fdc_write_sector (fdc.h)         */
/*    - Device API 経由: dev_blk_read_lba (dev.h) — LBA→CHS 自動変換     */
/*                                                                          */
/*  このファイルはビルド互換のため残存。将来的に削除可能。                   */
/* ======================================================================== */

#include "disk.h"
