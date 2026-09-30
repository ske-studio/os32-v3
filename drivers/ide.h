/* ======================================================================== */
/*  IDE.H — PC-98 IDE/ATA PIOドライバ (CHS専用)                              */
/*                                                                          */
/*  PC-98のIDE I/Oポートマッピング:                                          */
/*    0x640: Data (16-bit)        0x642: Features/Error                      */
/*    0x644: Sector Count         0x646: Sector Number / LBA[7:0]            */
/*    0x648: Cylinder Low / LBA[15:8]  0x64A: Cylinder High / LBA[23:16]    */
/*    0x64C: Drive/Head           0x64E: Command/Status                     */
/*    0x74C: Device Control / Alt Status                                     */
/*                                                                          */
/*  バンク切替: 0x430/0x432 でプライマリ/セカンダリ選択                      */
/*                                                                          */
/*  APIはLBA値で受け取る。レジスタへの指定は drivers/ide_addr.c が決める:    */
/*  IDENTIFY word 49 bit9 なら LBA28、無ければ現在の CHS (word 53 bit0)、    */
/*  それも無ければ既定の CHS (票 TASK_HDD_INSTALL 段 1、F13)。               */
/*  UNDOCUMENTED io_ide.md の「PC-9800 では LBA を使わない」は BIOS の話。     */
/*                                                                          */
/*  出典: NP21/W (ideio.c), DOSBox-X (ide.cpp), PC9800Bible §2-9,           */
/*        UNDOCUMENTED 9801/9821 Vol.2 io_ide.md                             */
/* ======================================================================== */

#ifndef IDE_H
#define IDE_H

#include "types.h"

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

/* ステータスビット */
#define IDE_ST_BSY     0x80   /* ビジー */
#define IDE_ST_DRDY    0x40   /* ドライブレディ */
#define IDE_ST_DF      0x20   /* デバイスフォルト */
#define IDE_ST_DSC     0x10   /* シーク完了 */
#define IDE_ST_DRQ     0x08   /* データ要求 */
#define IDE_ST_CORR    0x04   /* 訂正済みデータ */
#define IDE_ST_IDX     0x02   /* インデックス */
#define IDE_ST_ERR     0x01   /* エラー */

/* ATAコマンド */
#define IDE_CMD_READ       0x20   /* READ SECTOR(S) */
#define IDE_CMD_WRITE      0x30   /* WRITE SECTOR(S) */
#define IDE_CMD_IDENTIFY   0xEC   /* IDENTIFY DEVICE */

/* ドライブ選択 — PC-98ではCHS専用 (UNDOCUMENTED: bit6は常に0) */
#define IDE_DRV_SEL_CHS    0xA0   /* CHSモード: bit5=1, bit6=0, bit7=1 */

#define IDE_TIMEOUT_LOOP   1000000 /* BSY/DRQ待ちループカウント (長い) */
#define IDE_TIMEOUT_BSY    500000  /* 一般的なBSY等のループカウント */
#define IDE_NIEN           0x02   /* nIEN: 割り込み無効 */
#define IDE_SRST           0x04   /* SRST: バス (バンク) の全装置をリセット */

/* ====================================================================== */
/*  IDE ハードウェアタイミング仕様                                          */
/* ====================================================================== */
#define IDE_WAIT_NS             400    /* ドライブ選択等の必要ウェイト時間 (ns) */
#define IDE_BUS_CYCLE_NS        100    /* I/Oバスサイクル時間概算 (ns) */
#define IDE_SEL_SETTLE          (IDE_WAIT_NS / IDE_BUS_CYCLE_NS) /* 4回 */

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
    char model[41];      /* モデル名 */
    char serial[21];     /* シリアル番号 */
    char firmware[9];    /* ファームウェアリビジョン */
    int  lba_supported;  /* LBAサポート有無 */
    u16  phys_sector_size; /* 物理セクタサイズ (SASI=256, IDE=512) */
} IdeInfo;

/* IDENTIFY の幾何まわりの生の値 (票 TASK_HDD_INSTALL 段 0)。
 * **IdeInfo は KAPI の out 構造体 (96 B、インストーラが並びに依存) なので
 * 触らない** — 足りない語はこちらに別に持つ。カーネル内だけで使う。 */
typedef struct {
    u16 def_cyl;        /* word 1  既定シリンダ数 */
    u16 def_heads;      /* word 3  既定ヘッド数 */
    u16 def_spt;        /* word 6  既定セクタ数/トラック */
    u16 w49;            /* word 49 能力 (bit9 = LBA) */
    u16 w53;            /* word 53 (bit0 = word 54-58 が有効) */
    u16 cur_cyl;        /* word 54 現在のシリンダ数 */
    u16 cur_heads;      /* word 55 現在のヘッド数 */
    u16 cur_spt;        /* word 56 現在のセクタ数/トラック */
    u32 total;          /* word 60-61 LBA 総セクタ数 */
    u8  valid;          /* 1 = IDENTIFY が成功して値を持っている */
} IdeGeom;

/* 最後に成功した IDENTIFY の幾何 (ide_init が drive 0-3 を埋める)。
 * 0 = 値あり、負 = そのドライブは IDENTIFY に答えていない。 */
int ide_get_geom(int drive, IdeGeom *out);

/* IDE初期化 (ドライブ検出) */
int ide_init(void);

/* ドライブ情報取得 */
int ide_identify(int drive, IdeInfo *info);

/* CHS ネイティブ 1セクタ読み書き */
int ide_read_sector_chs(int drive, u16 cyl, u8 head, u8 sect, void *buf);
int ide_write_sector_chs(int drive, u16 cyl, u8 head, u8 sect,
                         const void *buf);

/* LBA の読み書き (KAPI slot ide_read_sector / ide_write_sector(s) と
 * drivers/dev.c の hd0-3)。範囲外は 1 セクタも読み書きせずに IDE_ERR_RANGE。 */
int ide_read_sector(int drive, u32 lba, void *buf);
int ide_write_sector(int drive, u32 lba, const void *buf);
int ide_read_sectors(int drive, u32 lba, u32 count, void *buf);
int ide_write_sectors(int drive, u32 lba, u32 count, const void *buf);

/* そのドライブのセクタ指定の方式 (IDE_AMODE_*、drivers/ide_addr.h)。 */
int ide_addr_mode_of(int drive);
/* [lba, lba+count) がそのドライブの指定の方式で指せる範囲か (LBA28 の上限・総数・
 * CHS のシリンダ 16 ビット・桁あふれ)。1 = 収まる / 0 = 収まらないかドライブが無い。
 * 書き込みの**前に**範囲全体を照合するため (ext2_format_at、Codex C4)。 */
int ide_range_ok(int drive, u32 lba, u32 count);

/* ドライブ存在チェック */
int ide_drive_present(int drive);

/* キャッシュされたドライブ情報の取得 */
int ide_get_info(int drive, IdeInfo *info);

#endif /* IDE_H */
