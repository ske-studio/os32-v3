/* ======================================================================== */
/*  INST_DISK.H — インストーラ (cdinst / install) の判定 (純粋関数)          */
/*                                                                          */
/*  票 docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 2 (9〜11) / §1-v3 N4・N8。   */
/*  CD の cdinst と FD の install は**同じ規則**で hd0 (= DA 80h) を扱う:     */
/*                                                                          */
/*    幾何と区画   hdprep と同じ (userland/shell/hdprep_plan.c)。区画表と    */
/*                 IPL の [8]/[9] は BIOS 幾何 (INT 1Bh AH=84h)、開始は      */
/*                 LBA 1632 以上の最初の BIOS シリンダ境界 (8/17 → 1632、    */
/*                 16/63 → 2016)、長さは 256MiB (ext2 32 グループ) まで。    */
/*    モード       空のディスク (区画項目 0、LBA 0 に 55AA 無し) か、        */
/*                 **再作成** (項目がちょうど 1 つで、OS32 が作ったもの =   */
/*                 sid 0xE2・名前 "OS32"・開始 = 期待値)。旧配置 (2026-09-23 */
/*                 までの cdinst / install が書いた +6/+7/+8-9) の OS32 項目も */
/*                 開始が期待値なら再作成の対象 (8/17 の NHD を入れ直すため)。 */
/*                 それ以外 (未知の区画・2 つ以上・開始が違う・壊れた項目)  */
/*                 は断る。                                                 */
/*    媒体         IPL 1〜512 B、ローダ 1〜8192 B (IPL が LBA 2 から 16      */
/*                 セクタ読む)、vmkernel.lz4 1〜508KiB (boot/boot_defs.h    */
/*                 MAX_IMAGE_SIZE)、展開先の容量 (ブロックと inode)。        */
/*                                                                          */
/*  I/O は持たない。書く手順は inst_hdd.c。                                  */
/*  試験: tools/tests/test_hdd_stage2.py (記録 tools/tests/hdd_stage2_tdd.md) */
/*  型は C の素の型だけ (os32api.h と include/types.h のどちらとも組める)。  */
/* ======================================================================== */

#ifndef INST_DISK_H
#define INST_DISK_H

#include "userland/shell/hdprep_plan.h"   /* HdprepGeom / HdprepPlan / HDPREP_E_* */

/* ---- 媒体の上限 ---------------------------------------------------------- */
#define INST_IPL_MAX        512UL
#define INST_LOADER_LBA     2UL
/* boot/boot_hdd.asm は LBA 2 から 16 セクタ読む (tools/nhd_deploy.py の
 * LOADER_MAX_SECTORS と同じ) */
#define INST_LOADER_MAX     8192UL
/* boot/boot_defs.h MAX_IMAGE_SIZE (0x10000〜0x8EFFF)。試験が突き合わせる */
#define INST_KERNEL_MAX     (508UL * 1024UL)
/* IPL の中の幾何 (boot/boot_hdd.asm の geo_heads / geo_spt) */
#define INST_IPL_OFF_HEADS  8
#define INST_IPL_OFF_SPT    9
/* 区画の大きさ (hdprep の上限と同じ 256MiB = ext2 32 グループ) */
#define INST_PART_MB        HDPREP_MAX_MB
/* fs/ext2_ctx.h EXT2_MAX_GROUPS。試験が突き合わせる */
#define INST_EXT2_MAX_GROUPS 32UL
/* 展開の後にも要る余白 (settings.db の更新・/tmp など、ブロック = 1KiB) */
#define INST_SPACE_MARGIN_BLOCKS 256UL
/* ext2 の予約 inode (1〜10。ルートは 2 で予約の内側) */
#define INST_EXT2_RESERVED_INODES 10UL
/* 展開が自動で作る親ディレクトリ (項目表に無いもの) の分の inode の余白 */
#define INST_SPACE_MARGIN_INODES 128UL
/* ext2 (1KiB ブロック) で OS32 が書ける 1 ファイルの上限: 直接 12 + 単一間接
 * 256 + 二重間接 256 × 256 ブロック = 67,383,296 B (三重間接は書かない) */
#define INST_EXT2_MAX_FILE  ((12UL + 256UL + 256UL * 256UL) * 1024UL)
/* fs/vfs.h VFS_MAX_PATH_DEPTH。展開先の前置 "/hd0" の 1 要素を足して収まること */
#define INST_VFS_MAX_DEPTH  32UL
#define INST_PREFIX_DEPTH   1UL

/* ---- モード --------------------------------------------------------------- */
#define INST_MODE_EMPTY         1   /* 区画項目が無い */
#define INST_MODE_RECREATE      2   /* OS32 の項目 1 つ (標準配置) を作り直す */
#define INST_MODE_RECREATE_OLD  3   /* OS32 の項目 1 つ (旧配置) を作り直す */

/* ---- 断る理由 (HDPREP_E_* と重ならない値) --------------------------------- */
#define INST_E_FOREIGN      (-40)   /* 項目が 1 つだが OS32 の物ではない */
#define INST_E_MULTI        (-41)   /* 項目が 2 つ以上 */
#define INST_E_START        (-42)   /* OS32 の項目だが開始が期待値でない */
#define INST_E_BROKEN       (-43)   /* OS32 の項目だがどちらの配置でも読めない */
#define INST_E_IPL_SIZE     (-44)   /* IPL が 0 か 512 B 超 */
#define INST_E_LOADER_SIZE  (-45)   /* ローダが 0 か 8192 B 超 */
#define INST_E_KERNEL_SIZE  (-46)   /* vmkernel.lz4 が 0 か 508KiB 超 */
#define INST_E_SPACE        (-47)   /* 展開先のブロックが足りない */
#define INST_E_INODES       (-48)   /* 展開先の inode が足りない */
#define INST_E_LAYOUT       (-49)   /* 区画に ext2 の配置が成り立たない */
#define INST_E_ARG          (-50)
#define INST_E_PATH         (-51)   /* パスが絶対でない / 空・"."・".." の要素 */
#define INST_E_DEPTH        (-52)   /* "/hd0" を付けると要素が 32 を超える */
#define INST_E_FILE_SIZE    (-53)   /* 1 ファイルが ext2 の上限 (二重間接まで) を超える */
#define INST_E_GEOM         (-54)   /* hdd_geom_info が hd0 の幾何を返さない */

/* 区画表と LBA 0 からモードを決める。heads / spt は BIOS 幾何、disk_total は
 * IDENTIFY の総数、expect_start は計画の開始 (hdprep_plan の start)。
 * 戻り値 0 = *out_mode にモード / 負 = 断る理由 (HDPREP_E_MBR_SIG か INST_E_*)。 */
int inst_classify(const unsigned char *lba0, const unsigned char *lba1,
                  unsigned long heads, unsigned long spt,
                  unsigned long disk_total, unsigned long expect_start,
                  int *out_mode);

/* 媒体の大きさ (バイト)。0 = OK / INST_E_*_SIZE */
int inst_check_boot_files(unsigned long ipl_len, unsigned long loader_len,
                          unsigned long kernel_len);

/* 展開に要る量の見積もり (多めに数える) */
typedef struct {
    unsigned long files;
    unsigned long dirs;
    unsigned long blocks;      /* データ + 間接ブロック + ディレクトリ */
    unsigned long too_big;     /* INST_EXT2_MAX_FILE を超えるファイルの数 */
} InstNeed;

typedef struct {
    unsigned long free_blocks;
    unsigned long free_inodes;
    unsigned long need_blocks; /* 余白込み */
    unsigned long need_inodes;
} InstRoom;

void inst_need_init(InstNeed *n);
void inst_need_file(InstNeed *n, unsigned long size);
void inst_need_dir(InstNeed *n);
/* 1 ファイルが使うブロック数 (データ + 間接) */
unsigned long inst_file_blocks(unsigned long size);

/* part_sectors の区画に ext2 を作ったとき、n が収まるか。out は NULL 可
 * (失敗でも 0 で埋めてから返す)。
 * 0 = 収まる / INST_E_FILE_SIZE / INST_E_SPACE / INST_E_INODES / INST_E_LAYOUT */
int inst_check_space(unsigned long part_sectors, const InstNeed *n, InstRoom *out);

/* 展開するパス (パッケージの項目、"/hd0" を付ける前) の検査。/hd0 の外へ出ない
 * ことを要素ごとに見る: 先頭が '/'、どの要素も空・"."・".." でない、要素数 +
 * INST_PREFIX_DEPTH が INST_VFS_MAX_DEPTH 以下。0 / INST_E_PATH / INST_E_DEPTH */
int inst_check_path(const char *path);

/* IPL (512 B) に BIOS 幾何を書き、末尾に 55AA を置く */
void inst_patch_ipl(unsigned char *ipl, unsigned long heads, unsigned long spt);

/* 区画表 (512 B) を作る: 全部 0 にして項目 0 に OS32 の区画 (標準配置)。
 * 0 = OK / 負 = pc98pt の失敗 (PC98PT_ERR_*) */
int inst_build_pt(unsigned char *sect, const HdprepPlan *p);

/* 表示用 (英語 1 行) */
const char *inst_reason(int code);
const char *inst_mode_name(int mode);

#endif /* INST_DISK_H */
