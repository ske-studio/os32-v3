/* Upper double-indirect table and single/lower table have separate slots. */
typedef struct {
    Ext2Ctx *ctx;
    u32 block;
    int valid;
    u8 data[EXT2_BLOCK_SIZE];
} Ext2IndirectCache;

static Ext2IndirectCache ext2_indirect_upper, ext2_indirect_lower;

static void ext2_indirect_cache_reset(void)
{
    ext2_indirect_upper.valid = 0;
    ext2_indirect_lower.valid = 0;
}

static int ext2_indirect_cache_read(Ext2Ctx *ctx, u32 block, u32 index,
                                   int upper, u32 *value)
{
    Ext2IndirectCache *slot = upper ? &ext2_indirect_upper : &ext2_indirect_lower;
    if (!(ctx->mounted && !ctx->fs_error && slot->valid &&
          slot->ctx == ctx && slot->block == block)) {
        slot->valid = 0;
        if (ext2_read_block(ctx, block, slot->data) != 0) return EXT2_ERR_IO;
        if (ctx->mounted && !ctx->fs_error && block != 0) {
            slot->ctx = ctx;
            slot->block = block;
            slot->valid = 1;
        }
    }
    *value = le32_rd(&slot->data[index * EXT2_BLOCK_PTR_SIZE]);
    return EXT2_OK;
}
