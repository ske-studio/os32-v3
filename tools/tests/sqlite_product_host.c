/* Host boundary only: the bundled engine keeps every product compile option.
 * Databases use memory; the copy VFS exposes only supplied read-only bytes. */
#include <string.h>
#include "sqlite3.h"
static const unsigned char *copy_bytes;
static int copy_size;
static int file_close(sqlite3_file *f) { (void)f; return SQLITE_OK; }
static int file_read(sqlite3_file *f, void *out, int amount, sqlite3_int64 offset)
{
    int available;
    (void)f;
    available = offset >= copy_size ? 0 : copy_size - (int)offset;
    if (available > amount) available = amount;
    memset(out, 0, amount);
    if (available) memcpy(out, copy_bytes + offset, available);
    return available == amount ? SQLITE_OK : SQLITE_IOERR_SHORT_READ;
}
static int file_size(sqlite3_file *f, sqlite3_int64 *size)
{ (void)f; *size = copy_size; return SQLITE_OK; }
static int file_lock(sqlite3_file *f, int lock)
{ (void)f; (void)lock; return SQLITE_OK; }
static int file_reserved(sqlite3_file *f, int *out)
{ (void)f; *out = 0; return SQLITE_OK; }
static int file_control(sqlite3_file *f, int op, void *arg)
{ (void)f; (void)op; (void)arg; return SQLITE_NOTFOUND; }
static int file_sector(sqlite3_file *f) { (void)f; return 512; }
static int file_characteristics(sqlite3_file *f) { (void)f; return 0; }
static const sqlite3_io_methods methods = {
    1, file_close, file_read, 0, 0, 0, file_size, file_lock, file_lock,
    file_reserved, file_control, file_sector, file_characteristics
};
static int copy_open(sqlite3_vfs *v, const char *p, sqlite3_file *f, int flags, int *out)
{
    (void)v;
    if (!p || strcmp(p, "/settings-copy") || !(flags & SQLITE_OPEN_READONLY))
        return SQLITE_CANTOPEN;
    f->pMethods = &methods;
    if (out) *out = SQLITE_OPEN_READONLY;
    return SQLITE_OK;
}
static int copy_access(sqlite3_vfs *v, const char *p, int flags, int *out)
{ (void)v; (void)flags; *out = !strcmp(p, "/settings-copy"); return SQLITE_OK; }
static int copy_path(sqlite3_vfs *v, const char *p, int n, char *out)
{ (void)v; if ((int)strlen(p) >= n) return SQLITE_CANTOPEN; strcpy(out, p); return SQLITE_OK; }
int product_open_copy(const unsigned char *bytes, int size, sqlite3 **db)
{
    copy_bytes = bytes;
    copy_size = size;
    return sqlite3_open_v2("/settings-copy", db, SQLITE_OPEN_READONLY, 0);
}
static int random_bytes(sqlite3_vfs *v, int n, char *out)
{ (void)v; memset(out, 0x42, n); return n; }
int sqlite3_os_init(void)
{
    static sqlite3_vfs vfs;
    vfs.iVersion = 1;
    vfs.szOsFile = sizeof(sqlite3_file);
    vfs.mxPathname = 256;
    vfs.zName = "prepare-memory-only";
    vfs.xOpen = copy_open;
    vfs.xAccess = copy_access;
    vfs.xFullPathname = copy_path;
    vfs.xRandomness = random_bytes;
    return sqlite3_vfs_register(&vfs, 1);
}
int sqlite3_os_end(void) { return SQLITE_OK; }
static unsigned char heap[SQLITE_MEMSYS5_SIZE];
int product_init(void)
{
    int rc = sqlite3_config(SQLITE_CONFIG_HEAP, heap, sizeof(heap), 32);
    return rc ? rc : sqlite3_initialize();
}
