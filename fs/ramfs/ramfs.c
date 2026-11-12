#include "fs/ramfs/ramfs.h"
#include "kmalloc.h"
#include "kstring.h"

typedef struct {
    char name[32];
    u8 *data;
    u32 size;
} RamFile;

typedef struct {
    RamFile files[RAMFS_FILES];
    u32 count;
} RamFs;

_Static_assert(sizeof(RamFs) <= RAMFS_META_MAX, "ramfs metadata budget");
_Static_assert(RAMFS_FILES * RAMFS_FILE_MAX <= RAMFS_DATA_MAX, "ramfs data budget");

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

static int ramfs_read(void *ctx, const char *path, void *buf, u32 max_size)
{
    const char *name = file_name(path);
    RamFile *file = name ? find_file(ctx, name) : 0;
    u32 size;
    if (!file) return VFS_ERR_NOTFOUND;
    size = file->size < max_size ? file->size : max_size;
    kmemcpy(buf, file->data, size);
    return (int)size;
}

static int ramfs_write(void *ctx, const char *path, const void *buf, u32 size)
{
    RamFs *fs = ctx;
    const char *name = file_name(path);
    RamFile *file;
    if (!name) return VFS_ERR_INVAL;
    if (size > RAMFS_FILE_MAX) return VFS_ERR_NOSPC;
    file = find_file(fs, name);
    if (!file) {
        u8 *data;
        if (fs->count == RAMFS_FILES) return VFS_ERR_NOSPC;
        data = kmalloc(RAMFS_FILE_MAX);
        if (!data) return VFS_ERR_NOSPC;
        file = &fs->files[fs->count++];
        kstrncpy(file->name, name, sizeof(file->name));
        file->data = data;
    }
    kmemcpy(file->data, buf, size);
    file->size = size;
    return (int)size;
}

static int ramfs_size(void *ctx, const char *path, u32 *size)
{
    const char *name = file_name(path);
    RamFile *file = name ? find_file(ctx, name) : 0;
    if (!file) return VFS_ERR_NOTFOUND;
    *size = file->size;
    return VFS_OK;
}

static int ramfs_read_stream(void *ctx, const char *path, void *buf, u32 size, u32 offset)
{
    const char *name = file_name(path);
    RamFile *file = name ? find_file(ctx, name) : 0;
    if (!file) return VFS_ERR_NOTFOUND;
    if (offset >= file->size) return 0;
    if (size > file->size - offset) size = file->size - offset;
    kmemcpy(buf, file->data + offset, size);
    return (int)size;
}

static int ramfs_write_stream(void *ctx, const char *path, const void *buf, u32 size, u32 offset)
{
    const char *name = file_name(path);
    RamFile *file = name ? find_file(ctx, name) : 0;
    if (!file) return VFS_ERR_NOTFOUND;
    if (offset > RAMFS_FILE_MAX || size > RAMFS_FILE_MAX - offset) return VFS_ERR_NOSPC;
    if (offset > file->size) kmemset(file->data + file->size, 0, offset - file->size);
    kmemcpy(file->data + offset, buf, size);
    if (offset + size > file->size) file->size = offset + size;
    return (int)size;
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

static int ramfs_stat(void *ctx, const char *path, OS32_Stat *out)
{
    const char *name;
    RamFile *file;
    kmemset(out, 0, sizeof(*out));
    if (kstrcmp(path, "/") == 0) {
        out->st_mode = OS_S_IFDIR | OS_S_IRWXU;
        out->st_nlink = 2;
        return VFS_OK;
    }
    name = file_name(path);
    file = name ? find_file(ctx, name) : 0;
    if (!file) return VFS_ERR_NOTFOUND;
    out->st_mode = OS_S_IFREG | OS_S_IRWXU;
    out->st_nlink = 1;
    out->st_size = file->size;
    return VFS_OK;
}

VfsOps ramfs_ops = {
    .name = "ramfs", .mount = ramfs_mount, .umount = ramfs_umount,
    .is_mounted = ramfs_is_mounted, .read_file = ramfs_read,
    .write_file = ramfs_write, .get_file_size = ramfs_size,
    .read_stream = ramfs_read_stream, .write_stream = ramfs_write_stream, .list_dir = ramfs_list, .stat = ramfs_stat
};
