/* ======================================================================== */
/*  EXT2_LAYOUT.H — ext2 の一括フォーマットの配置計算 (純粋関数)             */
/*                                                                          */
/*  票 TASK_HDD_INSTALL §1-v3 N7 (F11 / F12)。以前の ext2_format は          */
/*  **最終グループがメタデータより小さくても断らず**区画の外へ inode 表を    */
/*  書いた (例: 16,652 セクタ = 8,326 ブロックでは最終グループ 133 ブロックに */
/*  SB + GDT + bitmap 2 + inode 表 131 = 135 ブロックが要る)。               */
/*                                                                          */
/*  ここでは「与えられた長さの中に全メタデータが収まる」大きさを固定点で     */
/*  求める: 最終グループが足りなければそのグループを丸ごと落とし、グループ数・*/
/*  inode 数を計算し直して再判定する。成立しなければ断る。                   */
/*                                                                          */
/*  必要量 (最終グループ g):                                                 */
/*    sparse (0, 1, 3^n, 5^n, 7^n) なら SB 1 + GDT                           */
/*    + block bitmap 1 + inode bitmap 1 + inode 表                           */
/*    + g = 0 ならルートディレクトリのデータ 1 (fs/ext2_fmt.c が作るのは       */
/*      ルートだけ。lost+found は作らない — e2fsck -n は「無い」を報告する)   */
/*  上限は EXT2_MAX_GROUPS (32 グループ = 256MiB)。                          */
/*                                                                          */
/*  試験: tools/tests/test_hdd_stage1.py (記録 tools/tests/hdd_stage1_tdd.md) */
/* ======================================================================== */

#ifndef EXT2_LAYOUT_H
#define EXT2_LAYOUT_H

#include "types.h"

/* 1KB ブロック、1 グループ 8192 ブロック、inode 128 B、4 ブロックに 1 inode */
#define EXT2L_BLOCK_SIZE        1024UL
#define EXT2L_BLOCKS_PER_GROUP  8192UL
#define EXT2L_INODE_SIZE        128UL
#define EXT2L_BLOCKS_PER_INODE  4UL
#define EXT2L_MIN_INODES        16UL
#define EXT2L_MIN_BLOCKS        64UL    /* 旧 ext2_format と同じ下限 */
#define EXT2L_GD_SIZE           32UL
#define EXT2L_FIRST_DATA_BLOCK  1UL     /* 1KB ブロックでは 1 */
#define EXT2L_ROOT_DATA_BLOCKS  1UL     /* グループ 0 のルートのデータ */
/* 固定点の繰り返しの上限 (1 回落とすごとにグループが 1 つ減るので、
 * グループ数 + 1 回で必ず止まる。念のための上限) */
#define EXT2L_MAX_ITER          40

/* g グループに収まる最大の長さ (セクタ)。ブロック 0 + g × 8192 ブロック */
#define EXT2L_MAX_SECTORS(g)    ((EXT2L_FIRST_DATA_BLOCK + (u32)(g) * EXT2L_BLOCKS_PER_GROUP) * 2UL)

#define EXT2L_OK                0
#define EXT2L_ERR_SMALL       (-1)   /* 収まる大きさが無い (64 ブロック未満) */
#define EXT2L_ERR_GROUPS      (-2)   /* グループ上限を超える */

typedef struct {
    u32 total_blocks;       /* ファイルシステムのブロック数 (≤ 長さ / 2) */
    u32 num_groups;
    u32 inodes_per_group;   /* 8 の倍数 */
    u32 inodes_count;       /* inodes_per_group × num_groups */
    u32 itable_blocks;      /* 1 グループの inode 表のブロック数 */
    u32 gdt_blocks;         /* グループ記述子表のブロック数 */
    u32 last_group_blocks;  /* 最終グループのブロック数 */
    u32 last_group_need;    /* 最終グループの必要量 (上の式) */
} Ext2Layout;

/* g はスパースグループ (SB と GDT の写しを持つ) か */
int ext2_layout_is_sparse(u32 g);

/* グループ g のメタデータのブロック数 (ルートのデータは含まない) */
u32 ext2_layout_group_overhead(const Ext2Layout *l, u32 g);

/* sectors (512 B) の範囲に収まる配置を求める。max_groups は上限
 * (通常 EXT2_MAX_GROUPS)。戻り値 EXT2L_OK / EXT2L_ERR_*。 */
int ext2_layout_plan(u32 sectors, u32 max_groups, Ext2Layout *out);

#endif /* EXT2_LAYOUT_H */
