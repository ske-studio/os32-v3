/* ext2_fmt.c から同じ翻訳単位に取り込む。 */
static int fmt_write_backups(const FormatIO *ctx, const Ext2Layout *lay)
{
    u32 g, i;
    int ret;

    /* ===== スパースグループへのSBバックアップ + GDTコピー ===== */
    for (g = 1; g < lay->num_groups; g++) {
        u32 gs;
        if (!is_sparse_group(g)) continue;
        gs = 1 + g * EXT2_BLOCKS_PER_GROUP_MAX;

        /* SBバックアップ: プライマリSBを読み、s_block_group_nrを変更 */
        ret = fmt_rd(ctx, 1, fmt_blk);
        if (ret != 0) return EXT2_ERR_IO;
        le16_wr(&fmt_blk[EXT2_SB_BLOCK_GROUP_NR_OFF], (u16)g);           /* s_block_group_nr = g */
        ret = fmt_wr(ctx, gs, fmt_blk);
        if (ret != 0) return EXT2_ERR_IO;

        /* GDTコピー: プライマリGDTを各ブロックごとにコピー */
        for (i = 0; i < lay->gdt_blocks; i++) {
            ret = fmt_rd(ctx, EXT2_GDT_FIRST_BLOCK + i, fmt_blk);
            if (ret != 0) return EXT2_ERR_IO;
            ret = fmt_wr(ctx, gs + 1 + i, fmt_blk);
            if (ret != 0) return EXT2_ERR_IO;
        }
    }

    return EXT2_OK;
}
