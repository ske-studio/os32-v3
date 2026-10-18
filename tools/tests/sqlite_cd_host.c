/* T4b-4c: bundled SQLite -> real VFS/FD -> real ISO -> real ATAPI.
 * Only ATA ports, owner and TTY are mocked. Media is built before mounting. */
#define main cd_baseline_main
#include "cd_read_host.c"
#undef main
#include "../../fs/vfs.c"
#include "../../fs/vfs_fd.c"
#include "../../lib/sqlite3/sqlite3.h"
#include "../../lib/sqlite3/os32_sqlite_vfs.c"

int kstrncmp(const char *a, const char *b, u32 n) { return strncmp(a, b, n); }
void *kmemset(void *dst, int c, u32 n) { return memset(dst, c, n); }
u32 sys_time(void) { return 0; }
int res_owner_get(void) { return 2; }
int fd_is_redirected(int fd) { return 0; }
u16 fd_redirect_ifmt(int fd, int *out) { *out = -1; return OS_S_IFCHR; }
int fd_redirect_read(int fd, void *buf, u32 size) { return VFS_ERR_INVAL; }
int fd_redirect_write(int fd, const void *buf, u32 size) { return VFS_ERR_INVAL; }
int kbd_getchar_kapi(void) { return '\n'; }
void console_write(const char *buf, u32 size, u8 color) { }
static int writes;
static int count_write(void *ctx, const char *p, const void *buf, u32 size)
{ writes++; return VFS_ERR_ROFS; }
static int count_stream(void *ctx, const char *p, const void *buf, u32 size, u32 off)
{ writes++; return VFS_ERR_ROFS; }
static int count_unlink(void *ctx, const char *p) { writes++; return VFS_ERR_ROFS; }
static sqlite3_vfs *cd_group(int kind, int owner, Os32SqliteGroup *g)
{
    CHECK(os32_sqlite_group_acquire(kind, owner, g) == SQLITE_OK);
    return sqlite3_vfs_find(os32_sqlite_group_vfs_name(g));
}
static void close_db(Os32SqliteGroup *g, sqlite3 *db)
{
    CHECK(os32_sqlite_group_begin_close(g) == SQLITE_OK);
    CHECK(sqlite3_close(db) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(g, SQLITE_OK) == SQLITE_OK);
}
static sqlite3_file *db_file(sqlite3 *db)
{
    sqlite3_file *f = NULL;
    CHECK(sqlite3_file_control(db, "main", SQLITE_FCNTL_FILE_POINTER, &f) == SQLITE_OK);
    CHECK(f && f->pMethods);
    return f;
}
static void build_database_media(const char *case_name)
{
    sqlite3 *db = NULL;
    sqlite3_int64 bytes;
    unsigned char *data;
    u8 *root;
    u32 off = 0;
    CHECK(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "CREATE TABLE dict(yomi TEXT,kanji TEXT);"
        "INSERT INTO dict VALUES('ki','tree')", NULL, NULL, NULL) == SQLITE_OK);
    data = sqlite3_serialize(db, "main", &bytes, 0);
    CHECK(data && bytes > 0 && bytes < 64 * 1024);
    memcpy(g_media_a.img + BIG_LBA_A * SEC, data, (size_t)bytes);
    sqlite3_free(data); CHECK(sqlite3_close(db) == SQLITE_OK);
    root = g_media_a.img + ROOT_LBA * SEC;
    memset(root, 0, SEC);
    off += put_rec(root + off, ROOT_LBA, SEC, ISO_FLAG_DIRECTORY, "\0", 1);
    off += put_rec(root + off, ROOT_LBA, SEC, ISO_FLAG_DIRECTORY, "\1", 1);
    off += put_rec(root + off, BIG_LBA_A, (u32)bytes, 0, "FEP.DB;1", 8);
    off += put_rec(root + off, BIG_LBA_A, (u32)bytes, 0, "ALIAS.DB;1", 10);
    if (!strcmp(case_name, "journal"))
        put_rec(root + off, 35, 8, 0, "FEP.DB-JOURNAL;1", 16);
    if (!strcmp(case_name, "wal"))
        put_rec(root + off, 36, 8, 0, "FEP.DB-WAL;1", 12);
}
static int fail_stat(void *ctx, const char *path, OS32_Stat *st) { return VFS_ERR_IO; }
static void cd_access(sqlite3_vfs *v)
{
    int out = 17;
    CHECK(v->xAccess(v, "/fep.db", SQLITE_ACCESS_EXISTS, &out) == SQLITE_OK && out == 1);
    CHECK(v->xAccess(v, "/missing", SQLITE_ACCESS_EXISTS, &out) == SQLITE_OK && out == 0);
    CHECK(v->xAccess(v, "/fep.db", SQLITE_ACCESS_READ, &out) == SQLITE_OK && out == 1);
    CHECK(v->xAccess(v, "/fep.db", SQLITE_ACCESS_READWRITE, &out) == SQLITE_OK && out == 0);
    iso9660_ops.stat = fail_stat; out = 17;
    CHECK(v->xAccess(v, "/fep.db", SQLITE_ACCESS_EXISTS, &out) == SQLITE_IOERR_ACCESS && out == 17);
    iso9660_ops.stat = iso9660_stat;
}
#ifndef SQLITE_CD_HOST_MAIN
#define SQLITE_CD_HOST_MAIN main
#endif
int SQLITE_CD_HOST_MAIN(int argc, char **argv)
{
    Os32SqliteGroup a, b;
    sqlite3 *fep = NULL, *app = NULL;
    sqlite3_vfs *v;
    sqlite3_file *f;
    sqlite3_stmt *stmt = NULL;
    Os32File denied;
    char byte;
    int rc, flags, out;
    CHECK(argc == 2);
    CHECK(os32_sqlite_init() == SQLITE_OK);
    setup_np2();
    build_database_media(argv[1]);
    vfs_register_fs(&iso9660_ops);
    CHECK(vfs_mount("/", "cd0", "iso9660") == VFS_OK);
    iso9660_ops.write_file = count_write;
    iso9660_ops.write_stream = count_stream;
    iso9660_ops.unlink = count_unlink;
    v = cd_group(OS32_SQLITE_RESIDENT, 0, &a);
    if (!strcmp(argv[1], "unpermitted")) {
        CHECK(os32_sqlite_group_open(&a, "/fep.db", SQLITE_OPEN_READONLY, &fep) != SQLITE_OK);
        close_db(&a, fep); CHECK(writes == 0); puts("PASS unpermitted RO CD"); return 0;
    }
    CHECK(vfs_sqlite_allow_immutable("/") == VFS_OK);
    rc = os32_sqlite_group_open(&a, "/fep.db", SQLITE_OPEN_READONLY, &fep);
    if (!strcmp(argv[1], "journal") || !strcmp(argv[1], "wal")) {
        CHECK(rc != SQLITE_OK); CHECK(vfs_count_sqlite(&a) == 0 && writes == 0);
        close_db(&a, fep); printf("PASS %s open refused write0\n", argv[1]); return 0;
    }
    CHECK(rc == SQLITE_OK);
    f = db_file(fep);
    CHECK(f->pMethods->xLock(f, SQLITE_LOCK_SHARED) == SQLITE_OK);
    CHECK(f->pMethods->xUnlock(f, SQLITE_LOCK_NONE) == SQLITE_OK);
    CHECK(f->pMethods->xLock(f, SQLITE_LOCK_RESERVED) == SQLITE_READONLY);
    CHECK(f->pMethods->xLock(f, SQLITE_LOCK_PENDING) == SQLITE_READONLY);
    CHECK(f->pMethods->xLock(f, SQLITE_LOCK_EXCLUSIVE) == SQLITE_READONLY);
    CHECK(f->pMethods->xWrite(f, "X", 1, 0) == SQLITE_READONLY);
    CHECK(f->pMethods->xTruncate(f, 0) == SQLITE_READONLY);
    flags = 88;
    CHECK(v->xOpen(v, "/new", &denied.base, SQLITE_OPEN_MAIN_DB |
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &flags) != SQLITE_OK && flags == 88);
    CHECK(v->xOpen(v, "/fep.db", &denied.base, SQLITE_OPEN_MAIN_DB |
        SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK);
    CHECK(v->xOpen(v, "/fep.db-journal", &denied.base, SQLITE_OPEN_MAIN_JOURNAL |
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK);
    CHECK(v->xDelete(v, "/fep.db", 0) == SQLITE_READONLY);
    cd_group(OS32_SQLITE_EXEC, 2, &b);
    CHECK(os32_sqlite_group_open(&b, "/alias.db", SQLITE_OPEN_READONLY, &app) == SQLITE_OK);
    CHECK(sqlite3_prepare_v2(app, "SELECT kanji FROM dict WHERE yomi='ki'", -1, &stmt, NULL) == SQLITE_OK);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(!strcmp((const char *)sqlite3_column_text(stmt, 0), "tree"));
    CHECK(sqlite3_finalize(stmt) == SQLITE_OK);
    cd_access(v);
    if (!strcmp(argv[1], "generation")) {
        CHECK(f->pMethods->xRead(f, &byte, 1, 0) == SQLITE_OK); /* cache populated */
        N.d[0].media = &g_media_b; N.d[0].ua = 1; /* Actual ATAPI UNIT ATTENTION. */
        CHECK(f->pMethods->xRead(f, &byte, 1, 0) == SQLITE_IOERR_READ);
        CHECK(f->pMethods->xLock(f, SQLITE_LOCK_SHARED) == SQLITE_IOERR_LOCK);
        CHECK((sqlite3_exec(app, "SELECT kanji FROM dict", NULL, NULL, NULL) & 0xff) == SQLITE_IOERR);
        out = 31;
        CHECK(v->xAccess(v, "/fep.db", SQLITE_ACCESS_EXISTS, &out) == SQLITE_IOERR_ACCESS && out == 31);
        CHECK(vfs_sqlite_allow_immutable("/") == VFS_ERR_IO); /* Cannot rearm old mount. */
    }
    if (!strcmp(argv[1], "remount")) {
        u32 old = vfs_path_mount_generation("/");
        vfs_umount("/");
        CHECK(vfs_mount("/", "cd0", "iso9660") == VFS_OK);
        CHECK(vfs_path_mount_generation("/") != old);
        CHECK(f->pMethods->xRead(f, &byte, 1, 0) == SQLITE_IOERR_READ);
        CHECK(vfs_sqlite_backend("/fep.db") == VFS_ERR_ROFS);
    }
    close_db(&b, app); close_db(&a, fep);
    CHECK(writes == 0);
    printf("PASS %s FEP/app RO duplicate + locks + SQL + write0\n", argv[1]);
    return 0;
}
