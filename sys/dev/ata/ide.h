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
/*  APIはLBA値で受け取る。レジスタへの指定は sys/dev/ata/ide_addr.c が決める:    */
/*  IDENTIFY word 49 bit9 なら LBA28、無ければ現在の CHS (word 53 bit0)、    */
/*  それも無ければ既定の CHS (票 TASK_HDD_INSTALL 段 1、F13)。               */
/*  UNDOCUMENTED io_ide.md の「PC-9800 では LBA を使わない」は BIOS の話。     */
/*                                                                          */
/*  出典: NP21/W (ideio.c), DOSBox-X (ide.cpp), PC9800Bible §2-9,           */
/*        UNDOCUMENTED 9801/9821 Vol.2 io_ide.md                             */
/* ======================================================================== */

#ifndef IDE_H
#define IDE_H

#include "ide_contract.h"

#include "config.h"

#define IDE_PIO_WORDS          (SYS_HDD_SECTOR_SIZE / sizeof(u16))
#define IDE_COMMAND_MAX_SECTORS 256u /* count レジスタの 0 は 256 */
#define IDE_IDENTIFY_WORDS     256
#define IDE_ID_MODEL_WORD      27
#define IDE_ID_SERIAL_WORD     10
#define IDE_ID_FIRMWARE_WORD   23
#define IDE_ID_MULTI_MAX_WORD      47
#define IDE_ID_MULTI_CURRENT_WORD  59
#define IDE_ID_MULTI_COUNT_MASK    0x00FF
#define IDE_ID_MULTI_VALID         0x0100
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
#define IDE_CMD_READ_MULTIPLE  0xC4
#define IDE_CMD_WRITE_MULTIPLE 0xC5
#define IDE_CMD_SET_MULTIPLE   0xC6
#define IDE_CMD_FLUSH_CACHE 0xE7
#define IDE_CMD_IDENTIFY   0xEC   /* IDENTIFY DEVICE */

#define IDE_SPINUP_MS          31000
#define IDE_PROBE_BSY_MS       1000
#define IDE_PROBE_DRQ_MS       50
#define IDE_IO_TIMEOUT_MS 10000u
#define IDE_EARLY_PHASE_POLLS   1000000 /* BSY/DRQ待ちループカウント (長い) */
#define IDE_EARLY_BSY_POLLS    500000  /* 一般的なBSY等のループカウント */
#define IDE_NIEN           0x02   /* nIEN: 割り込み無効 */
#define IDE_SRST           0x04   /* SRST: バス (バンク) の全装置をリセット */

/* ====================================================================== */
/*  IDE ハードウェアタイミング仕様                                          */
/* ====================================================================== */
#define IDE_WAIT_NS             400    /* ドライブ選択等の必要ウェイト時間 (ns) */
#define IDE_BUS_CYCLE_NS        100    /* I/Oバスサイクル時間概算 (ns) */
#define IDE_SEL_SETTLE          (IDE_WAIT_NS / IDE_BUS_CYCLE_NS) /* 4回 */

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

/* Internal reset notification: invalidate the bank's two HDD settings before SRST. */
void ide_multiple_reset_bank(int bank);

/* CHS ネイティブ 1セクタ読み書き */
int ide_read_sector_chs(int drive, u16 cyl, u8 head, u8 sect, void *buf);
int ide_write_sector_chs(int drive, u16 cyl, u8 head, u8 sect,
                         const void *buf);

int ide_read_sectors(int drive, u32 lba, u32 count, void *buf);

/* そのドライブのセクタ指定の方式 (IDE_AMODE_*、sys/dev/ata/ide_addr.h)。 */
int ide_addr_mode_of(int drive);

int ide_install_identify(u32 unit, IdeInfo *info, char serial[IDE_SERIAL_LEN]);
int ide_install_read(u32 unit, u32 lba, void *sector);
int ide_install_read_many(u32 unit, u32 lba, u32 count, void *sectors);
int ide_install_write(u32 unit, u32 lba, u32 count, const void *sectors);
int ide_install_flush(u32 unit);

#endif /* IDE_H */
