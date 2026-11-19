/* Real SQLite, VFS mount routing, FD table and ramfs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs/vfs_device.h"
#include "fs/ramfs/ramfs.h"
#include "sqlite3.h"
#include "os32_sqlite_vfs.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static int fail_alloc;
void *kmalloc(u32 n) { return fail_alloc ? NULL : malloc(n); }
void *kzalloc(u32 n) { return fail_alloc ? NULL : calloc(1, n); }
void kfree(void *p) { free(p); }
volatile u32 tick_count;
u32 sys_time(void) { return 0; }
void kprintf(u8 color, const char *fmt, ...) {}
int res_owner_get(void) { return 1; }
struct VfsStdinRead;
struct VfsStdinRead *exec_stdin_state(void) { return NULL; }
int input_getchar_kapi(void) { return 0; }
int fd_is_redirected(int fd) { return 0; }
u16 fd_redirect_ifmt(int fd, int *file) { return OS_S_IFCHR; }
int fd_redirect_read(int fd, void *p, u32 n) { return -1; }
int fd_redirect_write(int fd, const void *p, u32 n) { return -1; }
void console_write(const char *p, u32 n, u8 attr) {}

static void storage(void)
{
    static char block[RAMFS_FILE_MAX];
    char data[8];
    CHECK(vfs_write("/tmp/f", "abc", 3) == 3);
    int a = vfs_open("/tmp/f", O_RDWR);
    int b = vfs_open("/tmp/f", O_RDWR);
    CHECK(a >= 0 && b >= 0 && vfs_sync_fd(a) == VFS_OK);
    CHECK(vfs_seek(a, RAMFS_GROW, SEEK_SET) == RAMFS_GROW);
    fail_alloc = 1;
    CHECK(vfs_write_fd(a, "z", 1) == VFS_ERR_NOSPC);
    fail_alloc = 0;
    CHECK(vfs_read_fd(b, data, sizeof(data)) == 3 && !memcmp(data, "abc", 3));
    CHECK(vfs_write_fd(a, "z", 1) == 1);
    CHECK(vfs_seek(a, RAMFS_GROW - 2, SEEK_SET) == RAMFS_GROW - 2);
    CHECK(vfs_read_fd(a, data, 3) == 3 && !memcmp(data, "\0\0z", 3));
    CHECK(vfs_seek(b, 0, SEEK_SET) == 0 && vfs_write_fd(b, "X", 1) == 1);
    CHECK(vfs_seek(a, 0, SEEK_SET) == 0 && vfs_read_fd(a, data, 3) == 3 && !memcmp(data, "Xbc", 3));
    CHECK(vfs_write("/tmp/f", block, sizeof(block)) == sizeof(block));
    CHECK(vfs_write("/tmp/g", block, sizeof(block)) == sizeof(block));
    CHECK(vfs_write("/tmp/h", "", 0) == VFS_ERR_NOSPC);
    CHECK(vfs_resize_fd(a, RAMFS_FILE_MAX - RAMFS_GROW) == VFS_OK);
    CHECK(vfs_write("/tmp/h", "x", 1) == 1);
    CHECK(vfs_resize_fd(a, RAMFS_FILE_MAX) == VFS_ERR_NOSYS);
    CHECK(vfs_rm("/tmp/h") == VFS_OK);
    int trunc = vfs_open("/tmp/f", O_RDWR | O_TRUNC);
    CHECK(trunc >= 0);
    vfs_close(trunc);
    CHECK(vfs_write("/tmp/h", block, sizeof(block) - RAMFS_GROW) == sizeof(block) - RAMFS_GROW);
    CHECK(vfs_rm("/tmp/f") == VFS_OK && vfs_write("/tmp/f", "new", 3) == 3);
    CHECK(vfs_read_fd(a, data, 1) < 0 && vfs_write_fd(b, "x", 1) < 0);
    CHECK(vfs_read("/tmp/f", data, 3) == 3 && !memcmp(data, "new", 3));
    vfs_close(a);
    vfs_close(b);
    CHECK(!vfs_rm("/tmp/f") && !vfs_rm("/tmp/g") && !vfs_rm("/tmp/h"));
}

static sqlite3 *open_db(Os32SqliteGroup *group, const char *name)
{
    sqlite3 *db = NULL;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, group) == SQLITE_OK);
    CHECK(os32_sqlite_group_open(group, name, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &db) == SQLITE_OK);
    return db;
}

static void close_db(Os32SqliteGroup *group, sqlite3 *db)
{
    CHECK(!os32_sqlite_group_begin_close(group));
    CHECK(!sqlite3_close(db));
    CHECK(!os32_sqlite_group_finish(group, SQLITE_OK));
}

static void scalar(sqlite3 *db, const char *sql, const char *expected)
{
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc) fprintf(stderr, "%s: %s (%d)\n", sql, sqlite3_errmsg(db), rc);
    CHECK(rc == SQLITE_OK);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(!strcmp((const char *)sqlite3_column_text(stmt, 0), expected));
    CHECK(!sqlite3_finalize(stmt));
}

static int sql(sqlite3 *db, const char *statement)
{
    return sqlite3_exec(db, statement, NULL, NULL, NULL);
}

int main(void)
{
    static const struct vfs_device_name names[] = {{"rd", VFS_NAME_DIGIT_PREFIX, 5, 0, 2}};
    vfs_device_names_register(names, 1);
    CHECK(!vfs_register_fs(&ramfs_ops) && !vfs_mount("/tmp", "rd1", "ramfs"));
    CHECK(!os32_sqlite_init());
    Os32SqliteGroup g, other;
    sqlite3 *db = open_db(&g, "/tmp/T.DB"), *duplicate = NULL;
    CHECK(!os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &other));
    CHECK(os32_sqlite_group_open(&other, "/tmp/T.DB", SQLITE_OPEN_READWRITE, &duplicate) != SQLITE_OK);
    CHECK(!os32_sqlite_group_begin_close(&other));
    CHECK(!sqlite3_close(duplicate) && !os32_sqlite_group_finish(&other, SQLITE_OK));
    CHECK(!sql(db, "CREATE TABLE t(x); BEGIN; INSERT INTO t VALUES(17); COMMIT;"
                   "BEGIN; INSERT INTO t VALUES(99); ROLLBACK"));
    close_db(&g, db);
    const char *long_name = "/tmp/123456789012345678901234";
    db = open_db(&g, "/tmp/12345678901234567890123");
    scalar(db, "PRAGMA journal_mode", "delete");
    CHECK(!sql(db, "CREATE TABLE t(x)"));
    close_db(&g, db);
    db = open_db(&g, long_name);
    scalar(db, "PRAGMA journal_mode", "delete");
    CHECK(sql(db, "CREATE TABLE t(x)") != SQLITE_OK);
    close_db(&g, db);
    OS32_Stat st;
    CHECK(!vfs_stat(long_name, &st) && st.st_size == 0);
    puts("GREEN 23-byte CREATE, 24-byte failed CREATE and zero-byte file after close");
    db = open_db(&g, "/tmp/T.DB");
    scalar(db, "SELECT sum(x) FROM t", "17");
    static char filler[RAMFS_FILE_MAX];
    CHECK(vfs_write("/tmp/fill", filler, sizeof(filler)) == sizeof(filler));
    CHECK(vfs_write("/tmp/pad", filler, 12 * 1024) == 12 * 1024);
    CHECK(!sql(db, "BEGIN; INSERT INTO t VALUES(zeroblob(18000))"));
    CHECK(sql(db, "COMMIT") != SQLITE_OK);
    puts("GREEN full ramfs rejected COMMIT");
    close_db(&g, db);
    db = open_db(&g, "/tmp/T.DB");
    scalar(db, "SELECT count(*) || '/' || sum(x) FROM t", "1/17");
    CHECK(!vfs_rm("/tmp/fill"));
    CHECK(!sql(db, "INSERT INTO t VALUES(18)"));
    close_db(&g, db);
    CHECK(!vfs_rm("/tmp/T.DB") && !vfs_rm("/tmp/pad"));
    CHECK(!vfs_rm(long_name) && !vfs_rm("/tmp/12345678901234567890123"));
    storage();
    puts("GREEN ramfs SQLite transactions, name limits, capacity and FD identity");
    return 0;
}
