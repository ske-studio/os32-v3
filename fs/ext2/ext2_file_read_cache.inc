/* Only partial reads use this slot; full blocks transfer to the destination. */
static struct {
    Ext2Ctx *ctx;
    u32 block;
    int valid;
    u8 data[EXT2_BLOCK_SIZE];
} ext2_data_cache;

void ext2_data_cache_reset(void)
{
    ext2_data_cache.valid = 0;
}

static int ext2_read_partial(Ext2Ctx *ctx, u32 block, void *dst,
                             u32 offset, u32 size)
{
    if (!(ctx->mounted && !ctx->fs_error && ext2_data_cache.valid &&
          ext2_data_cache.ctx == ctx && ext2_data_cache.block == block)) {
        ext2_data_cache.valid = 0;
        if (ext2_read_data_block(ctx, block, ext2_data_cache.data) != 0)
            return EXT2_ERR_IO;
        if (ctx->mounted && !ctx->fs_error && block != 0) {
            ext2_data_cache.ctx = ctx;
            ext2_data_cache.block = block;
            ext2_data_cache.valid = 1;
        }
    }
    ext2_mem_copy(dst, &ext2_data_cache.data[offset], size);
    return EXT2_OK;
}
