/* ======================================================================== */
/*  OS32_SQLITE_TEST.C — 段階的テスト                                        */
/*  テストG4: open + PRAGMA + exec(CREATE TABLE) + close                     */
/* ======================================================================== */

#include "os32_sqlite_config.h"
#include "sqlite3.h"
#include "os32_sqlite_outer.h"

int os32_sqlite_test(void)
{
    sqlite3 *db;
    int rc, close_rc;
    Os32SqliteGroup group;

    db = 0;
    rc = os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT, 0, &group);
    if (rc != SQLITE_OK) return -1;
    rc = os32_sqlite_group_open(&group, ":memory:", SQLITE_OPEN_READWRITE, &db);
    if (rc != 0) goto close;

    /* PRAGMA */
    sqlite3_exec(db, "PRAGMA journal_mode=OFF", 0, 0, 0);
    sqlite3_exec(db, "PRAGMA temp_store=MEMORY", 0, 0, 0);

    /* CREATE TABLE */
    rc = sqlite3_exec(db,
        "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)",
        0, 0, 0);

    /* close */
close:
    os32_sqlite_group_begin_close(&group);
    close_rc = db ? sqlite3_close(db) : SQLITE_OK;
    if (close_rc == SQLITE_OK) db = 0;
    close_rc = os32_sqlite_group_finish(&group, close_rc);
    if (close_rc != SQLITE_OK) return -1;

    return rc;
}
