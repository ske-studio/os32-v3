static Ext2WriteTable *ext2_write_table_find(u32 physical)
{
    u32 i;
    for (i = 0; i < EXT2_WRITE_TABLE_SLOTS; i++) {
        Ext2WriteTable *slot = &ext2_write_work.table[i];
        if (slot->valid && slot->physical == physical) return slot;
    }
    return (Ext2WriteTable *)0;
}

/* Forward-only windows touch at most two lower tables. Dirty slots cannot be
 * evicted: their children and allocation bits must precede any parent write. */
static int ext2_write_table_load(Ext2Ctx *ctx, u32 physical, int upper,
                                 int fresh, Ext2WriteTable **out)
{
    Ext2WriteTable *slot = ext2_write_table_find(physical);
    u32 i, first = upper ? EXT2_WRITE_UPPER_SLOT : EXT2_WRITE_LOWER_FIRST;
    u32 end = upper ? EXT2_WRITE_LOWER_FIRST : EXT2_WRITE_TABLE_SLOTS;
    if (slot) { *out = slot; return EXT2_OK; }
    for (i = first; i < end; i++) {
        slot = &ext2_write_work.table[i];
        if (slot->valid && slot->dirty) continue;
        slot->valid = 0;
        if (fresh) ext2_mem_zero(slot->data, EXT2_BLOCK_SIZE);
        else if (ext2_read_block(ctx, physical, slot->data) != 0) return EXT2_ERR_IO;
        slot->physical = physical;
        slot->valid = 1;
        slot->dirty = fresh;
        *out = slot;
        return EXT2_OK;
    }
    return EXT2_ERR_NOSPC; /* caller commits before advancing beyond 64 data */
}

/* Locate the leaf pointer; create only zero entries. parent == 0 is inode. */
static int ext2_write_leaf(Ext2Ctx *ctx, u32 logical, int create,
                            u32 *parent, u16 *index);

static void ext2_write_link(u32 parent, u16 index, u32 physical)
{
    if (parent == 0) ext2_write_work.inode.current.block[index] = physical;
    else {
        Ext2WriteTable *slot = ext2_write_table_find(parent);
        le32_wr(slot->data + index * EXT2_BLOCK_PTR_SIZE, physical);
        slot->dirty = 1;
    }
    ext2_write_work.inode.dirty = 1;
}

static int ext2_write_new_table(Ext2Ctx *ctx, u32 parent, u16 index,
                                int upper, u32 *physical)
{
    Ext2WriteWork *w = &ext2_write_work;
    Ext2WriteTable *slot;
    Ext2WriteRecord *r;
    int ret = ext2_write_bitmap_reserve(ctx, physical);
    if (ret != 0) return ret;
    ret = ext2_write_table_load(ctx, *physical, upper, 1, &slot);
    if (ret != 0) { ext2_write_bitmap_cancel(ctx, *physical); return ret; }
    r = &w->record[EXT2_WRITE_WINDOW_BLOCKS + w->table_count++];
    r->physical = *physical; r->parent = parent; r->index = index;
    r->kind = upper ? EXT2_WRITE_KIND_UPPER : EXT2_WRITE_KIND_LOWER;
    r->flags = EXT2_WRITE_REC_NEW | EXT2_WRITE_REC_LINKED;
    ext2_write_link(parent, index, *physical);
    w->inode.current.blocks += EXT2_SECTORS_PER_BLOCK;
    return EXT2_OK;
}

static int ext2_write_leaf(Ext2Ctx *ctx, u32 logical, int create,
                            u32 *parent, u16 *index)
{
    Ext2Inode *inode = &ext2_write_work.inode.current;
    Ext2WriteTable *slot;
    u32 physical, upper_index;
    int ret;
    *parent = 0;
    if (logical < EXT2_NDIR_BLOCKS) { *index = (u16)logical; return EXT2_OK; }
    logical -= EXT2_NDIR_BLOCKS;
    if (logical < EXT2_ADDR_PER_BLOCK) {
        physical = inode->block[EXT2_IND_BLOCK];
        if (!physical && create) {
            ret = ext2_write_new_table(ctx, 0, EXT2_IND_BLOCK, 0, &physical);
            if (ret != 0) return ret;
        }
        if (physical) {
            ret = ext2_write_table_load(ctx, physical, 0, 0, &slot);
            if (ret != 0) return ret;
        }
        *parent = physical; *index = (u16)logical;
        return EXT2_OK;
    }
    logical -= EXT2_ADDR_PER_BLOCK;
    if (logical >= EXT2_ADDR_PER_BLOCK * EXT2_ADDR_PER_BLOCK) return EXT2_ERR_NOSPC;
    physical = inode->block[EXT2_DIND_BLOCK];
    if (!physical && create) {
        ret = ext2_write_new_table(ctx, 0, EXT2_DIND_BLOCK, 1, &physical);
        if (ret != 0) return ret;
    }
    *index = (u16)(logical % EXT2_ADDR_PER_BLOCK);
    if (!physical) return EXT2_OK;
    ret = ext2_write_table_load(ctx, physical, 1, 0, &slot);
    if (ret != 0) return ret;
    upper_index = logical / EXT2_ADDR_PER_BLOCK;
    *parent = le32_rd(slot->data + upper_index * EXT2_BLOCK_PTR_SIZE);
    if (!*parent && create) {
        ret = ext2_write_new_table(ctx, physical, (u16)upper_index, 0, parent);
        if (ret != 0) return ret;
    }
    if (*parent) return ext2_write_table_load(ctx, *parent, 0, 0, &slot);
    return EXT2_OK;
}

int ext2_write_bmap_get(Ext2Ctx *ctx, u32 logical, u32 *out_phys)
{
    u32 parent;
    u16 index;
    int ret;
    *out_phys = 0;
    ret = ext2_write_leaf(ctx, logical, 0, &parent, &index);
    if (ret != 0) return ret;
    if (logical < EXT2_NDIR_BLOCKS) *out_phys = ext2_write_work.inode.current.block[index];
    else if (parent) *out_phys = le32_rd(ext2_write_table_find(parent)->data +
                                       index * EXT2_BLOCK_PTR_SIZE);
    return EXT2_OK;
}

int ext2_write_bmap_set(Ext2Ctx *ctx, u32 logical, u32 physical)
{
    u32 parent;
    u16 index;
    Ext2WriteRecord *r = &ext2_write_work.record[ext2_write_work.data_count - 1];
    int ret = ext2_write_leaf(ctx, logical, 1, &parent, &index);
    if (ret != 0) return ret;
    r->parent = parent; r->index = index;
    ext2_write_link(parent, index, physical);
    r->flags |= EXT2_WRITE_REC_LINKED;
    ext2_write_work.inode.current.blocks += EXT2_SECTORS_PER_BLOCK;
    return EXT2_OK;
}

static void ext2_write_cancel_record(Ext2Ctx *ctx, Ext2WriteRecord *r)
{
    if (!(r->flags & EXT2_WRITE_REC_NEW) || !r->physical) return;
    if (r->flags & EXT2_WRITE_REC_LINKED) {
        ext2_write_link(r->parent, r->index, 0);
        ext2_write_work.inode.current.blocks -= EXT2_SECTORS_PER_BLOCK;
    }
    ext2_write_bitmap_cancel(ctx, r->physical);
    r->flags = 0;
    r->physical = 0;
}

void ext2_write_bmap_trim(Ext2Ctx *ctx)
{
    Ext2WriteWork *w = &ext2_write_work;
    u32 i, j;
    for (i = w->data_done; i < w->data_count; i++)
        ext2_write_cancel_record(ctx, &w->record[i]);
    /* Reverse creation order drops empty children before their new parents. */
    for (i = w->table_count; i > 0; i--) {
        Ext2WriteRecord *r = &w->record[EXT2_WRITE_WINDOW_BLOCKS + i - 1];
        Ext2WriteTable *slot;
        if (!r->physical) continue;
        slot = ext2_write_table_find(r->physical);
        for (j = 0; j < EXT2_BLOCK_SIZE && slot->data[j] == 0; j++) { }
        if (j != EXT2_BLOCK_SIZE) continue;
        ext2_write_cancel_record(ctx, r);
        slot->valid = 0; slot->dirty = 0;
    }
}

int ext2_write_bmap_commit(Ext2Ctx *ctx)
{
    u32 i;
    /* slots 1,2 are children of slot 0; unchanged upper tables stay clean. */
    for (i = EXT2_WRITE_LOWER_FIRST; i < EXT2_WRITE_TABLE_SLOTS + 1; i++) {
        Ext2WriteTable *slot = &ext2_write_work.table[i % EXT2_WRITE_TABLE_SLOTS];
        if (!slot->valid || !slot->dirty) continue;
        if (ext2_write_block(ctx, slot->physical, slot->data) != 0) return EXT2_ERR_IO;
        slot->dirty = 0;
    }
    return EXT2_OK;
}
