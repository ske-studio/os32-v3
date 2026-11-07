#ifndef PC98_BOOTINFO_WIRE_H
#define PC98_BOOTINFO_WIRE_H
#include "../../include/types.h"
/* ---- 識別 ---------------------------------------------------------------- */
#define BOOTINFO_MAGIC        0x49544F42UL  /* 'B','O','T','I' (LE) */
#define BOOTINFO_VERSION      2U            /* v2 = イメージ欄 (0x30〜0x3F) を足した */
#define BOOTINFO_CHECK        ((~(BOOTINFO_MAGIC ^ (u32)BOOTINFO_VERSION)) & U32_MAX)
#define BOOTINFO_NDRIVES      2U            /* DA 80h / 81h */

/* 取得元 (どのローダが書いたか) */
#define BOOTINFO_SRC_NONE     0U
#define BOOTINFO_SRC_FD       1U            /* boot/loader_fat_new.asm (2HD / 1.44MB) */
#define BOOTINFO_SRC_HDD      2U            /* boot/loader_hdd.asm */

/* ---- 域全体のオフセット -------------------------------------------------- */
#define BI_OFF_MAGIC          0x00          /* u32 */
#define BI_OFF_VERSION        0x04          /* u16 */
#define BI_OFF_SOURCE         0x06          /* u8  BOOTINFO_SRC_* */
#define BI_OFF_NDRIVES        0x07          /* u8  = BOOTINFO_NDRIVES */
#define BI_OFF_DRIVE0         0x08          /* ドライブ記録 × BOOTINFO_NDRIVES */
#define BI_DRIVE_SIZE         0x10
#define BI_OFF_SUM            0x28          /* u16 ドライブ記録 (0x08〜0x27) のバイト和 */
#define BI_OFF_RSVD           0x2A          /* u16 0 */
#define BI_OFF_CHECK          0x2C          /* u32 = BOOTINFO_CHECK。**最後に書く** */
/* イメージ欄 (v2、票 TASK_SERIAL_HOSTFS 部品 A-4 / §1-v3「.old の識別」)。
 * ローダが vmkernel.lz4 を**検査し終えてから**書く (主部の封印より後 —
 * HDD ローダは PM の boot_main、FD ローダは PM の pm_entry32)。主部の sum /
 * check には含めず、自分のチェック語 img_check を**最後に**書く:
 *   img_check = img_crc ^ img_size ^ BOOTINFO_IMG_KEY
 * 主部が有効で img_check が合い img_size != 0 のときだけ「記録あり」。
 * **写し**: boot/bootinfo.inc (NASM) と boot/boot_defs.h (HDD ローダの C)。 */
#define BI_OFF_IMG_CRC        0x30          /* u32 VK32 ファイル全体の CRC32 (image_crc) */
#define BI_OFF_IMG_SIZE       0x34          /* u32 読んだファイルの長さ */
#define BI_OFF_IMG_CHECK      0x38          /* u32 **最後に書く** */
#define BI_OFF_IMG_RSVD       0x3C          /* u32 0 */
#define BOOTINFO_IMG_KEY      0x43474D49UL  /* 'I','M','G','C' (LE) */
#define BOOTINFO_WIRE_SIZE    0x40

/* ---- ドライブ記録 (BI_OFF_DRIVE0 + i * BI_DRIVE_SIZE) 内のオフセット ----- */
/* INT 1Bh AH=84h の戻り (PC-9800 Bible 2-9 §3 SENSE [HDD]):               */
/*   CF = 1 で失敗 (AH = 状態)、BX = セクタ長、CX = シリンダ数、            */
/*   DH = ヘッド数、DL = セクタ数/トラック                                   */
#define BI_DRV_DA             0x00          /* u8  問い合わせた DA/UA (80h / 81h) */
#define BI_DRV_VALID          0x01          /* u8  ローダの判定 1 = 使える */
#define BI_DRV_CF             0x02          /* u8  INT 1Bh 後の CF (0/1) */
#define BI_DRV_AH             0x03          /* u8  INT 1Bh 後の AH */
#define BI_DRV_BX             0x04          /* u16 セクタ長 [B] */
#define BI_DRV_CX             0x06          /* u16 シリンダ数 */
#define BI_DRV_DH             0x08          /* u8  ヘッド数 */
#define BI_DRV_DL             0x09          /* u8  セクタ数/トラック */
#define BI_DRV_QUERIED        0x0A          /* u8  1 = このローダが INT 1Bh を呼んだ */
/* 0x0B〜0x0F は 0 */

/* 幾何を「使える」とみなす条件 (ローダとカーネルの両方が同じ規則で見る):
 *   queried = 1、CF = 0、BX = 512、CX != 0、DH != 0、DL != 0、DA が 80h か 81h
 * (カーネルはさらにローダの valid = 1 も要求する) */
#define BOOTINFO_SECLEN       512U
#define BOOTINFO_DA_HD0       0x80U
#define BOOTINFO_DA_HD1       0x81U

/* ---- i386 での並び (説明と STATIC_ASSERT 用) ------------------------------ */
struct bootinfo_wire_drive {
    u8  da;
    u8  valid;
    u8  cf;
    u8  ah;
    u16 bx;
    u16 cx;
    u8  dh;
    u8  dl;
    u8  queried;
    u8  rsvd[5];
};

struct bootinfo_wire {
    u32 magic;
    u16 version;
    u8  source;
    u8  ndrives;
    struct bootinfo_wire_drive drive[BOOTINFO_NDRIVES];
    u16 sum;
    u16 rsvd;
    u32 check;
    u32 img_crc;
    u32 img_size;
    u32 img_check;
    u32 img_rsvd;
};


#endif
