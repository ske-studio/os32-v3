/* Isolated RAM filesystem boundary for the REAL bundled SQLite integration.
 * Not a copy of VFS/FD ownership. No host filesystem paths are accessed. */
#define FIXTURE_FILES 16
/* 8192 件の settings.db (票 S3-C の件数上限) と、その DELETE journal が
 * 収まる大きさ。128KiB では 8192 件の実 transaction を通せなかった。 */
#define FIXTURE_BYTES (1024 * 1024)
typedef struct {
    int exists;
    char path[VFS_MAX_PATH];
    u32 size;
    unsigned char data[FIXTURE_BYTES];
} FixtureFile;
static FixtureFile fixture_files[FIXTURE_FILES];
static int fixture_active;
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
static void fixture_init(void)
{
    fixture_active = 1;
    mock_ops.get_file_size = fixture_size;
    mock_ops.write_file = fixture_create;
    mock_ops.read_stream = fixture_read;
    mock_ops.write_stream = fixture_write;
}
