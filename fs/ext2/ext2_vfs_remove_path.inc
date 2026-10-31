/* unlink/rmdir の親解決と最終操作。ext2_vfs.c から取り込む。 */
static int ext2_vfs_remove_path(Ext2Ctx *ec, const char *path,
                              int (*remove)(Ext2Ctx *, u32, const char *))
{
    char dir_path[VFS_MAX_PATH];
    const char *name;
    u32 parent_ino;
    int rc;

    ext2_split_path(path, dir_path, &name);

    rc = ext2_resolve_path(ec, dir_path, &parent_ino);
    if (rc != VFS_OK) return rc;
    return ext2_to_vfs_err(remove(ec, parent_ino, name));
}
