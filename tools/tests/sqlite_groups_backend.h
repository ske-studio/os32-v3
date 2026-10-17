/* Isolated RAM filesystem boundary for the REAL bundled SQLite integration.
 * Not a copy of VFS/FD ownership. No host filesystem paths are accessed. */
#ifndef FIXTURE_FILES
#define FIXTURE_FILES 16
#endif
/* 8192 件の settings.db (票 S3-C の件数上限) と、その DELETE journal が
 * 収まる大きさ。128KiB では 8192 件の実 transaction を通せなかった。 */
#ifndef FIXTURE_BYTES
#define FIXTURE_BYTES (1024 * 1024)
#endif
typedef struct {
    int exists;
    char path[VFS_MAX_PATH];
    u32 size;
    unsigned char data[FIXTURE_BYTES];
} FixtureFile;
static FixtureFile fixture_files[FIXTURE_FILES];
static int fixture_active;
static int fixture_sync_rc, fixture_readonly;
static FixtureFile *fixture_find(const char *path, int create)
{
    int i;
    FixtureFile *empty = NULL;
    for (i = 0; i < FIXTURE_FILES; i++) {
        if (fixture_files[i].exists && !strcmp(fixture_files[i].path, path))
            return &fixture_files[i];
        if (!fixture_files[i].exists && !empty) empty = &fixture_files[i];
    }
    if (!create || !empty) return NULL;
    memset(empty, 0, sizeof(*empty));
    str_cpy(empty->path, path, sizeof(empty->path)); empty->exists = 1;
    return empty;
}
static int fixture_size(void *ctx, const char *path, u32 *size)
{
    FixtureFile *f = fixture_find(path, 0);
    probes++;
    if (!f) return VFS_ERR_NOTFOUND;
    *size = f->size; return VFS_OK;
}
static int fixture_create(void *ctx, const char *path, const void *buf, u32 size)
{
    FixtureFile *f = fixture_find(path, 1);
    probes++;
    if (!f || size > FIXTURE_BYTES) return VFS_ERR_NOSPC;
    if (size) memcpy(f->data, buf, size);
    f->size = size; return VFS_OK;
}
static int fixture_read(void *ctx, const char *path, void *buf, u32 size, u32 offset)
{
    FixtureFile *f = fixture_find(path, 0);
    probes++;
    if (!f) return VFS_ERR_NOTFOUND;
    if (offset >= f->size) return 0;
    if (size > f->size - offset) size = f->size - offset;
    memcpy(buf, f->data + offset, size); return (int)size;
}
static int fixture_write(void *ctx, const char *path, const void *buf, u32 size, u32 offset)
{
    FixtureFile *f = fixture_find(path, 0);
    probes++;
    if (!f) return VFS_ERR_NOTFOUND;
    if (offset > FIXTURE_BYTES || size > FIXTURE_BYTES - offset) return VFS_ERR_NOSPC;
    memcpy(f->data + offset, buf, size);
    if (f->size < offset + size) f->size = offset + size;
    return (int)size;
}
static int fixture_rm(const char *path)
{
    FixtureFile *f = fixture_find(path, 0);
    if (!f) return VFS_ERR_NOTFOUND;
    f->exists = 0; return VFS_OK;
}
/* Explicit stable file IDs for the ext2-capable RAM fixture. */
static int fixture_lookup_ino(void *ctx, const char *path, u32 *ino)
{
    FixtureFile *f;
    if (!fixture_active) { *ino = (u32)(unsigned char)path[1]; return VFS_OK; }
    f = fixture_find(path, 0);
    if (!f) return VFS_ERR_NOTFOUND;
    *ino = (u32)(f - fixture_files) + 1;
    return VFS_OK;
}
static const char *fixture_ino_path(u32 ino)
{
    CHECK(ino > 0 && ino <= FIXTURE_FILES);
    return fixture_files[ino - 1].path;
}
static int fixture_read_ino(void *ctx, u32 ino, void *buf, u32 size, u32 offset)
{
    if (!fixture_active) return mock_read_stream(ctx, "", buf, size, offset);
    return mock_ops.read_stream(ctx, fixture_ino_path(ino), buf, size, offset);
}
static int fixture_write_ino(void *ctx, u32 ino, const void *buf, u32 size, u32 offset)
{
    if (!fixture_active) return mock_write_stream(ctx, "", buf, size, offset);
    return mock_ops.write_stream(ctx, fixture_ino_path(ino), buf, size, offset);
}
static int fixture_stat_ino(void *ctx, u32 ino, OS32_Stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = OS_S_IFREG | OS_S_IRUSR | OS_S_IWUSR;
    st->st_ino = ino;
    st->st_size = fixture_active ? fixture_files[ino - 1].size : 8;
    return VFS_OK;
}
static int fixture_resize(void *ctx, u32 ino, u32 size)
{
    if (!fixture_active) return VFS_OK;
    if (size > FIXTURE_BYTES) return VFS_ERR_NOSPC;
    fixture_files[ino - 1].size = size;
    return VFS_OK;
}
static int fixture_truncate(void *ctx, u32 ino) { return fixture_resize(ctx, ino, 0); }
static int fixture_writable(void *ctx) { return fixture_readonly ? VFS_ERR_ROFS : VFS_OK; }
static int fixture_sync(void *ctx) { return fixture_sync_rc; }
static const VfsInoOps fixture_ino_ops = {
    fixture_lookup_ino, fixture_read_ino, fixture_write_ino,
    fixture_stat_ino, fixture_truncate
};
static const VfsSqliteOps fixture_sqlite_ops = {
    VFS_SQLITE_EXT2, fixture_resize, fixture_writable
};
static void fixture_capability(void)
{
    mock_ops.ino = &fixture_ino_ops;
    mock_ops.sqlite = &fixture_sqlite_ops;
    mock_ops.sync = fixture_sync;
}
static void fixture_init(void)
{
    fixture_active = 1;
    fixture_capability();
    mock_ops.get_file_size = fixture_size;
    mock_ops.write_file = fixture_create;
    mock_ops.read_stream = fixture_read;
    mock_ops.write_stream = fixture_write;
}
