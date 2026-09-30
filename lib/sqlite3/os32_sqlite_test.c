/* ======================================================================== */
/*  OS32_SQLITE_TEST.C — 段階的テスト                                        */
/*  テストG4: open + PRAGMA + exec(CREATE TABLE) + close                     */
/* ======================================================================== */

#include "os32_sqlite_config.h"
#include "sqlite3.h"
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
    int rc;

    tv_put(20, 0, 'A', 0x0E);

    db = 0;
    rc = sqlite3_open(":memory:", &db);
    tv_put(20, 1, (rc == 0) ? 'O' : 'X', (rc == 0) ? 0x0A : 0x02);
    if (rc != 0) return -1;

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
    sqlite3_close(db);
    tv_put(20, 6, 'K', 0x0A);

    return rc;
}
