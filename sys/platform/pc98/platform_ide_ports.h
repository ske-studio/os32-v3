#ifndef PLATFORM_IDE_PORTS_H
#define PLATFORM_IDE_PORTS_H

/* IDE I/Oポート (PC-98) */
#define IDE_DATA       0x640   /* データレジスタ (16-bit) */
#define IDE_ERROR      0x642   /* エラーレジスタ (R) */
#define IDE_FEATURES   0x642   /* フィーチャーレジスタ (W) */
#define IDE_SECT_CNT   0x644   /* セクタカウント */
#define IDE_SECT_NUM   0x646   /* セクタ番号 / LBA[7:0] */
#define IDE_CYL_LO     0x648   /* シリンダ下位 / LBA[15:8] */
#define IDE_CYL_HI     0x64A   /* シリンダ上位 / LBA[23:16] */
#define IDE_DRV_HEAD   0x64C   /* ドライブ/ヘッド */
#define IDE_STATUS     0x64E   /* ステータス (R) */
#define IDE_COMMAND    0x64E   /* コマンド (W) */
#define IDE_ALT_STATUS 0x74C   /* 代替ステータス (R) */
#define IDE_DEV_CTRL   0x74C   /* デバイスコントロール (W) */

/* バンクレジスタ (プライマリ/セカンダリ切替) */
#define IDE_BANK0      0x430
#define IDE_BANK1      0x432

#endif
