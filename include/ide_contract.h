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

/* IDE初期化 (ドライブ検出) */
int ide_init(void);

/* ドライブ情報取得 */
int ide_identify(int drive, IdeInfo *info);

/* LBA の読み書き (KAPI slot ide_read_sector / ide_write_sector(s) と
 * drivers/base/dev.c の hd0-3)。範囲外は 1 セクタも読み書きせずに IDE_ERR_RANGE。
 * 最初の失敗で停止し、負の既存エラーを返す。再試行・部分成功数は返さない。
 * 失敗時の読取り先は未定義、書込みは一部変更済みになり得る。 */
int ide_read_sector(int drive, u32 lba, void *buf);
int ide_write_sector(int drive, u32 lba, const void *buf);
int ide_write_sectors(int drive, u32 lba, u32 count, const void *buf);

/* [lba, lba+count) がそのドライブの指定の方式で指せる範囲か (LBA28 の上限・総数・
 * CHS のシリンダ 16 ビット・桁あふれ)。1 = 収まる / 0 = 収まらないかドライブが無い。
 * 書き込みの**前に**範囲全体を照合するため (ext2_format_at、Codex C4)。 */
int ide_range_ok(int drive, u32 lba, u32 count);

/* ドライブ存在チェック */
int ide_drive_present(int drive);

/* キャッシュされたドライブ情報の取得 */
int ide_get_info(int drive, IdeInfo *info);

#endif /* IDE_CONTRACT_H */
