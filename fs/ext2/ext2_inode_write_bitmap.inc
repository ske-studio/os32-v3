/* Reservations live only in the write workspace until the bitmap is written. */
int ext2_write_bitmap_reserve(Ext2Ctx *ctx, u32 *out_phys)
{
    u32 g, bit, limit;
    for (g = 0; g < ctx->num_groups; g++) {
        Ext2WriteBitmap *slot = &ext2_write_work.block_bitmap[g];
        if (ctx->gd_table[g].free_blocks <= slot->reserved) continue;
        if (!slot->valid) {
            if (ext2_read_block(ctx, ctx->gd_table[g].block_bitmap, slot->data) != 0)
                return EXT2_ERR_IO;
            slot->valid = 1;
        }
        limit = ctx->sb_info.blocks_per_group;
        if (g == ctx->num_groups - 1) {
            u32 remaining = ctx->sb_info.total_blocks - ctx->sb_info.first_data_block -
                            g * ctx->sb_info.blocks_per_group;
            if (limit > remaining) limit = remaining;
        }
        for (bit = 0; bit < limit; bit++) {
            if (slot->data[bit / BITS_PER_BYTE] & (1u << (bit % BITS_PER_BYTE))) continue;
            slot->data[bit / BITS_PER_BYTE] |= (u8)(1u << (bit % BITS_PER_BYTE));
            slot->reserved++;
            slot->dirty = 1;
            *out_phys = ctx->sb_info.first_data_block + g * ctx->sb_info.blocks_per_group + bit;
            return EXT2_OK;
        }
    }
    return EXT2_ERR_NOSPC;
}

int ext2_write_bitmap_reserve_inode(Ext2Ctx *ctx, u32 *out_ino)
{
    Ext2WriteBitmap *slot = &ext2_write_work.inode_bitmap;
    Ext2WriteInodeReservation *r = &ext2_write_work.inode_reservation;
    u32 g, bit;
    for (g = 0; g < ctx->num_groups; g++) {
        if (ctx->gd_table[g].free_inodes == 0) continue;
        slot->valid = 0;
        if (ext2_read_block(ctx, ctx->gd_table[g].inode_bitmap, slot->data) != 0)
            return EXT2_ERR_IO;
        slot->valid = 1;
        for (bit = 0; bit < ctx->sb_info.inodes_per_group; bit++) {
            u32 ino = g * ctx->sb_info.inodes_per_group + bit + 1;
            if (ino > ctx->sb_info.total_inodes) break;
            if (slot->data[bit / BITS_PER_BYTE] & (1u << (bit % BITS_PER_BYTE))) continue;
            slot->data[bit / BITS_PER_BYTE] |= (u8)(1u << (bit % BITS_PER_BYTE));
            slot->reserved = 1;
            slot->dirty = 1;
            r->group = g; r->bit = bit; r->reserved = 1;
            *out_ino = ino;
            return EXT2_OK;
        }
    }
    return EXT2_ERR_NOSPC;
}

void ext2_write_bitmap_cancel(Ext2Ctx *ctx, u32 physical)
{
    u32 relative = physical - ctx->sb_info.first_data_block;
    u32 group = relative / ctx->sb_info.blocks_per_group;
    u32 bit = relative % ctx->sb_info.blocks_per_group;
    Ext2WriteBitmap *slot = &ext2_write_work.block_bitmap[group];
    slot->data[bit / BITS_PER_BYTE] &= (u8)~(1u << (bit % BITS_PER_BYTE));
    slot->reserved--;
    slot->dirty = slot->reserved != 0;
}

int ext2_write_bitmap_commit(Ext2Ctx *ctx)
{
    u32 g, i;
    Ext2WriteBitmap *slot;
    for (g = 0; g < ctx->num_groups; g++) {
        slot = &ext2_write_work.block_bitmap[g];
        if (!slot->dirty) continue;
        if (ext2_write_block(ctx, ctx->gd_table[g].block_bitmap, slot->data) != 0)
            return EXT2_ERR_IO;
        ctx->gd_table[g].free_blocks -= slot->reserved;
        ctx->sb_info.free_blocks_count -= slot->reserved;
        slot->reserved = 0;
        slot->dirty = 0;
        ext2_meta_touch(ctx);
        for (i = 0; i < EXT2_WRITE_RECORDS; i++) {
            Ext2WriteRecord *r = &ext2_write_work.record[i];
            if ((r->flags & EXT2_WRITE_REC_NEW) && r->physical &&
                (r->physical - ctx->sb_info.first_data_block) /
                ctx->sb_info.blocks_per_group == g)
                r->flags |= EXT2_WRITE_REC_BITMAP_DONE;
        }
    }
    slot = &ext2_write_work.inode_bitmap;
    if (slot->dirty) {
        g = ext2_write_work.inode_reservation.group;
        if (ext2_write_block(ctx, ctx->gd_table[g].inode_bitmap, slot->data) != 0)
            return EXT2_ERR_IO;
        ctx->gd_table[g].free_inodes--;
        ctx->sb_info.free_inodes_count--;
        slot->reserved = 0;
        slot->dirty = 0;
        ext2_write_work.inode_reservation.bitmap_done = 1;
        ext2_meta_touch(ctx);
    }
    return EXT2_OK;
}
