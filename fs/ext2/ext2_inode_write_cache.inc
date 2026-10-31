/* Shared codec preserves every supported inode field; raw RMW preserves neighbours
 * and bytes beyond the supported inode layout. */
static void ext2_inode_decode(const u8 *src, Ext2Inode *inode)
{
    int i;
    inode->mode  = le16_rd(&src[EXT2_INODE_MODE_OFF]);
    inode->uid   = le16_rd(&src[EXT2_INODE_UID_OFF]);
    inode->size  = le32_rd(&src[EXT2_INODE_SIZE_OFF]);
    inode->atime = le32_rd(&src[EXT2_INODE_ATIME_OFF]);
    inode->ctime = le32_rd(&src[EXT2_INODE_CTIME_OFF]);
    inode->mtime = le32_rd(&src[EXT2_INODE_MTIME_OFF]);
    inode->dtime = le32_rd(&src[EXT2_INODE_DTIME_OFF]);
    inode->gid   = le16_rd(&src[EXT2_INODE_GID_OFF]);
    inode->links_count = le16_rd(&src[EXT2_INODE_LINKS_COUNT_OFF]);
    inode->blocks = le32_rd(&src[EXT2_INODE_BLOCKS_OFF]);
    inode->flags  = le32_rd(&src[EXT2_INODE_FLAGS_OFF]);
    inode->osd1   = le32_rd(&src[EXT2_INODE_OSD1_OFF]);
    for (i = 0; i < EXT2_N_BLOCKS; i++) {
        inode->block[i] = le32_rd(&src[EXT2_INODE_BLOCK_OFF + i * EXT2_BLOCK_PTR_SIZE]);
    }
    inode->generation = le32_rd(&src[EXT2_INODE_GENERATION_OFF]);
    inode->file_acl   = le32_rd(&src[EXT2_INODE_FILE_ACL_OFF]);
    inode->dir_acl    = le32_rd(&src[EXT2_INODE_DIR_ACL_OFF]);
    inode->faddr      = le32_rd(&src[EXT2_INODE_FADDR_OFF]);
    ext2_mem_copy(inode->osd2, &src[EXT2_INODE_OSD2_OFF], EXT2_INODE_OSD2_SIZE);
}

static void ext2_inode_encode(u8 *dst, const Ext2Inode *inode)
{
    int i;
    le16_wr(&dst[EXT2_INODE_MODE_OFF], inode->mode);
    le16_wr(&dst[EXT2_INODE_UID_OFF], inode->uid);
    le32_wr(&dst[EXT2_INODE_SIZE_OFF], inode->size);
    le32_wr(&dst[EXT2_INODE_ATIME_OFF], inode->atime);
    le32_wr(&dst[EXT2_INODE_CTIME_OFF], inode->ctime);
    le32_wr(&dst[EXT2_INODE_MTIME_OFF], inode->mtime);
    le32_wr(&dst[EXT2_INODE_DTIME_OFF], inode->dtime);
    le16_wr(&dst[EXT2_INODE_GID_OFF], inode->gid);
    le16_wr(&dst[EXT2_INODE_LINKS_COUNT_OFF], inode->links_count);
    le32_wr(&dst[EXT2_INODE_BLOCKS_OFF], inode->blocks);
    le32_wr(&dst[EXT2_INODE_FLAGS_OFF], inode->flags);
    le32_wr(&dst[EXT2_INODE_OSD1_OFF], inode->osd1);
    for (i = 0; i < EXT2_N_BLOCKS; i++) {
        le32_wr(&dst[EXT2_INODE_BLOCK_OFF + i * EXT2_BLOCK_PTR_SIZE], inode->block[i]);
    }
    le32_wr(&dst[EXT2_INODE_GENERATION_OFF], inode->generation);
    le32_wr(&dst[EXT2_INODE_FILE_ACL_OFF], inode->file_acl);
    le32_wr(&dst[EXT2_INODE_DIR_ACL_OFF], inode->dir_acl);
    le32_wr(&dst[EXT2_INODE_FADDR_OFF], inode->faddr);
    ext2_mem_copy(&dst[EXT2_INODE_OSD2_OFF], inode->osd2, EXT2_INODE_OSD2_SIZE);

}

int ext2_write_inode_load(Ext2Ctx *ctx, u32 ino, const Ext2Inode *initial)
{
    Ext2WriteInode *slot = &ext2_write_work.inode;
    u32 group = (ino - 1) / ctx->sb_info.inodes_per_group;
    u32 index = (ino - 1) % ctx->sb_info.inodes_per_group;
    if (ino == 0 || group >= ctx->num_groups) return EXT2_ERR_NOTFOUND;
    slot->physical = ctx->gd_table[group].inode_table +
                     index * ctx->sb_info.inode_size / EXT2_BLOCK_SIZE;
    slot->offset = index * ctx->sb_info.inode_size % EXT2_BLOCK_SIZE;
    if (ext2_read_block(ctx, slot->physical, slot->raw) != 0) return EXT2_ERR_IO;
    if (initial) slot->current = *initial;
    else ext2_inode_decode(slot->raw + slot->offset, &slot->current);
    slot->window_start = slot->current;
    slot->valid = 1;
    slot->dirty = initial != 0;
    return EXT2_OK;
}

int ext2_write_inode_commit(Ext2Ctx *ctx)
{
    Ext2WriteInode *slot = &ext2_write_work.inode;
    if (!slot->dirty) return EXT2_OK;
    ext2_inode_encode(slot->raw + slot->offset, &slot->current);
    if (ext2_write_block(ctx, slot->physical, slot->raw) != 0) return EXT2_ERR_IO;
    slot->dirty = 0;
    if (ext2_write_work.inode_reservation.reserved)
        ext2_write_work.inode_reservation.published = 1;
    return EXT2_OK;
}
