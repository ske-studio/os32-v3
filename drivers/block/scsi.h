/* ======================================================================== */
/*  SCSI.H - PC-9801 SCSI I/O ポート定義 (WD33C93) + DA/UA 定数             */
/*                                                                          */
/*  Phase 5 ではスタブのみ。将来の SCSI ドライバ実装用にポート定義と         */
/*  DA/UA 定数を予約する。                                                   */
/*                                                                          */
/*  出典: UNDOCUMENTED 9801/9821 Vol.2 io_scsi.md                           */
/*        PC9800Bible 2-9-4 ハードディスクBIOS                               */
/* ======================================================================== */

#ifndef SCSI_H
#define SCSI_H

#include "types.h"

/* ======== PC-9801 SCSI I/O ポート (WD33C93) ======== */
/* 2段階I/O: 0CC0h にレジスタアドレスを書き込み、0CC2h でデータR/W */
#define SCSI_IO_ADDR    0x0CC0  /* レジスタアドレスセット / 補助ステータス */
#define SCSI_IO_DATA    0x0CC2  /* コントロールレジスタ (R/W) */
#define SCSI_IO_STATUS  0x0CC4  /* ステータス / 割り込み制御 */

/* ======== 補助ステータス (SCSI_IO_ADDR READ) ======== */
#define SCSI_AUX_INT    0x80    /* 割り込み発生中 */
#define SCSI_AUX_LCI    0x40    /* LastCommandIgnored */
#define SCSI_AUX_BSY    0x20    /* ChipBusy */
#define SCSI_AUX_CIP    0x10    /* CommandInProgress */
#define SCSI_AUX_PE     0x02    /* パリティエラー */
#define SCSI_AUX_DBR    0x01    /* 有効データあり */

/* ======== WD33C93 レジスタアドレス (AR) ======== */
#define SCSI_REG_OWN_ID         0x00
#define SCSI_REG_CONTROL        0x01
#define SCSI_REG_TIMEOUT        0x02
#define SCSI_REG_TOTAL_SECTORS  0x03
#define SCSI_REG_TOTAL_HEAD     0x04
#define SCSI_REG_TOTAL_CYL_MSB  0x05
#define SCSI_REG_TOTAL_CYL_LSB  0x06
#define SCSI_REG_LOGICAL_ADDR3  0x07    /* MSB */
#define SCSI_REG_LOGICAL_ADDR2  0x08
#define SCSI_REG_LOGICAL_ADDR1  0x09
#define SCSI_REG_LOGICAL_ADDR0  0x0A    /* LSB */
#define SCSI_REG_SECTOR_NUM     0x0B
#define SCSI_REG_HEAD_NUM       0x0C
#define SCSI_REG_CYL_MSB        0x0D
#define SCSI_REG_CYL_LSB        0x0E
#define SCSI_REG_TARGET_LUN     0x0F
#define SCSI_REG_CMD_PHASE      0x10
#define SCSI_REG_SYNC_XFER      0x11
#define SCSI_REG_XFER_COUNT2    0x12    /* MSB */
#define SCSI_REG_XFER_COUNT1    0x13
#define SCSI_REG_XFER_COUNT0    0x14    /* LSB */
#define SCSI_REG_DEST_ID        0x15
#define SCSI_REG_SOURCE_ID      0x16
#define SCSI_REG_SCSI_STATUS    0x17    /* READ only */
#define SCSI_REG_COMMAND        0x18
#define SCSI_REG_DATA           0x19

/* ======== WD33C93 コマンド ======== */
#define SCSI_CMD_RESET          0x00
#define SCSI_CMD_ABORT          0x01
#define SCSI_CMD_SELECT_ATN     0x06
#define SCSI_CMD_SELECT_NOATN   0x07
#define SCSI_CMD_SEL_ATN_XFER   0x08
#define SCSI_CMD_SEL_XFER       0x09
#define SCSI_CMD_XFER_INFO      0x20

/* ======== SCSI DA/UA 範囲 (INT 1Bh) ======== */
#define SCSI_DAUA_ABS_BASE  0xA0  /* 絶対アドレス: A0h-A7h (ID 0-7) */
#define SCSI_DAUA_REL_BASE  0x20  /* 相対アドレス: 20h-27h */
#define SCSI_DAUA_DEV_BASE  0xC0  /* 汎用デバイス: C0h-C7h (MO/CD等) */

/* ======== BIOS パラメータテーブル (BDA) ======== */
/* 0000:0460h-047Fh — SCSI ID #0-#6 パラメータ (各4バイト)
 * byte 0: セクタ数 (SPT)
 * byte 1: ヘッド数
 * byte 2-3: シリンダ数 (LE) */
#define SCSI_BDA_PARAM_BASE  0x0460
#define SCSI_BDA_PARAM_SIZE  4

/* ======== INT 1Bh HDD BIOS コマンドコード ======== */
#define SCSI_BIOS_VERIFY     0x01
#define SCSI_BIOS_INIT       0x03
#define SCSI_BIOS_SENSE      0x04
#define SCSI_BIOS_SENSE_EX   0x84    /* ジオメトリ取得 (BX=セクタ長等) */
#define SCSI_BIOS_WRITE      0x05
#define SCSI_BIOS_READ       0x06
#define SCSI_BIOS_RECALIBRATE 0x07
#define SCSI_BIOS_FORMAT     0x0D
#define SCSI_BIOS_RETRACT    0x0F

/* ======== 将来の API (Phase 5 ではスタブのみ) ========
 * int scsi_init(void);
 * int scsi_read_sector(int id, u32 lba, void *buf);
 * int scsi_write_sector(int id, u32 lba, const void *buf);
 * int scsi_sense(int id, u16 *cyls, u8 *heads, u8 *spt, u16 *sect_size);
 */

#endif /* SCSI_H */
