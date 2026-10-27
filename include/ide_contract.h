/* Shared IDE values and the fixed 96-byte KAPI information layout. */
#ifndef IDE_CONTRACT_H
#define IDE_CONTRACT_H

#include "types.h"

#define SASI_SECTOR_BYTES      256

#define IDE_BANK_COUNT         2
#define IDE_DRIVES_PER_BANK    2
#define IDE_MAX_DRIVES         (IDE_BANK_COUNT * IDE_DRIVES_PER_BANK)

#define IDE_MODEL_LEN          40

#define IDE_SERIAL_LEN         20

#define IDE_FIRMWARE_LEN       8

/* エラー定義 */
#define IDE_OK              0
#define IDE_ERR_TIMEOUT    -1
#define IDE_ERR_NO_DRIVE   -2
#define IDE_ERR_IO         -3
#define IDE_ERR_RANGE      -4    /* 指定の方式で指せないセクタ (LBA28 上限・総数・
                                  * シリンダ 16 ビット・lba + count の桁あふれ) */

/* IDENTIFY DEVICE情報 */
typedef struct {
    u32 total_sectors;   /* LBA総セクタ数 */
    u16 cylinders;       /* シリンダ数 */
    u16 heads;           /* ヘッド数 */
    u16 sectors;         /* セクタ数/トラック */
    u32 size_mb;         /* サイズ(MB) */
    char model[IDE_MODEL_LEN + 1];      /* モデル名 */
    char serial[IDE_SERIAL_LEN + 1];     /* シリアル番号 */
    char firmware[IDE_FIRMWARE_LEN + 1];    /* ファームウェアリビジョン */
    int  lba_supported;  /* LBAサポート有無 */
    u16  phys_sector_size; /* 物理セクタサイズ (SASI=256, IDE=512) */
} IdeInfo;

#endif /* IDE_CONTRACT_H */
