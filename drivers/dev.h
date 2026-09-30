/* ======================================================================== */
/*  DEV.H — デバイス抽象化層                                                */
/*                                                                          */
/*  CHS / LBA 2系統ブロックI/O:                                            */
/*    blk_read_chs — CHS ネイティブ (FDD, loop)                           */
/*    blk_read     — LBA ネイティブ (ATAPI CD、IDE HDD)                    */
/*                   HDD の LBA → レジスタ値は drivers/ide_addr.c が決める */
/*                                                                          */
/*  dev_blk_read_lba() 汎用ラッパー:                                       */
/*    CHS デバイス → LBA→CHS 変換 → blk_read_chs                         */
/*    LBA デバイス → blk_read 直接呼び出し                                 */
/* ======================================================================== */

#ifndef DEV_H
#define DEV_H

#include "types.h"  /* u8, u16, u32 */

/* ======== デバイスタイプ ======== */
typedef enum {
    DEV_CHAR,       /* キャラクタデバイス (con, serial, fm) */
    DEV_BLOCK       /* ブロックデバイス (fdd, hdd) */
} DevType;

/* ======== バスタイプ (デバイスが接続されているバスを識別) ======== */
typedef enum {
    DEV_BUS_NONE,       /* 不明/未設定 */
    DEV_BUS_FDC,        /* uPD765A FDC (FDD) */
    DEV_BUS_IDE,        /* ATA/IDE PIO */
    DEV_BUS_SCSI,       /* WD33C93 SCSI (将来) */
    DEV_BUS_ATAPI,      /* ATAPI CD-ROM (IDEセカンダリ) */
    DEV_BUS_LOOP        /* ループバックデバイス (仮想) */
} DevBusType;

/* ======== デバイス構造体 ======== */
typedef struct _Device Device;
struct _Device {
    const char *name;        /* デバイス名 ("fdd0", "con", "serial") */
    DevType     type;        /* DEV_CHAR or DEV_BLOCK */
    DevBusType  bus_type;    /* バスタイプ (IDE/FDC/SCSI/ATAPI/LOOP) */
    u8          bus_id;      /* バス上のID (IDE:0-3, SCSI:0-6, FDC:0-1) */
    int         sect_size;   /* セクタサイズ (ブロックデバイスのみ) */
    u32         total_sects; /* 総セクタ数 (ブロックデバイスのみ) */

    /* ブロックデバイス用: LBA 系統 (ATAPI CD 等 LBA ネイティブデバイス) */
    int       (*blk_read)(Device *self, int lba, int count, void *buf);
    int       (*blk_write)(Device *self, int lba, int count, const void *buf);

    /* キャラクタデバイス用: バイト単位 */
    int       (*chr_read)(Device *self, void *buf, int len);
    int       (*chr_write)(Device *self, const void *buf, int len);

    /* デバイス固有制御 */
    int       (*ioctl)(Device *self, int cmd, void *arg);

    void       *priv;        /* ドライバ固有データへのポインタ */

    /* CHS 系統 I/O (FDD, HDD, loop — sect は 1-based) */
    int       (*blk_read_chs)(Device *self, u16 cyl, u8 head, u8 sect,
                              void *buf);
    int       (*blk_write_chs)(Device *self, u16 cyl, u8 head, u8 sect,
                               const void *buf);

    /* CHS ジオメトリ (CHS デバイス用) */
    u16        cyls;          /* シリンダ数 */
    u16        heads;         /* ヘッド数 */
    u16        spt;           /* セクタ/トラック */
};

/* ======== 定数 ======== */
#define MAX_DEVICES  16  /* fd0/fd1 + lo0..lo3 + hd0..hd3 + cd0 + 予備 */

/* ======== API ======== */

/* デバイスシステム初期化 (全ドライバ登録) */
void dev_init(void);

/* デバイス登録 (戻り値: 0=成功, -1=満杯) */
int dev_register(Device *dev);

/* HDDデバイスの登録 (ユーティリティ) */
void dev_register_hdd(int drive);

/* SCSIデバイスの登録 (将来用スタブ — WD33C93ドライバ完成後に実装) */
void dev_register_scsi(int scsi_id);

/* CD-ROMデバイスの登録 (ATAPIドライバ検出済みの場合) */
void dev_register_cdrom(void);

/* デバイス検索 (名前で) */
Device *dev_find(const char *name);

/* デバイス取得 (インデックスで, 0〜dev_count()-1) */
Device *dev_get(int index);

/* 登録デバイス数 */
int dev_count(void);

/* デバイス名一覧取得 (Tab補完用: names配列にポインタを格納, 戻り値=個数) */
int dev_get_names(const char **names, int max);

/* API用デバイス情報取得 (構造体を隠蔽) */
int dev_api_get_info(int idx, char *name, int name_max, int *type, u32 *total_sects);

/* ======== 汎用 LBA ラッパー ======== */
/* CHS デバイス: ジオメトリから CHS 変換 → blk_read_chs
 * LBA デバイス: blk_read を直接呼び出し */
int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf);
int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *buf);

#endif /* DEV_H */
