/* ブロック block_num (1KB = 2 セクタ) が区画の中か。part_len は区画の長さ
 * (セクタ)。媒体から読んだブロック番号は壊れていることがあるので、掛け算の
 * 前に割り算の側で比べる (block_num * EXT2_SECTORS_PER_BLOCK の桁あふれで範囲内に化けない)。 */
static int ext2_block_range_lba(const Ext2Ctx *ctx, u32 first, u32 blocks,
                                u32 *sector)
{
    u32 limit = ctx->part_len / EXT2_SECTORS_PER_BLOCK;
    u32 relative, sectors;
    if (blocks == 0 || blocks > EXT2_DATA_RUN_MAX_BLOCKS) return 0;
    if (first >= limit || blocks > limit - first) return 0;
    relative = first * EXT2_SECTORS_PER_BLOCK;
    sectors = blocks * EXT2_SECTORS_PER_BLOCK;
    if (relative > U32_MAX - ctx->base_lba) return 0;
    *sector = ctx->base_lba + relative;
    if (sectors - 1 > U32_MAX - *sector) return 0;
    return 1;
}

/* Mount also uses raw I/O before ctx->mounted becomes true. No allocation or
 * retained copy: Device transfers directly into the caller's destination. */
static int ext2_raw_read_blocks(Ext2Ctx *ctx, u32 first, u32 blocks, void *buf)
{
    u32 sector;
    int ret = EXT2_ERR_IO;
    if (ext2_block_range_lba(ctx, first, blocks, &sector))
        ret = dev_blk_read_lba(ctx->dev, sector, blocks * EXT2_SECTORS_PER_BLOCK, buf);
    if (ret != 0) ext2_read_cache_reset();
    return ret;
}

static int ext2_raw_write_block(Ext2Ctx *ctx, u32 block_num, const void *buf)
{
    u32 sector;
    int ret;

    ext2_read_cache_reset();
    if (!ext2_block_range_lba(ctx, block_num, 1, &sector)) return EXT2_ERR_IO;
    ret = dev_blk_write_lba(ctx->dev, sector, 1, buf);
    if (ret != 0) return ret;
    ret = dev_blk_write_lba(ctx->dev, sector + 1, 1, (const u8 *)buf + EXT2_SECTOR_SIZE);
    return ret;
}

/* **メタデータ**の I/O。失敗したらエラー状態にする (票 B8 往復 5 / 決裁 2)。
 *
 * 既定をメタデータ側に置いた — データブロックの I/O は ext2_file.c の数か所
 * だけで、そこは明示的に _data 版を呼ぶ。逆 (既定をデータ側) にすると、
 * メタデータの呼び手を 1 か所見落としただけでエラー状態が立たなくなる。 */
int ext2_read_block(Ext2Ctx *ctx, u32 block_num, void *buf)
{
    int ret = ext2_raw_read_blocks(ctx, block_num, 1, buf);
    if (ret != 0) ext2_fs_error(ctx);
    return ret;
}

int ext2_write_block(Ext2Ctx *ctx, u32 block_num, const void *buf)
{
    int ret = ext2_raw_write_block(ctx, block_num, buf);
    if (ret != 0) ext2_fs_error(ctx);
    return ret;
}

/* **データブロック**の I/O。失敗しても I/O エラーを返すだけ (Linux ext2 と同じ)。
 *   読み取り: 構造には何も起きていない。
 *   書き込み: ブロックの中身が古いか半分だけ新しいだけで、どの参照も正しい
 *             ブロックを指している (参照を書くのはメタデータ側の段)。
 * どちらも「以後の書き込みが構造を壊す」原因にならないので止めない。 */
int ext2_read_data_block(Ext2Ctx *ctx, u32 block_num, void *buf)
{
    return ext2_raw_read_blocks(ctx, block_num, 1, buf);
}

int ext2_write_data_block(Ext2Ctx *ctx, u32 block_num, const void *buf)
{
    return ext2_raw_write_block(ctx, block_num, buf);
}

int ext2_read_data_blocks(Ext2Ctx *ctx, u32 first, u32 blocks, void *dst)
{
    return ext2_raw_read_blocks(ctx, first, blocks, dst);
}

int ext2_write_data_blocks(Ext2Ctx *ctx, u32 first, u32 blocks, const void *src)
{
    u32 sector;
    ext2_read_cache_reset();
    if (!ext2_block_range_lba(ctx, first, blocks, &sector)) return EXT2_ERR_IO;
    return dev_blk_write_lba(ctx->dev, sector, blocks * EXT2_SECTORS_PER_BLOCK, src);
}

/* ---- セクタ 1 本だけの I/O (票 H2 §2-2-1) ----
 *
 * ext2_write_block は 1KB ブロックを 512B x 2 回で書くので、
 * 「1 回の書き込みが失敗した = ブロックは未変更」は成り立たない。
 * 変更したフィールドを含む**セクタ 1 本だけ**を書きたい経路 (ext2_dir.c の
 * 置き換え rename の公開処理) のために、ブロック内のセクタ番号を指定できる
 * 口を出す。sect は 0 か 1。
 *
 * **エラー状態 (ext2_fs_error) は立てない。** 公開処理は失敗したセクタを
 * 読み直して「公開済み / 未公開 / 不明」の 3 値を決めるので、どちらへ倒すか
 * (そして書き込み禁止にするか) は呼び手が決める。 */
int ext2_read_sector(Ext2Ctx *ctx, u32 block_num, u32 sect, void *buf)
{
    u32 sector;
    int ret = EXT2_ERR_IO;
    if (sect < EXT2_SECTORS_PER_BLOCK &&
        ext2_block_range_lba(ctx, block_num, 1, &sector))
        ret = dev_blk_read_lba(ctx->dev, sector + sect, 1, buf);
    if (ret != 0) ext2_read_cache_reset();
    return ret;
}

int ext2_write_sector(Ext2Ctx *ctx, u32 block_num, u32 sect, const void *buf)
{
    u32 sector;
    ext2_read_cache_reset();
    if (sect >= EXT2_SECTORS_PER_BLOCK ||
        !ext2_block_range_lba(ctx, block_num, 1, &sector)) return EXT2_ERR_IO;
    return dev_blk_write_lba(ctx->dev, sector + sect, 1, buf);
}

