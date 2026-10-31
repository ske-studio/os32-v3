/* Reserve just this window before issuing data, so new allocations can join
 * existing blocks in a run. No data copy is retained beside the caller's buf. */
static int ext2_write_run_window(Ext2Ctx *ctx, const u8 *src, u32 size,
                                  u32 offset, u32 *completed)
{
    Ext2WriteWork *w = &ext2_write_work;
    u32 planned = 0, logical = offset / EXT2_BLOCK_SIZE;
    u32 edge = offset % EXT2_BLOCK_SIZE;
    int ret = EXT2_OK;
    *completed = 0;
    while (planned < size && w->data_count < EXT2_WRITE_WINDOW_BLOCKS) {
        Ext2WriteRecord *r;
        u32 bytes = EXT2_BLOCK_SIZE - edge;
        if (bytes > size - planned) bytes = size - planned;
        ret = ext2_write_reserve(ctx, logical, &r);
        if (ret != 0) break;
        planned += bytes;
        logical++; edge = 0;
    }
    /* A metadata read failure forbids all subsequent writes, including data. */
    if (ctx->fs_error) return EXT2_ERR_IO;
    edge = offset % EXT2_BLOCK_SIZE;
    while (*completed < planned) {
        Ext2WriteRecord *r = &w->record[w->data_done];
        const u8 *data = src + *completed;
        u32 blocks = 1, bytes = EXT2_BLOCK_SIZE - edge;
        int io;
        if (bytes > planned - *completed) bytes = planned - *completed;
        if (edge || bytes < EXT2_BLOCK_SIZE) {
            if (r->flags & EXT2_WRITE_REC_NEW) ext2_mem_zero(ext2_g_dat, EXT2_BLOCK_SIZE);
            else if (ext2_read_data_block(ctx, r->physical, ext2_g_dat) != 0)
                return EXT2_ERR_IO;
            ext2_mem_copy(ext2_g_dat + edge, data, bytes);
            io = ext2_write_data_block(ctx, r->physical, ext2_g_dat);
        } else {
            while (blocks < (planned - *completed) / EXT2_BLOCK_SIZE &&
                   blocks < EXT2_DATA_RUN_MAX_BLOCKS &&
                   w->record[w->data_done + blocks].physical == r->physical + blocks)
                blocks++;
            io = ext2_write_data_blocks(ctx, r->physical, blocks, data);
            bytes = blocks * EXT2_BLOCK_SIZE;
        }
        /* Even a final-completion failure leaves the whole request uncounted. */
        if (io != 0) return EXT2_ERR_IO;
        ext2_write_data_done(ctx, blocks);
        *completed += bytes;
        edge = 0;
        if (offset + *completed > w->inode.current.size)
            w->inode.current.size = offset + *completed;
        w->inode.dirty = 1;
    }
    return ret;
}
