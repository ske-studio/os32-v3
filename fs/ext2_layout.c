/* ======================================================================== */
/*  EXT2_LAYOUT.C — ext2 の一括フォーマットの配置 (固定点、純粋関数)         */
/*                                                                          */
/*  規則は ext2_layout.h。fs/ext2_fmt.c はここで決まった値だけを書く。       */
/* ======================================================================== */

#include "ext2_layout.h"

static int is_power_of(u32 n, u32 base)
{
    u32 v = base;
    while (v < n) v *= base;
    return (v == n) ? 1 : 0;
}

int ext2_layout_is_sparse(u32 g)
{
    if (g <= 1) return 1;
    return (is_power_of(g, 3) || is_power_of(g, 5) || is_power_of(g, 7)) ? 1 : 0;
}

u32 ext2_layout_group_overhead(const Ext2Layout *l, u32 g)
{
    u32 n = 2UL + l->itable_blocks;               /* bitmap 2 + inode 表 */
    if (ext2_layout_is_sparse(g)) n += 1UL + l->gdt_blocks;   /* SB + GDT */
    return n;
}

/* total_blocks から残りの値を決める (旧 ext2_format と同じ式) */
static int layout_fill(u32 total_blocks, u32 max_groups, Ext2Layout *l)
{
    u32 inodes, last;

    if (total_blocks < EXT2L_MIN_BLOCKS) return EXT2L_ERR_SMALL;

    l->total_blocks = total_blocks;
    l->num_groups = (total_blocks - EXT2L_FIRST_DATA_BLOCK
                     + EXT2L_BLOCKS_PER_GROUP - 1UL) / EXT2L_BLOCKS_PER_GROUP;
    if (l->num_groups == 0) l->num_groups = 1;
    if (l->num_groups > max_groups) return EXT2L_ERR_GROUPS;

    inodes = total_blocks / EXT2L_BLOCKS_PER_INODE;
    if (inodes < EXT2L_MIN_INODES) inodes = EXT2L_MIN_INODES;
    l->inodes_per_group = (inodes + l->num_groups - 1UL) / l->num_groups;
    l->inodes_per_group = (l->inodes_per_group + 7UL) & ~7UL;
    l->inodes_count = l->inodes_per_group * l->num_groups;
    l->itable_blocks = (l->inodes_per_group * EXT2L_INODE_SIZE
                        + EXT2L_BLOCK_SIZE - 1UL) / EXT2L_BLOCK_SIZE;
    l->gdt_blocks = (l->num_groups * EXT2L_GD_SIZE + EXT2L_BLOCK_SIZE - 1UL)
                    / EXT2L_BLOCK_SIZE;

    last = l->num_groups - 1UL;
    l->last_group_blocks = total_blocks
                           - (EXT2L_FIRST_DATA_BLOCK + last * EXT2L_BLOCKS_PER_GROUP);
    l->last_group_need = ext2_layout_group_overhead(l, last);
    if (last == 0) l->last_group_need += EXT2L_ROOT_DATA_BLOCKS;
    return EXT2L_OK;
}

int ext2_layout_plan(u32 sectors, u32 max_groups, Ext2Layout *out)
{
    Ext2Layout l;
    u32 tb;
    int rc, iter;

    if (!out) return EXT2L_ERR_SMALL;
    tb = sectors / 2UL;                           /* 512 B → 1 KB */

    for (iter = 0; iter < EXT2L_MAX_ITER; iter++) {
        rc = layout_fill(tb, max_groups, &l);
        if (rc != EXT2L_OK) return rc;
        if (l.last_group_blocks >= l.last_group_need) {
            *out = l;
            return EXT2L_OK;
        }
        /* 最終グループに全メタデータが入らない → そのグループを落とす。
         * グループ数・inode 数が変わるので、次の周で全部を計算し直す。 */
        if (l.num_groups <= 1) return EXT2L_ERR_SMALL;
        tb = EXT2L_FIRST_DATA_BLOCK
             + (l.num_groups - 1UL) * EXT2L_BLOCKS_PER_GROUP;
    }
    return EXT2L_ERR_SMALL;
}
