#ifndef PC98_BOOTINFO_NATIVE_H
#define PC98_BOOTINFO_NATIVE_H
#include "bootinfo_wire.h"
/* ---- カーネルが保存する写し (init/bootinfo.c) --------------------------- */
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

/* Internal adapter contract is defined in kernel/boot_disk.h. */
struct boot_disk_ops;
void bootinfo_disk_register(const struct boot_disk_ops *ops);

/* ---- 純粋関数 (init/bootinfo_check.c、ホスト試験の対象) ----------------- */

/* イメージ欄のチェック語 (ローダと同じ計算)。 */
#define BOOTINFO_IMG_CHECK_OF(crc, size) \
    ((u32)(((u32)(crc) ^ (u32)(size) ^ (u32)BOOTINFO_IMG_KEY) & U32_MAX))

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

/* IDENTIFY の値 (drivers/ata/ide.c の ide_get_geom が埋める) の 1 行。
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

/* ---- カーネル側 (init/bootinfo.c) --------------------------------------- */

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


const struct boot_disk_ops *pc98_boot_disk_get(void);
int boot_image_info(void *out);
void bootinfo_report_image(void);
#endif
