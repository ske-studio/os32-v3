/* One synchronous owner, separate from D1's read slots. */
Ext2WriteWork ext2_write_work;

void ext2_write_abort(Ext2Ctx *ctx)
{
    if (ext2_write_work.ctx != ctx) return;
    ext2_mem_zero(&ext2_write_work, sizeof(ext2_write_work));
}

int ext2_write_begin(Ext2Ctx *ctx, u32 ino, const Ext2Inode *initial)
{
    Ext2WriteWork *w = &ext2_write_work;
    int ret;
    ext2_read_cache_reset();
    ext2_mem_zero(w, sizeof(*w));
    w->ctx = ctx; w->active = 1; w->defer_inode = initial != 0;
    if (ino == EXT2_WRITE_NEW_INO) {
        ret = ext2_write_bitmap_reserve_inode(ctx, &ino);
        if (ret != 0) { ext2_write_abort(ctx); return ret; }
    }
    w->ino = ino;
    ret = ext2_write_inode_load(ctx, ino, initial);
    if (ret != 0) ext2_write_abort(ctx);
    return ret;
}

int ext2_write_map(Ext2Ctx *ctx, u32 logical, u32 *out_phys)
{
    return ext2_write_bmap_get(ctx, logical, out_phys);
}

int ext2_write_reserve(Ext2Ctx *ctx, u32 logical, Ext2WriteRecord **out_record)
{
    Ext2WriteWork *w = &ext2_write_work;
    Ext2WriteRecord *r;
    u32 physical;
    int ret;
    if (w->data_count == EXT2_WRITE_WINDOW_BLOCKS) return EXT2_ERR_NOSPC;
    ret = ext2_write_map(ctx, logical, &physical);
    if (ret != 0) return ret;
    r = &w->record[w->data_count++];
    ext2_mem_zero(r, sizeof(*r));
    r->logical = logical; r->physical = physical; r->kind = EXT2_WRITE_KIND_DATA;
    if (!physical) {
        ret = ext2_write_bitmap_reserve(ctx, &r->physical);
        if (ret != 0) return ret;
        r->flags = EXT2_WRITE_REC_NEW;
        ret = ext2_write_bmap_set(ctx, logical, r->physical);
        if (ret != 0) return ret;
    }
    *out_record = r;
    return EXT2_OK;
}

void ext2_write_data_done(Ext2Ctx *ctx, u32 n)
{
    Ext2WriteWork *w = &ext2_write_work;
    u32 i;
    (void)ctx;
    for (i = 0; i < n; i++) w->record[w->data_done++].flags |= EXT2_WRITE_REC_DATA_DONE;
}

int ext2_write_commit_window(Ext2Ctx *ctx)
{
    Ext2WriteWork *w = &ext2_write_work;
    ext2_write_bmap_trim(ctx);
    if (ext2_write_bitmap_commit(ctx) != 0 || ext2_write_bmap_commit(ctx) != 0 ||
        (!w->defer_inode && ext2_write_inode_commit(ctx) != 0)) {
        ext2_write_abort(ctx);
        return EXT2_ERR_IO;
    }
    w->data_count = 0; w->table_count = 0; w->data_done = 0;
    ext2_mem_zero(w->record, sizeof(w->record));
    w->inode.window_start = w->inode.current;
    return EXT2_OK;
}

int ext2_write_finish(Ext2Ctx *ctx)
{
    int ret = ext2_write_commit_window(ctx);
    if (ret == 0) ret = ext2_write_inode_commit(ctx);
    ext2_write_abort(ctx);
    return ret;
}

/* The range issuer prepares and completes one window at a time. */
static int ext2_write_batch_transfer(Ext2Ctx *ctx, const void *buf, u32 size,
                                     u32 offset, int complete)
{
    Ext2WriteWork *w = &ext2_write_work;
    const u8 *src = (const u8 *)buf;
    u32 done = 0;
    int ret = EXT2_OK;
    while (done < size) {
        u32 bytes;
        ret = ext2_write_run_window(ctx, src + done, size - done, offset + done, &bytes);
        done += bytes;
        if (ret != 0) break;
        if (done < size) {
            ret = ext2_write_commit_window(ctx);
            if (ret != 0) return ret;
        }
    }
    if (ctx->fs_error) { ext2_write_abort(ctx); return EXT2_ERR_IO; }
    if (complete && ret != 0) {
        /* The current window's metadata has never been issued. Discard it before the
         * caller aborts the workspace and releases earlier unreferenced data. */
        w->data_done = 0;
        ext2_write_bmap_trim(ctx);
        w->inode.current.size = w->inode.window_start.size;
        return ret;
    }
    if (ext2_write_finish(ctx) != 0) return EXT2_ERR_IO;
    if (ret == EXT2_ERR_IO && done == 0) return EXT2_ERR_IO;
    return (int)done;
}

/* Only deferred builders use this: their on-media inode still has no block
 * references. Drop retained bitmaps before the existing cleanup does any I/O. */
static void ext2_write_build_abort(Ext2Ctx *ctx, int new_inode)
{
    Ext2Inode inode = ext2_write_work.inode.current;
    u32 ino = ext2_write_work.ino;
    int bitmap_done = ext2_write_work.inode_reservation.bitmap_done;
    ext2_write_abort(ctx);
    if (ctx->fs_error) return;
    if (new_inode && bitmap_done) {
        if (ext2_create_abort_unwritten(ctx, ino, &inode) != 0) { /* leaked */ }
    } else if (ext2_write_release_blocks(ctx, inode.block) != 0) { /* leaked */ }
}
