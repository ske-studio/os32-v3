/* ======================================================================== */
/*  OS32_SQLITE_TEST.C — 段階的テスト                                        */
/*  テストG4: open + PRAGMA + exec(CREATE TABLE) + close                     */
/* ======================================================================== */

#include "os32_sqlite_config.h"
#include "sqlite3.h"
#include "os32_sqlite_outer.h"
#include "types.h"

#define TVRAM_CHAR ((volatile u16 *)0xA0000UL)
#define TVRAM_ATTR ((volatile u16 *)0xA2000UL)

static void tv_put(int row, int col, char c, u8 attr)
{
    int pos = row * 80 + col;
    TVRAM_CHAR[pos] = (u16)(u8)c;
    TVRAM_ATTR[pos] = (u16)attr;
}

int os32_sqlite_test(void)
{
    sqlite3 *db;
    int rc, close_rc;
    Os32SqliteGroup group;

    tv_put(20, 0, 'A', 0x0E);

    db = 0;
    rc = os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT, 0, &group);
    if (rc != SQLITE_OK) return -1;
    rc = os32_sqlite_group_open(&group, ":memory:", SQLITE_OPEN_READWRITE, &db);
    tv_put(20, 1, (rc == 0) ? 'O' : 'X', (rc == 0) ? 0x0A : 0x02);
    if (rc != 0) goto close;

    /* PRAGMA */
    sqlite3_exec(db, "PRAGMA journal_mode=OFF", 0, 0, 0);
    sqlite3_exec(db, "PRAGMA temp_store=MEMORY", 0, 0, 0);
    tv_put(20, 2, 'P', 0x0A);

    /* CREATE TABLE */
    tv_put(20, 3, 'C', 0x0E);
    rc = sqlite3_exec(db,
        "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)",
        0, 0, 0);
    tv_put(20, 4, (rc == 0) ? 'O' : 'X', (rc == 0) ? 0x0A : 0x02);

    /* close */
    tv_put(20, 5, 'D', 0x0E);
close:
    os32_sqlite_group_begin_close(&group);
    close_rc = db ? sqlite3_close(db) : SQLITE_OK;
    if (close_rc == SQLITE_OK) db = 0;
    close_rc = os32_sqlite_group_finish(&group, close_rc);
    if (close_rc != SQLITE_OK) return -1;
    tv_put(20, 6, 'K', 0x0A);

    return rc;
}
