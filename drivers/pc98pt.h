/* ======================================================================== */
/*  PC98PT.H — PC-98 区画表 (LBA 1) の読み書きの共有部                      */
/*                                                                          */
/*  票 TASK_HDD_INSTALL 段 1-4 (F10)。区画表を触る全員がここを通す:         */
/*    カーネル   fs/ext2_super.c の ext2_find_partition                      */
/*    カーネル   fs/fatfs_vfs.c の FAT 区画探索 (PC98PartEntry を共有)       */
/*    ローダ     boot/boot_main.c の find_partition (loader_hdd.bin)         */
/*    シェル     userland/shell/cmd_hdprep.c (hdprep)                        */
/*    インストーラ userland/system/inst_disk.c (cdinst / install、段 2)      */
/*    ホスト     tools/pc98pt.py (nhd_deploy.py の書き手、同じ値をホスト試験  */
/*               tools/tests/test_hdd_stage1.py が突き合わせる)              */
/*                                                                          */
/*  **標準配置** (FreeBSD pc98 の diskpc98.h / MS-DOS と同じ、32B × 16):     */
/*    +0  mid     bit7 = 起動可能                                           */
/*    +1  sid     システム ID (bit7 = アクティブ)                           */
/*    +2  予約 2B                                                           */
/*    +4  IPL セクタ  +5 IPL ヘッド  +6-7 IPL シリンダ                       */
/*    +8  開始セクタ  +9 開始ヘッド  +10-11 開始シリンダ                      */
/*    +12 終了セクタ  +13 終了ヘッド +14-15 終了シリンダ                      */
/*    +16 名前 16B                                                          */
/*  2026-09-23 までの OS32 は開始を +6/+7/+8-9、終了を +10〜13 に書く独自の  */
/*  配置で、書き手と読み手が揃っていたので NP21/W では出なかった。           */
/*                                                                          */
/*  **区画はシリンダ単位**。開始は (シリンダ, ヘッド, セクタ) を幾何で LBA   */
/*  にし、終わりは「終了シリンダの次のシリンダの先頭」(排他) とする — 終了   */
/*  ヘッド / セクタは読まない (MS-DOS の表は 0 を書くことがあり、OS32 の     */
/*  書き手は最終ヘッド・最終セクタを書く。どちらでも同じ長さになる)。        */
/*  HDD BIOS のセクタ番号は 0 始まり (FDD の 1 始まりとは違う)。              */
/*                                                                          */
/*  **純粋関数だけ**。I/O も静的状態も持たない。ブートローダ (-Iboot だけ)   */
/*  でもそのまま組めるよう、型は C の素の型で書く (u32 = unsigned long は     */
/*  カーネル・ローダ・SDK で共通)。32 ビットの桁あふれに頼る計算はしない —   */
/*  ホスト試験は 64 ビットで回る。                                           */
/* ======================================================================== */

#ifndef PC98PT_H
#define PC98PT_H

/* ---- 表の形 ------------------------------------------------------------- */
#define PC98PT_LBA            1      /* 区画表のセクタ */
#define PC98PT_SECTOR_SIZE    512
#define PC98PT_ENTRY_SIZE     32
#define PC98PT_MAX_ENTRIES    16     /* 512 / 32 */

/* 項目内のオフセット (標準配置) */
#define PC98PT_OFF_MID        0
#define PC98PT_OFF_SID        1
#define PC98PT_OFF_IPL_SECT   4
#define PC98PT_OFF_IPL_HEAD   5
#define PC98PT_OFF_IPL_CYL    6
#define PC98PT_OFF_SSECT      8
#define PC98PT_OFF_SHEAD      9
#define PC98PT_OFF_SCYL       10
#define PC98PT_OFF_ESECT      12
#define PC98PT_OFF_EHEAD      13
#define PC98PT_OFF_ECYL       14
#define PC98PT_OFF_NAME       16
#define PC98PT_NAME_LEN       16

/* OS32 の ext2 区画の印。cdinst / install / nhd_deploy.py が 2026-09 以前から
 * 書いている値をそのまま使う (mid 0x80 = 起動可能、sid 0xE2 = OS32 ext2)。 */
#define PC98PT_MID_BOOTABLE   0x80
#define PC98PT_SID_OS32       0xE2
#define PC98PT_NAME_OS32      "OS32"

/* 1 シリンダの上限 (ヘッド・セクタは 1 バイト、シリンダは 2 バイト) */
#define PC98PT_MAX_CYL        0xFFFFUL

/* ---- 生の 32 バイト (fs/fatfs_vfs.c と共有する並び) ----------------------
 * 全フィールドが自然な境界にあるので詰め物は入らない (32 B)。**媒体を読む
 * コードはこの構造体を重ねず**、下の pc98pt_get / pc98pt_put でバイト列から
 * 取り出す (エンディアンと境界に寄りかからない)。構造体はオフセットの正典を
 * 目に見える形で置くためのもの — ホスト試験が offsetof と上の OFF_* の一致を
 * 見る。 */
typedef struct {
    unsigned char  bootable;        /* +0  mid */
    unsigned char  sys_id;          /* +1  sid */
    unsigned char  reserved1[2];    /* +2 */
    unsigned char  ipl_sector;      /* +4 */
    unsigned char  ipl_head;        /* +5 */
    unsigned short ipl_cyl;         /* +6 */
    unsigned char  start_sector;    /* +8 */
    unsigned char  start_head;      /* +9 */
    unsigned short start_cyl;       /* +10 */
    unsigned char  end_sector;      /* +12 */
    unsigned char  end_head;        /* +13 */
    unsigned short end_cyl;         /* +14 */
    char           name[16];        /* +16 */
} PC98PartEntry;

/* ---- 戻り値 ------------------------------------------------------------- */
#define PC98PT_OK             0
#define PC98PT_ERR_ARG      (-1)   /* NULL / 添字の範囲外 */
#define PC98PT_ERR_GEOM     (-2)   /* ヘッド・セクタが 0 か 255 超 */
#define PC98PT_ERR_NOTFOUND (-3)   /* OS32 の項目が無い */
#define PC98PT_ERR_RANGE    (-4)   /* 開始 >= 終わり / 開始の CHS が幾何の外 /
                                    * 終わりがディスクの外 */
#define PC98PT_ERR_ALIGN    (-5)   /* (書き手) 開始か長さがシリンダ境界に無い */
#define PC98PT_ERR_CYL      (-6)   /* (書き手) シリンダが 16 ビットに収まらない */

/* 項目 idx (0〜15) を 512 B の表 sect から取り出す / 書き込む。 */
int  pc98pt_get(const unsigned char *sect, int idx, PC98PartEntry *out);
int  pc98pt_put(unsigned char *sect, int idx, const PC98PartEntry *in);

/* 項目が空か (mid と sid が両方 0。fatfs の走査の終わりと同じ判定)。 */
int  pc98pt_entry_empty(const PC98PartEntry *e);

/* 表に空でない項目がいくつあるか。 */
int  pc98pt_count_used(const unsigned char *sect);

/* CHS → LBA。sect は 0 始まり。head >= heads / sect >= spt は RANGE。 */
int  pc98pt_chs_to_lba(unsigned long cyl, unsigned long head, unsigned long sect,
                       unsigned long heads, unsigned long spt,
                       unsigned long *out_lba);

/* 項目の範囲 (開始 LBA、長さ)。disk_total = 0 ならディスク側の上限は見ない
 * (ローダ: 総数を知らない)。長さ 0 や終わりがディスクの外は RANGE。 */
int  pc98pt_entry_range(const PC98PartEntry *e,
                        unsigned long heads, unsigned long spt,
                        unsigned long disk_total,
                        unsigned long *out_start, unsigned long *out_len);

/* OS32 の区画 (sid = PC98PT_SID_OS32 の最初の項目) を探して範囲を返す。
 * out_idx は NULL 可。見つからなければ NOTFOUND、範囲が壊れていれば RANGE。 */
int  pc98pt_find_os32(const unsigned char *sect,
                      unsigned long heads, unsigned long spt,
                      unsigned long disk_total,
                      int *out_idx,
                      unsigned long *out_start, unsigned long *out_len);

/* 2026-09-23 までの OS32 の**旧配置** (+6/+7/+8-9 開始、+10/+11/+12-13 終了) の
 * OS32 項目があるか。sid 0xE2 の最初の項目が**標準配置では読めず**、旧配置では
 * 範囲として成り立つとき 1。カーネルが「移行が要る」と案内するためだけに使う
 * (旧配置を**読む**互換はしない、票 段 1-4)。 */
int  pc98pt_os32_is_legacy(const unsigned char *sect,
                           unsigned long heads, unsigned long spt,
                           unsigned long disk_total);

/* 書き手: [start, start+len) を覆う OS32 の項目を作る。start と len は
 * シリンダ (heads × spt) の倍数、終了シリンダは 16 ビット以内。
 * IPL アドレスは開始と同じ。名前は "OS32" + 空白。 */
int  pc98pt_make_os32(PC98PartEntry *e, unsigned long start, unsigned long len,
                      unsigned long heads, unsigned long spt);

#endif /* PC98PT_H */
