/* ext2_fmt.c から同じ翻訳単位に取り込む。 */
static int fmt_init_groups(Ext2Ctx *ctx, const Ext2Layout *lay)
{
    u32 g, i;
    int ret;
    GroupLayout gl;

    /* ===== 各グループのビットマップとinodeテーブルを初期化 ===== */
    for (g = 0; g < lay->num_groups; g++) {
        u32 overhead, bmap_blk, imap_blk, itable_blk;

        /* レイアウトを再計算 */
        gl.group_start = 1 + g * EXT2_BLOCKS_PER_GROUP_MAX;
        gl.blocks_in_group = EXT2_BLOCKS_PER_GROUP_MAX;
        if (g == lay->num_groups - 1) {
            gl.blocks_in_group = lay->total_blocks - gl.group_start;
        }

        if (is_sparse_group(g)) {
            /* スパースグループ: SB + GDT + bmap + imap + itable */
            bmap_blk   = gl.group_start + 1 + lay->gdt_blocks;
            imap_blk   = bmap_blk + 1;
            itable_blk = imap_blk + 1;
            overhead = (itable_blk + lay->itable_blocks) - gl.group_start;
        } else {
            /* 非スパースグループ: bmap + imap + itable のみ */
            bmap_blk  = gl.group_start;
            imap_blk  = bmap_blk + 1;
            itable_blk = imap_blk + 1;
            overhead = 1 + 1 + lay->itable_blocks;
        }

        /* ブロックビットマップ */
        ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
        /* オーバーヘッドブロックをマーク */
        for (i = 0; i < overhead; i++) {
            ext2_g_blk[i / BITS_PER_BYTE] |= (u8)(1 << (i % BITS_PER_BYTE));
        }
        /* グループ0: ルートディレクトリ用データブロック1つ */
        if (g == 0) {
            ext2_g_blk[overhead / BITS_PER_BYTE] |= (u8)(1 << (overhead % BITS_PER_BYTE));
        }
        /* グループ内のブロック数以降のビットを全て使用不可にマーク
         * (ext2仕様: ビットマップのパディング部分は1) */
        for (i = gl.blocks_in_group; i < EXT2_BLOCKS_PER_GROUP_MAX; i++) {
            ext2_g_blk[i / BITS_PER_BYTE] |= (u8)(1 << (i % BITS_PER_BYTE));
        }
        ret = ext2_write_block(ctx, bmap_blk, ext2_g_blk);
        if (ret != 0) return EXT2_ERR_IO;

        /* inodeビットマップ */
        ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
        if (g == 0) {
            /* inode 1-10は予約 (ビット0-9) */
            for (i = 0; i < EXT2_RESERVED_INODES; i++) {
                ext2_g_blk[i / BITS_PER_BYTE] |= (u8)(1 << (i % BITS_PER_BYTE));
            }
        }
        /* inodes_per_group以降のビットを全て使用不可にマーク
         * (ext2仕様: ビットマップのパディング部分は1) */
        for (i = lay->inodes_per_group; i < EXT2_BLOCKS_PER_GROUP_MAX; i++) {
            ext2_g_blk[i / BITS_PER_BYTE] |= (u8)(1 << (i % BITS_PER_BYTE));
        }
        ret = ext2_write_block(ctx, imap_blk, ext2_g_blk);
        if (ret != 0) return EXT2_ERR_IO;

        /* inodeテーブル (ゼロクリア) */
        ext2_mem_zero(ext2_g_blk, EXT2_BLOCK_SIZE);
        for (i = 0; i < lay->itable_blocks; i++) {
            ret = ext2_write_block(ctx, itable_blk + i, ext2_g_blk);
            if (ret != 0) return EXT2_ERR_IO;
        }
    }

    return EXT2_OK;
}
