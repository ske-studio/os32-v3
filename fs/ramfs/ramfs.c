#include "fs/ramfs/ramfs.h"
#include "kmalloc.h"
#include "kstring.h"

typedef struct {
    char name[32];
    u8 *data;
    u32 size, cap, ino;
} RamFile;

typedef struct {
    RamFile files[RAMFS_FILES];
    u32 count, cap_total, next_ino;
} RamFs;

_Static_assert(sizeof(RamFs) <= RAMFS_META_MAX, "ramfs metadata budget");
_Static_assert(RAMFS_FILE_MAX <= RAMFS_DATA_MAX, "ramfs data budget");

static const char *file_name(const char *path)
{
    const char *name = path + 1;
    u32 n = 0;
    while (name[n] && name[n] != '/') n++;
    if (path[0] != '/' || !n || n > 31 || name[n]) return 0;
    return name;
}

static RamFile *find_file(RamFs *fs, const char *name)
{
    u32 i;
    for (i = 0; i < fs->count; i++)
        if (kstrcmp(fs->files[i].name, name) == 0) return &fs->files[i];
    return 0;
}

static RamFile *at_path(RamFs *fs, const char *path)
{
    const char *name = file_name(path);
    return name ? find_file(fs, name) : 0;
}

static void *ramfs_mount(int dev_id)
{
    (void)dev_id;
    return kzalloc(sizeof(RamFs));
}

static void ramfs_umount(void *ctx)
{
    RamFs *fs = ctx;
    u32 i;
    for (i = 0; i < fs->count; i++) kfree(fs->files[i].data);
    kfree(fs);
}

static int ramfs_is_mounted(void *ctx)
{
    return ctx != 0;
}

static int ramfs_reserve(RamFs *fs, RamFile *file, u32 need)
{
    if (need <= file->cap) return VFS_OK;
    if (need > RAMFS_FILE_MAX) return VFS_ERR_NOSPC;
    u32 cap = (need + RAMFS_GROW - 1) / RAMFS_GROW * RAMFS_GROW;
    if (cap - file->cap > RAMFS_DATA_MAX - fs->cap_total) return VFS_ERR_NOSPC;
    u8 *data = kzalloc(cap);
    if (!data) return VFS_ERR_NOSPC;
    if (file->data) {
        kmemcpy(data, file->data, file->size);
        kfree(file->data);
    }
    fs->cap_total += cap - file->cap;
    file->cap = cap;
    file->data = data;
    return VFS_OK;
}

static int ramfs_write(void *ctx, const char *path, const void *buf, u32 size)
{
    RamFs *fs = ctx;
    const char *name = file_name(path);
    if (!name) return VFS_ERR_INVAL;
    RamFile *file = find_file(fs, name);
    if (!file) {
        if (fs->count == RAMFS_FILES || fs->next_ino == (u32)-1) return VFS_ERR_NOSPC;
        file = &fs->files[fs->count];
        kmemset(file, 0, sizeof(*file));
    }
    int rc = ramfs_reserve(fs, file, size ? size : RAMFS_GROW);
    if (rc != VFS_OK) return rc;
    if (!file->ino) {
        file->ino = ++fs->next_ino;
        kstrncpy(file->name, name, sizeof(file->name));
        fs->count++;
    }
    if (size) kmemcpy(file->data, buf, size);
    file->size = size;
    return (int)size;
}

static int ramfs_size(void *ctx, const char *path, u32 *size)
{
    RamFile *file = at_path(ctx, path);
    if (!file) return VFS_ERR_NOTFOUND;
    *size = file->size;
    return VFS_OK;
}

static int read_bytes(RamFile *file, void *buf, u32 size, u32 offset)
{
    if (!file) return VFS_ERR_NOTFOUND;
    if (offset >= file->size) return 0;
    if (size > file->size - offset) size = file->size - offset;
    kmemcpy(buf, file->data + offset, size);
    return (int)size;
}

static int ramfs_read_stream(void *ctx, const char *path, void *buf, u32 size, u32 offset)
{
    RamFile *file = at_path(ctx, path);
    return read_bytes(file, buf, size, offset);
}

static int write_bytes(void *ctx, RamFile *file, const void *buf, u32 size, u32 offset)
{
    if (!file) return VFS_ERR_NOTFOUND;
    if (!size) return 0;  /* a zero-length write must not move the end */
    if (offset > RAMFS_FILE_MAX || size > RAMFS_FILE_MAX - offset) return VFS_ERR_NOSPC;
    int rc = ramfs_reserve(ctx, file, offset + size);
    if (rc != VFS_OK) return rc;
    if (offset > file->size) kmemset(file->data + file->size, 0, offset - file->size);
    kmemcpy(file->data + offset, buf, size);
    if (offset + size > file->size) file->size = offset + size;
    return (int)size;
}

static int ramfs_write_stream(void *ctx, const char *path, const void *buf, u32 size, u32 offset)
{
    RamFile *file = at_path(ctx, path);
    return write_bytes(ctx, file, buf, size, offset);
}

static int ramfs_list(void *ctx, const char *path, vfs_dir_cb cb, void *user)
{
    RamFs *fs = ctx;
    u32 i;
    if (kstrcmp(path, "/") != 0) return VFS_ERR_NOTDIR;
    for (i = 0; i < fs->count; i++) {
        VfsDirEntry entry;
        kmemset(&entry, 0, sizeof(entry));
        kstrncpy(entry.name, fs->files[i].name, sizeof(entry.name));
        entry.size = fs->files[i].size;
        entry.type = VFS_TYPE_FILE;
        cb(&entry, user);
    }
    return VFS_OK;
}

static int file_stat(RamFile *file, OS32_Stat *out)
{
    kmemset(out, 0, sizeof(*out));
    if (!file) return VFS_ERR_NOTFOUND;
    out->st_mode = OS_S_IFREG | OS_S_IRWXU;
    out->st_nlink = 1;
    out->st_ino = file->ino;
    out->st_size = file->size;
    return VFS_OK;
}

static int ramfs_stat(void *ctx, const char *path, OS32_Stat *out)
{
    if (kstrcmp(path, "/") == 0) {
        kmemset(out, 0, sizeof(*out));
        out->st_mode = OS_S_IFDIR | OS_S_IRWXU;
        out->st_nlink = 2;
        return VFS_OK;
    }
    return file_stat(at_path(ctx, path), out);
}

static RamFile *at_ino(RamFs *fs, u32 ino)
{
    for (u32 i = 0; i < fs->count; i++)
        if (fs->files[i].ino == ino) return &fs->files[i];
    return 0;
}

static int ramfs_lookup(void *ctx, const char *path, u32 *ino)
{
    RamFile *file = at_path(ctx, path);
    if (!file) return VFS_ERR_NOTFOUND;
    *ino = file->ino;
    return VFS_OK;
}

static int ramfs_create_excl(void *ctx, const char *path)
{
    const char *name = file_name(path);
    if (name && find_file(ctx, name)) return VFS_ERR_EXIST;
    return ramfs_write(ctx, path, 0, 0);
}

static int ramfs_unlink(void *ctx, const char *path)
{
    RamFs *fs = ctx;
    RamFile *file = at_path(fs, path);
    if (!file) return VFS_ERR_NOTFOUND;
    fs->cap_total -= file->cap;
    kfree(file->data);
    *file = fs->files[--fs->count];
    return VFS_OK;
}

static int ramfs_resize(void *ctx, u32 ino, u32 size)
{
    RamFs *fs = ctx;
    RamFile *file = at_ino(fs, ino);
    if (!file) return VFS_ERR_NOTFOUND;
    if (size > file->size) return VFS_ERR_NOSYS;

    u32 cap = size ? (size + RAMFS_GROW - 1) / RAMFS_GROW * RAMFS_GROW : RAMFS_GROW;
    if (cap < file->cap) {
        u8 *data = kmalloc(cap);
        if (!data) return VFS_ERR_NOSPC;
        if (size) kmemcpy(data, file->data, size);
        kfree(file->data);
        file->data = data;
        fs->cap_total -= file->cap - cap;
        file->cap = cap;
    }
    file->size = size;
    return VFS_OK;
}

static int ramfs_truncate(void *ctx, u32 ino)
{
    return ramfs_resize(ctx, ino, 0);
}

static int ramfs_sync(void *ctx)
{
    (void)ctx;
    return VFS_OK;
}

static int ramfs_writable(void *ctx)
{
    (void)ctx;
    return VFS_OK;
}

static int ramfs_ino_read(void *ctx, u32 ino, void *buf, u32 size, u32 offset)
{
    return read_bytes(at_ino(ctx, ino), buf, size, offset);
}

static int ramfs_ino_write(void *ctx, u32 ino, const void *buf, u32 size, u32 offset)
{
    return write_bytes(ctx, at_ino(ctx, ino), buf, size, offset);
}

static int ramfs_ino_stat(void *ctx, u32 ino, OS32_Stat *out)
{
    return file_stat(at_ino(ctx, ino), out);
}

static int ramfs_read(void *ctx, const char *path, void *buf, u32 size)
{
    return ramfs_read_stream(ctx, path, buf, size, 0);
}

static const VfsInoOps ramfs_ino_ops = {
    ramfs_lookup, ramfs_ino_read, ramfs_ino_write, ramfs_ino_stat, ramfs_truncate
};
static const VfsSqliteOps ramfs_sqlite_ops = {
    VFS_SQLITE_RAMFS, ramfs_resize, ramfs_writable
};

VfsOps ramfs_ops = {
    .name = "ramfs", .mount = ramfs_mount, .umount = ramfs_umount,
    .is_mounted = ramfs_is_mounted, .read_file = ramfs_read,
    .write_file = ramfs_write, .get_file_size = ramfs_size,
    .read_stream = ramfs_read_stream, .write_stream = ramfs_write_stream,
    .list_dir = ramfs_list, .stat = ramfs_stat,
    .unlink = ramfs_unlink, .create_excl = ramfs_create_excl, .sync = ramfs_sync,
    .ino = &ramfs_ino_ops, .sqlite = &ramfs_sqlite_ops
};
