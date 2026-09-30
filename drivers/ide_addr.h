/* ======================================================================== */
/*  IDE_ADDR.H — ATA のセクタ指定 (LBA28 / 現在の CHS / 既定の CHS) の選択    */
/*                                                                          */
/*  票 TASK_HDD_INSTALL §1-v3「ATA 側の変換」(F8 / F13)。                     */
/*  以前の drivers/ide.c と drivers/dev.c は IDENTIFY の**既定**幾何          */
/*  (word 1/3/6) で LBA → CHS にしていた。BIOS が INITIALIZE DEVICE PARAMETERS */
/*  でドライブの**現在の変換**を変えていると、既定での CHS は別の物理セクタを  */
/*  指す。そこで指定の方式を IDENTIFY の申告から 1 つに決める:                 */
/*                                                                          */
/*    1. word 49 bit9 (LBA 対応)            → LBA28 (DRV/HEAD bit6 = 1)       */
/*    2. word 53 bit0 と現在の幾何 (54-56)  → 現在の CHS                     */
/*    3. どちらも無い                       → 既定の CHS (word 1/3/6。0 なら  */
/*                                            従来の 8/17)。**書き込みの成否を */
/*                                            保証できない** — hdprep は断る   */
/*                                                                          */
/*  UNDOCUMENTED io_ide.md は「PC-9800 では LBA を使わない」と書くが、それは   */
/*  BIOS (INT 1Bh) の話で、ドライブ自身は word 49 で LBA を申告する。NP21/W   */
/*  も申告して LBA で読む (np21w-src/src/cbus/ideio.c の getcursec)。          */
/*                                                                          */
/*  **範囲検査はここで全部やる**: LBA28 の上限 (2^28)、総セクタ数、CHS の     */
/*  シリンダ 16 ビット、count を足したときの桁あふれ。`dd hd0 lba=268435456`  */
/*  が LBA 0 に化けない。32 ビットの桁あふれに頼る計算はしない (ホスト試験は  */
/*  64 ビットで回る)。純粋関数だけ — 試験は tools/tests/test_hdd_stage1.py。   */
/* ======================================================================== */

#ifndef IDE_ADDR_H
#define IDE_ADDR_H

#include "types.h"
#include "ide.h"      /* IdeGeom */

/* 方式 */
#define IDE_AMODE_NONE      0   /* ドライブが無い / 幾何が使えない */
#define IDE_AMODE_LBA28     1
#define IDE_AMODE_CHS_CUR   2
#define IDE_AMODE_CHS_DEF   3

/* LBA28 で指せるセクタ数 (0 〜 0x0FFFFFFF) */
#define IDE_LBA28_LIMIT     0x10000000UL
/* ATA の DRV/HEAD のヘッド欄は 4 ビット */
#define IDE_CHS_MAX_HEADS   16
#define IDE_CHS_MAX_CYL     0xFFFFUL
/* 既定の幾何が 0 のときの従来の値 (旧 ide_lba_to_chs と同じ) */
#define IDE_FALLBACK_HEADS  8
#define IDE_FALLBACK_SPT    17

/* DRV/HEAD レジスタ */
#define IDE_DRVHEAD_BASE    0xA0   /* bit7 / bit5 = 1 */
#define IDE_DRVHEAD_LBA     0x40   /* bit6 = LBA */
#define IDE_DRVHEAD_SLAVE   0x10   /* bit4 = スレーブ */

/* 1 セクタ分の指定 (レジスタに書く値) */
typedef struct {
    u8  mode;       /* IDE_AMODE_* */
    u8  sect_num;   /* 0x646: LBA[7:0] / セクタ (1 始まり) */
    u8  cyl_lo;     /* 0x648: LBA[15:8] / シリンダ下位 */
    u8  cyl_hi;     /* 0x64A: LBA[23:16] / シリンダ上位 */
    u8  drv_head;   /* 0x64C: 基本値 | LBA | スレーブ | LBA[27:24] / ヘッド */
} IdeAddr;

/* 方式と、その方式の幾何 (CHS のときだけ意味がある) と上限セクタ数を決める。
 * g が NULL か valid = 0 なら NONE。out_* は NULL 可。 */
int ide_addr_mode(const IdeGeom *g, u16 *out_heads, u16 *out_spt,
                  u32 *out_limit);

/* [lba, lba+count) が方式の範囲に収まるか。count = 0 は常に可 (何もしない)。
 * 戻り値 1 = 収まる / 0 = 収まらない (桁あふれを含む)。 */
int ide_addr_range_ok(const IdeGeom *g, u32 lba, u32 count);

/* 1 セクタ lba のレジスタ値を作る。drive は 0-3 (奇数 = スレーブ)。
 * 戻り値 IDE_OK / IDE_ERR_NO_DRIVE (NONE) / IDE_ERR_RANGE (範囲外)。 */
int ide_addr_make(const IdeGeom *g, int drive, u32 lba, IdeAddr *out);

#endif /* IDE_ADDR_H */
