/* Real bundled SQLite with os32_sqlite_config.h, ILP32 MEMSYS5.
 * Only the VFS is a seam: :memory: must never open a host file.
 * guest_inputs.inc contains the guest's actual macros and the candidate count
 * regenerated from IPADIC with the distribution's M dictionary builder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"
#include "os32_kapi_shared.h"
#include "guest_inputs.inc"

#define CHECK(c) do { if (!(c)) { puts("FAIL guest inputs: " #c); return 1; } } while (0)
static union { double align; unsigned char bytes[SQLITE_MEMSYS5_SIZE]; } pool;
static int file_opens;

static int no_open(sqlite3_vfs *v, const char *p, sqlite3_file *f, int flags, int *out)
{
    (void)v; (void)p; (void)f; (void)flags; (void)out;
    file_opens++;
    return SQLITE_CANTOPEN;
}
static int full_path(sqlite3_vfs *v, const char *p, int n, char *out)
{
    (void)v;
    if ((int)strlen(p) >= n) return SQLITE_CANTOPEN;
    strcpy(out, p);
    return SQLITE_OK;
}
static int random_bytes(sqlite3_vfs *v, int n, char *out)
{
    (void)v;
    memset(out, 0x42, n);
    return n;
}
int sqlite3_os_init(void)
{
    static sqlite3_vfs vfs = {
        .iVersion = 1, .szOsFile = sizeof(sqlite3_file), .mxPathname = 256,
        .zName = "raw-wait-memory", .xOpen = no_open, .xFullPathname = full_path,
        .xRandomness = random_bytes
    };
    return sqlite3_vfs_register(&vfs, 1);
}
int sqlite3_os_end(void) { return SQLITE_OK; }
int sqlite3DbIsNamed(void *db, int i, const char *name)
{
    (void)db; (void)name;
    return i == 0; /* Same OMIT_ATTACH bridge as os32_sqlite_vfs.c. */
}

int main(void)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    const char *tail = NULL;
    CHECK(sizeof(RAW_SLOW_SQL) <= DB_SQL_MAX_BYTES);
    CHECK(RAW_DICT_CANDIDATES > 0);
    CHECK(sqlite3_config(SQLITE_CONFIG_HEAP, pool.bytes, sizeof(pool.bytes), 32) == SQLITE_OK);
    CHECK(sqlite3_initialize() == SQLITE_OK);
    CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
    CHECK(sqlite3_prepare_v2(db, RAW_SLOW_SQL, -1, &stmt, &tail) == SQLITE_OK);
    CHECK(stmt && tail && !*tail);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW);
    /* 4^12 rows, sum of twelve independent values 1..4. Require real VM
     * work rather than relying on host timing or a trivially folded query. */
    CHECK(sqlite3_column_int64(stmt, 0) == 503316480);
    int steps = sqlite3_stmt_status(stmt, SQLITE_STMTSTATUS_VM_STEP, 0);
    CHECK(steps > 16777216);
    CHECK(sqlite3_step(stmt) == SQLITE_DONE);
    CHECK(sqlite3_finalize(stmt) == SQLITE_OK);
    CHECK(sqlite3_close(db) == SQLITE_OK);
    CHECK(sqlite3_shutdown() == SQLITE_OK);
    CHECK(!file_opens && sqlite3_memory_used() == 0);
    printf("PASS guest inputs: OS32 SQLite prepare/step, VM steps=%d, M dictionary yomi=%s candidates=%d\n",
           steps, RAW_IME_YOMI, RAW_DICT_CANDIDATES);
    return 0;
}
