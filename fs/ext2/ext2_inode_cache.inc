/* One decoded inode. Never retain a pointer into the shared scratch block. */
static struct {
    Ext2Ctx *ctx;
    u32 ino;
    int valid;
    Ext2Inode inode;
} ext2_inode_cache;

static void ext2_inode_cache_reset(void)
{
    ext2_inode_cache.valid = 0;
}

#ifdef EXT2_READ_CACHE_HOST_TEST
/* B8 must still reach the second inode read within the same open. */
static int ext2_inode_cache_disabled;
void ext2_inode_cache_test_disable(int disabled)
{
    ext2_read_cache_reset();
    ext2_inode_cache_disabled = disabled;
}
#endif

static int ext2_inode_cache_get(Ext2Ctx *ctx, u32 ino, Ext2Inode *inode)
{
    int enabled = ctx->mounted && !ctx->fs_error;
#ifdef EXT2_READ_CACHE_HOST_TEST
    enabled = enabled && !ext2_inode_cache_disabled;
#endif
    if (enabled && ext2_inode_cache.valid &&
        ext2_inode_cache.ctx == ctx && ext2_inode_cache.ino == ino) {
        *inode = ext2_inode_cache.inode;
        return 1;
    }
    ext2_inode_cache_reset();
    return 0;
}

static void ext2_inode_cache_put(Ext2Ctx *ctx, u32 ino, const Ext2Inode *inode)
{
    if (!ctx->mounted || ctx->fs_error) return;
#ifdef EXT2_READ_CACHE_HOST_TEST
    if (ext2_inode_cache_disabled) return;
#endif
    ext2_inode_cache.inode = *inode;
    ext2_inode_cache.ctx = ctx;
    ext2_inode_cache.ino = ino;
    ext2_inode_cache.valid = 1;
}
