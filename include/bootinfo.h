/* ======================================================================== */
/*  BOOTINFO.H — ブート情報域 (ローダ → kernel_main) の形式                  */
/*                                                                          */
/*  票: docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 0 / §1-v3 N1・N2            */
/*                                                                          */
/*  ローダ (FD: boot/loader_fat_new.asm、HDD: boot/loader_hdd.asm) が        */
/*  実モードで INT 1Bh AH=84h (新センス) を呼び、BIOS が見せる HDD の幾何を   */
/*  物理 MEM_BOOTINFO_BASE (0x7E00) に書く。カーネルは kernel_main の最初で   */
/*  写し (kernel/bootinfo.c)、以後はその写しだけを使う — 0x7E00 は           */
/*  フォントキャッシュ (0x1000〜) の内側で、フォント初期化で上書きされる。    */
/*                                                                          */
/*  **書く順序 (ローダ)**                                                   */
/*    1. 域全体 (BOOTINFO_WIRE_SIZE) を 0 で埋める (= magic 0 = 無効)       */
/*    2. version / source / ndrives と各ドライブの記録を書く                */
/*    3. sum (ドライブ記録のバイト和) を書く                                */
/*    4. magic を書く                                                       */
/*    5. **最後に** check (= ~(magic ^ version)) を書く                     */
/*  途中で止まった域は magic か check が合わないので無効に見える。          */
/*  前回の起動の残りを受け入れる経路は無い (1 で必ず消す、N2)。             */
/*                                                                          */
/*  **NASM 側の写しは boot/bootinfo.inc**。ASM は C のヘッダを読めないので  */
/*  値を二重に持つ。一致は tools/tests/test_bootinfo.py が名前ごとに照合し、 */
/*  番地 (MEM_BOOTINFO_BASE) は tools/gen_memmap.py --check が照合する。     */
/*                                                                          */
/*  i386 では下の struct bootinfo_wire が同じ並びになる (kernel/bootinfo.c   */
/*  の STATIC_ASSERT)。検証はホストでも回すので、バイト列を**オフセットで**  */
/*  読む (ホストの u32 は 64bit)。                                          */
/* ======================================================================== */

#ifndef BOOTINFO_H
#define BOOTINFO_H

#include "types.h"

/* ---- 識別 ---------------------------------------------------------------- */
#define BOOTINFO_MAGIC        0x49544F42UL  /* 'B','O','T','I' (LE) */
#define BOOTINFO_VERSION      2U            /* v2 = イメージ欄 (0x30〜0x3F) を足した */
#define BOOTINFO_CHECK        ((~(BOOTINFO_MAGIC ^ (u32)BOOTINFO_VERSION)) & 0xFFFFFFFFUL)
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

/* ---- カーネルが保存する写し (kernel/bootinfo.c) --------------------------- */
struct bootinfo_drive {
    u8  da;
    u8  queried;
    u8  loader_valid;   /* ローダが書いた valid */
    u8  valid;          /* カーネルが規則で判定し直した結果 */
    u8  cf;
    u8  ah;
    u16 seclen;         /* BX */
    u16 cyl;            /* CX */
    u8  heads;          /* DH */
    u8  spt;            /* DL */
};

struct bootinfo {
    int status;         /* BOOTINFO_OK か BOOTINFO_ERR_* */
    u32 magic;          /* 見えた magic (無効時の表示用) */
    u16 version;
    u8  source;
    u8  ndrives;
    struct bootinfo_drive drive[BOOTINFO_NDRIVES];
    u8  img_valid;      /* 1 = ローダが検査済みイメージの CRC を記録した */
    u32 img_crc;        /* img_valid のときだけ意味がある */
    u32 img_size;
};

/* bootinfo_parse の結果 */
#define BOOTINFO_OK            0
#define BOOTINFO_ERR_ARG      (-1)   /* NULL / 長さ不足 */
#define BOOTINFO_ERR_MAGIC    (-2)   /* magic が違う (ローダが書いていない) */
#define BOOTINFO_ERR_CHECK    (-3)   /* 反転チェック語が違う (書きかけ・壊れ) */
#define BOOTINFO_ERR_VERSION  (-4)   /* 知らない版 */
#define BOOTINFO_ERR_SUM      (-5)   /* ドライブ記録の和が違う */
#define BOOTINFO_ERR_NDRIVES  (-6)   /* ドライブ数が違う */
#define BOOTINFO_ERR_NONE     (-7)   /* まだ写していない */

/* ---- 純粋関数 (kernel/bootinfo_check.c、ホスト試験の対象) ----------------- */

/* イメージ欄のチェック語 (ローダと同じ計算)。 */
#define BOOTINFO_IMG_CHECK_OF(crc, size) \
    ((u32)(((u32)(crc) ^ (u32)(size) ^ (u32)BOOTINFO_IMG_KEY) & 0xFFFFFFFFUL))

/* バイト列 raw[0..len) を検証して out を埋める。戻り値 BOOTINFO_OK / ERR_*。
 * 失敗しても out->status と out->magic は埋める (全ドライブ valid = 0)。 */
int bootinfo_parse(const u8 *raw, unsigned int len, struct bootinfo *out);

/* 1 ドライブの記録が「使える」か (上の規則)。 */
int bootinfo_drive_usable(const struct bootinfo_drive *d);

/* ドライブ記録 (0x08〜0x27) のバイト和。ローダと同じ計算。 */
u16 bootinfo_sum(const u8 *raw);

/* 試験用: version〜ドライブ記録が書かれた raw に sum / magic / check を
 * ローダと同じ順で書く。 */
void bootinfo_seal(u8 *raw);

/* 試験用: イメージ欄をローダと同じ順 (crc → size → check) で書く。 */
void bootinfo_seal_image(u8 *raw, u32 crc, u32 size);

/* 起動画面の 1 行を作る。buf は BOOTINFO_LINE_MAX 以上。
 *   [hdd] bios da=80 cf=0 ah=00 len=512 C/H/S=16382/16/63 src=fd
 *   [hdd] bios geom: none (magic 00000000)
 * slot は drive[] の添字。戻り値は書いた長さ。 */
#define BOOTINFO_LINE_MAX     96
int bootinfo_format_bios(const struct bootinfo *bi, int slot,
                         char *buf, int size);

/* IDENTIFY の値 (drivers/ide.c の ide_get_geom が埋める) の 1 行。
 *   [hdd] ata def=16382/16/63 cur=16382/16/63(valid) lba=1 total=16514063 */
struct bootinfo_ata {
    u16 def_cyl, def_heads, def_spt;       /* word 1 / 3 / 6 */
    u16 cur_cyl, cur_heads, cur_spt;       /* word 54 / 55 / 56 */
    u8  cur_valid;                         /* word 53 bit0 */
    u8  lba;                               /* word 49 bit9 */
    u32 total;                             /* word 60-61 */
};
int bootinfo_format_ata(const struct bootinfo_ata *a, int drive,
                        char *buf, int size);

/* ---- カーネル側 (kernel/bootinfo.c) --------------------------------------- */

/* kernel_main の最初で 1 回だけ呼ぶ。0x7E00 を写して検証し、
 * 写した後は低位の magic を 0 に戻す (次の起動で残りを読まない)。 */
void bootinfo_capture(void);

/* 保存した結果。capture 前は status = BOOTINFO_ERR_NONE。 */
const struct bootinfo *bootinfo_get(void);

/* BIOS 幾何 (INT 1Bh AH=84h)。da = 0x80 / 0x81。
 * 使えるなら 0、情報域が無効・問い合わせていない・規則に外れるなら負。 */
int bootinfo_hdd_geom(int da, u16 *cyl, u8 *heads, u8 *spt, u16 *seclen);

/* 区画表の CHS → LBA に使う幾何 (票 TASK_HDD_INSTALL 段 1)。
 * IDE ドライブ 0 / 1 は DA 80h / 81h の BIOS 幾何を優先し、無ければ IDENTIFY の
 * 既定 (word 3/6)。戻り値 BOOTINFO_GEOM_BIOS / BOOTINFO_GEOM_IDENTIFY / 負。 */
#define BOOTINFO_DA_HDD0        0x80
#define BOOTINFO_GEOM_BIOS      1
#define BOOTINFO_GEOM_IDENTIFY  2
int bootinfo_part_geom(int ide_drive, u16 *heads, u16 *spt);

/* KAPI hdd_geom_info (v64) の実体。out は HddGeom (os32_kapi_shared.h、32 B)。
 * 0 / -9 (OS32_ERR_INVAL: out が NULL か drive が 0〜3 の外)。 */
int hdd_geom_info(int drive, void *out);

/* 起動画面へ [hdd] 行を出す (ide_init の後)。 */
void bootinfo_report(void);

/* KAPI boot_image_info (v65) の実体。out は BootImageInfo (os32_kapi_shared.h、
 * 40 B)。起動したイメージの CRC (ローダの記録) とカーネルのコミット ID。
 * 0 / OS32_ERR_INVAL (out が NULL)。 */
int boot_image_info(void *out);

/* 起動画面の 2 行 (Build + Commit、Image CRC)。kernel_main が出す。 */
void bootinfo_report_image(void);

#endif /* BOOTINFO_H */
