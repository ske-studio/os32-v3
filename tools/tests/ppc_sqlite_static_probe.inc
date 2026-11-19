#include "vfs.h"

/* The sync mutant consumes this once during the INSERT commit. */
volatile int sqlite_probe_commit;

static int probe_open(Os32SqliteGroup *group, sqlite3 **db, int flags)
{
    int rc = os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT, 0, group);
    if (rc != SQLITE_OK) return rc;
    rc = os32_sqlite_group_open(group, "/tmp/P.DB", flags, db);
    if (rc != SQLITE_OK) {
        os32_sqlite_group_begin_close(group);
        os32_sqlite_group_finish(group, *db ? sqlite3_close(*db) : SQLITE_OK);
    }
    return rc;
}

static int probe_close(Os32SqliteGroup *group, sqlite3 *db)
{
    os32_sqlite_group_begin_close(group);
    return os32_sqlite_group_finish(group, sqlite3_close(db));
}

static void sqlite_static_probe(void)
{
    Os32SqliteGroup group;
    sqlite3 *db = 0;
    int commit = -1, reopen = -1, rows = -1;
    int create = probe_open(&group, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    if (create == SQLITE_OK) {
        create = sqlite3_exec(db, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;"
                              "CREATE TABLE t(v INTEGER)", 0, 0, 0);
        kprintf(KLOG_ATTR, "[sqlite] probe create=%d\n", create);
        if (create == SQLITE_OK) {
            commit = sqlite3_exec(db, "BEGIN; INSERT INTO t VALUES(1)", 0, 0, 0);
            if (commit == SQLITE_OK) {
                OS32_Stat st;
                int present = vfs_stat("/tmp/P.DB-journal", &st) == VFS_OK;
                kprintf(KLOG_ATTR, "[sqlite] probe journal=%d\n", present);
                sqlite_probe_commit = 1;
                commit = sqlite3_exec(db, "COMMIT", 0, 0, 0);
                sqlite_probe_commit = 0;
            }
        }
        reopen = probe_close(&group, db);
        db = 0;
        if (reopen == SQLITE_OK) reopen = probe_open(&group, &db, SQLITE_OPEN_READWRITE);
        if (reopen == SQLITE_OK) {
            sqlite3_stmt *stmt = 0;
            if (sqlite3_prepare_v2(db, "SELECT count(*) FROM t", -1, &stmt, 0) == SQLITE_OK &&
                sqlite3_step(stmt) == SQLITE_ROW)
                rows = sqlite3_column_int(stmt, 0);
            if (stmt && sqlite3_finalize(stmt) != SQLITE_OK) rows = -1;
            if (probe_close(&group, db) != SQLITE_OK) rows = -1;
        }
    }
    OS32_Stat st;
    int stat_rc = vfs_stat("/tmp/P.DB-journal", &st);
    int journal = stat_rc == VFS_ERR_NOTFOUND ? 0 : stat_rc == VFS_OK ? 1 : stat_rc;
    kprintf(KLOG_ATTR, "[sqlite] probe create=%d commit=%d reopen=%d rows=%d journal=%d\n",
            create, commit, reopen, rows, journal);
}
