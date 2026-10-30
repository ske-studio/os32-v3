/* ext2_fmt.c から同じ翻訳単位に取り込む。 */
static int fmt_write_backups(Ext2Ctx *ctx, const Ext2Layout *lay)
{
    u32 g, i;
    int ret;

    /* ===== スパースグループへのSBバックアップ + GDTコピー ===== */
    for (g = 1; g < lay->num_groups; g++) {
        u32 gs;
        if (!is_sparse_group(g)) continue;
        gs = 1 + g * EXT2_BLOCKS_PER_GROUP_MAX;

        /* SBバックアップ: プライマリSBを読み、s_block_group_nrを変更 */
        ret = ext2_read_block(ctx, 1, ext2_g_blk);
        if (ret != 0) return EXT2_ERR_IO;
        le16_wr(&ext2_g_blk[EXT2_SB_BLOCK_GROUP_NR_OFF], (u16)g);           /* s_block_group_nr = g */
        ret = ext2_write_block(ctx, gs, ext2_g_blk);
        if (ret != 0) return EXT2_ERR_IO;

        /* GDTコピー: プライマリGDTを各ブロックごとにコピー */
        for (i = 0; i < lay->gdt_blocks; i++) {
            ret = ext2_read_block(ctx, EXT2_GDT_FIRST_BLOCK + i, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;
            ret = ext2_write_block(ctx, gs + 1 + i, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;
        }
    }

    return EXT2_OK;
}
