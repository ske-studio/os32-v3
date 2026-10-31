/* offset は inode.size 以下。stream の EOF 判定は入口で行う。 */
static int ext2_read_inode_range(Ext2Ctx *ctx, const Ext2Inode *inode,
                                void *buf, u32 size, u32 offset)
{
    int ret;
    u32 bi, remaining, to_copy, total_read, phys;
    u32 byte_in_blk, blocks;
    u8 *dst;

    remaining = inode->size - offset;
    if (remaining > size) remaining = size;

    total_read = 0;
    dst = (u8 *)buf;

    bi = offset / EXT2_BLOCK_SIZE;
    byte_in_blk = offset % EXT2_BLOCK_SIZE;

    for (; remaining > 0; bi += blocks) {
        blocks = 1;
        /* 読込失敗は途中までのバイト数にせず、I/O エラーを返す (票 B8)。 */
        ret = ext2_bmap(ctx, inode, bi, &phys);
        if (ret != 0) return EXT2_ERR_IO;
        if (phys == 0) break;

        to_copy = EXT2_BLOCK_SIZE - byte_in_blk;
        if (to_copy > remaining) to_copy = remaining;

        if (byte_in_blk == 0 && to_copy == EXT2_BLOCK_SIZE) {
            /* EOF, holes and partial blocks never enter a run. */
            while (blocks < remaining / EXT2_BLOCK_SIZE &&
                   blocks < EXT2_DATA_RUN_MAX_BLOCKS) {
                u32 next;
                ret = ext2_bmap(ctx, inode, bi + blocks, &next);
                if (ret != 0) return EXT2_ERR_IO;
                if (next == 0 || next != phys + blocks) break;
                blocks++;
            }
            ret = ext2_read_data_blocks(ctx, phys, blocks, &dst[total_read]);
            to_copy = blocks * EXT2_BLOCK_SIZE;
        } else {
            ret = ext2_read_partial(ctx, phys, &dst[total_read], byte_in_blk, to_copy);
        }
        if (ret != 0) return EXT2_ERR_IO;

        total_read += to_copy;
        remaining -= to_copy;
        byte_in_blk = 0;
    }
    return (int)total_read;
}

