/* ext2_fmt.c から同じ翻訳単位に取り込む。 */
static int fmt_write_groups(Ext2Ctx *ctx, const Ext2Layout *lay)
{
    u32 g;
    int ret;
    GroupLayout gl;
    u32 total_free_blocks = 0;

    /* まずGDTブロックをゼロクリアして書き込み準備 */
    ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);

    /* 各グループのレイアウトを計算してGDTに書き込み */
    for (g = 0; g < lay->num_groups; g++) {
        u32 gd_offset = (g * EXT2L_GD_SIZE) % EXT2_BLOCK_SIZE;
        u32 overhead;

        /* このグループのブロック範囲 */
        gl.group_start = 1 + g * EXT2_BLOCKS_PER_GROUP_MAX; /* first_data_block=1 */
        gl.blocks_in_group = EXT2_BLOCKS_PER_GROUP_MAX;
        if (g == lay->num_groups - 1) {
            /* 最終グループ: 残りブロック */
            gl.blocks_in_group = lay->total_blocks - gl.group_start;
        }
        gl.inodes_in_group = lay->inodes_per_group;
        gl.inode_tbl_blocks = lay->itable_blocks;

        if (is_sparse_group(g)) {
            /* スパースグループ: SB(1) + GDT(gdt_blocks) + bmap + imap + itable */
            gl.block_bitmap = gl.group_start + 1 + lay->gdt_blocks;
            gl.inode_bitmap = gl.block_bitmap + 1;
            gl.inode_table  = gl.inode_bitmap + 1;
            gl.data_start   = gl.inode_table + gl.inode_tbl_blocks;
            overhead = gl.data_start - gl.group_start;
        } else {
            /* 非スパースグループ: bmap + imap + itable のみ */
            gl.block_bitmap = gl.group_start;
            gl.inode_bitmap = gl.block_bitmap + 1;
            gl.inode_table  = gl.inode_bitmap + 1;
            gl.data_start   = gl.inode_table + gl.inode_tbl_blocks;
            overhead = gl.data_start - gl.group_start;
        }

        /* GDTブロックの該当オフセットに書き込み */
        if (gd_offset == 0 && g > 0) {
            /* 新しいGDTブロックの先頭 → 前ブロックを書き込み済み → ゼロクリア */
            ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
        }

        le32_wr(&ext2_g_blk[gd_offset + EXT2_GD_BLOCK_BITMAP_OFF], gl.block_bitmap);
        le32_wr(&ext2_g_blk[gd_offset + EXT2_GD_INODE_BITMAP_OFF], gl.inode_bitmap);
        le32_wr(&ext2_g_blk[gd_offset + EXT2_GD_INODE_TABLE_OFF], gl.inode_table);
        /* free_blocks, free_inodes は後で設定 */
        /* ルートディレクトリはグループ0のデータブロック1つを使用 */
        if (g == 0) {
            le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_FREE_BLOCKS_OFF], (u16)(gl.blocks_in_group - overhead - 1));
            le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_FREE_INODES_OFF], (u16)(gl.inodes_in_group - EXT2_RESERVED_INODES));
            le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_USED_DIRS_OFF], 1); /* used_dirs (root) */
            total_free_blocks += gl.blocks_in_group - overhead - 1;
        } else {
            le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_FREE_BLOCKS_OFF], (u16)(gl.blocks_in_group - overhead));
            le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_FREE_INODES_OFF], (u16)gl.inodes_in_group);
            le16_wr(&ext2_g_blk[gd_offset + EXT2_GD_USED_DIRS_OFF], 0);
            total_free_blocks += gl.blocks_in_group - overhead;
        }

        /* GDTブロック末尾 or 最後のグループ → 書き込み */
        if (gd_offset + EXT2L_GD_SIZE >= EXT2_BLOCK_SIZE || g == lay->num_groups - 1) {
            u32 gd_block = EXT2_GDT_FIRST_BLOCK + (g * EXT2L_GD_SIZE) / EXT2_BLOCK_SIZE;
            ret = ext2_write_block(ctx, gd_block, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;
        }
    }

    /* スーパーブロックのfree_blocks_countを更新 */
    ret = ext2_read_block(ctx, 1, ext2_g_blk);
    if (ret != 0) return EXT2_ERR_IO;
    le32_wr(&ext2_g_blk[EXT2_SB_FREE_BLOCKS_COUNT_OFF], total_free_blocks);
    ret = ext2_write_block(ctx, 1, ext2_g_blk);
    if (ret != 0) return EXT2_ERR_IO;

    return EXT2_OK;
}
