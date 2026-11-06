/* Stage I uses the existing real SQLite/MEMSYS5/VFS/FD RAM fixture. */
#include "fep_boundary_runtime.h"
#define IME_DICT_HOST_MAIN legacy_main
#include "ime_dict_host.c"
#include "kernel/ime/ime_conn.c"
#include "user_product.c"

/* Core probe is a separately tested boundary. This fixture supplies the
 * already validated legacy identity; SQL/open/cache/schema remain real. */
static int probe_error;
int fep_meta_probe(const char *path, FepMeta *out)
{
    CHECK(path && path[0]);
    if (probe_error) return probe_error;
    *out = (FepMeta){ .pos_version = FEP_DICT_POS_VERSION,
        .reserve_kb = FEP_DICT_RESERVE_KB, .cache_pages = FEP_DICT_CACHE_PAGES,
        .variant = 'M' };
    kstrncpy(out->dict_id, FEP_DICT_ID, sizeof(out->dict_id));
    return 0;
}
static sqlite3_stmt *fail_cursor;
static int step_calls, fail_step_at, step_error = SQLITE_IOERR;
static int stage_i_step(sqlite3_stmt *stmt)
{
    if (stmt == fail_cursor && fail_step_at && ++step_calls >= fail_step_at)
        return step_error;
    return sqlite3_step(stmt);
}
static struct { sqlite3_stmt *stmt; const char *p[2]; } borrowed[16];
static int borrow_slot(sqlite3_stmt *stmt)
{
    for (int i = 0; i < 16; i++) if (borrowed[i].stmt == stmt) return i;
    for (int i = 0; i < 16; i++) if (!borrowed[i].stmt) {
        borrowed[i].stmt = stmt; return i;
    }
    CHECK(0); return 0;
}
static int stage_i_bind_text(sqlite3_stmt *stmt, int i, const char *p, int n,
                             sqlite3_destructor_type d)
{
    int rc = sqlite3_bind_text(stmt, i, p, n, d);
    if (i <= 2 && rc == SQLITE_OK) borrowed[borrow_slot(stmt)].p[i - 1] = p;
    return rc;
}
static int stage_i_bind_null(sqlite3_stmt *stmt, int i)
{
    int rc = sqlite3_bind_null(stmt, i);
    if (i <= 2 && rc == SQLITE_OK) borrowed[borrow_slot(stmt)].p[i - 1] = 0;
    return rc;
}
static void sql(sqlite3 *db, const char *text)
{ CHECK(sqlite3_exec(db, text, 0, 0, 0) == SQLITE_OK); }
static int hits(FepStore *s, IME_Result *out)
{ return fep_store_search(s, "か", out, IME_MAX_RESULTS); }
static void no_borrow(FepConn *c)
{
    sqlite3_stmt *stmts[] = { c->exact, c->prefix, c->learn };
    for (int i = 0; i < 3; i++) if (stmts[i]) {
        CHECK(!sqlite3_stmt_busy(stmts[i]));
        int slot = borrow_slot(stmts[i]);
        CHECK(!borrowed[slot].p[0] && !borrowed[slot].p[1]);
    }
}
int main(void)
{
    static FepStore s;
    static IME_Result out[IME_MAX_RESULTS];
    IME_UserEntry entries[4];
    CHECK(os32_sqlite_init() == SQLITE_OK);
    fixture_init();
    /* The shared admin helpers still serve J's active legacy facade. */
    static IME_Dict legacy;
    make_dict("/db/legacy.db", "旧");
    CHECK(ime_dict_open(&legacy, "/db/legacy.db") == 0);
    ime_dict_learn(&legacy, "か", "旧");
    CHECK(ime_user_list(&legacy, 0, entries, 4) == 1);
    CHECK(ime_user_export(&legacy, "/db/legacy.csv") == 0);
    CHECK(ime_user_delete(&legacy, "か", "旧") == 0);
    CHECK(ime_user_clear(&legacy) == 0);
    CHECK(ime_dict_close(&legacy) == 0);
    make_dict(FEP_DICT_PATH_M, "蚊");
    sqlite3 *db = 0;
    CHECK(setup_open(FEP_DICT_PATH_M, &db, SQLITE_OPEN_READWRITE, 0) == SQLITE_OK);
    sql(db, "INSERT INTO dict VALUES('か','同',8,-10001);"
            "INSERT INTO dict VALUES('か','低',9,-20000);"
            "INSERT INTO dict VALUES('かなか','前',10,800);"
            "INSERT INTO dict VALUES('かなかい','先',11,400);");
    CHECK(setup_close(db) == SQLITE_OK);
    /* An explicit smaller settings reservation is not a switch. */
    s.boot_reserve_kb = FEP_DICT_MIN_RESERVE_KB;
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
    CHECK(s.user_enabled && fep_store_live_connections(&s) == 2);
    CHECK(sqlite3_exec(s.system.db, "DELETE FROM dict", 0, 0, 0) == SQLITE_READONLY);
    fep_store_learn(&s, "か", "同");
    CHECK(hits(&s, out) == 3);
    CHECK(!strcmp(out[0].kanji, "低") && out[0].pos_id == 9);
    CHECK(!strcmp(out[1].kanji, "同") && out[1].pos_id == 0);
    CHECK(!strcmp(out[2].kanji, "蚊") && out[2].cost == 310);
    CHECK(fep_store_search(&s, "かなか", out, IME_MAX_RESULTS) == 2);
    CHECK(!strcmp(out[0].kanji, "前") && out[0].cost == 300);
    no_borrow(&s.system); no_borrow(&s.user);
    CHECK(fep_store_user_list(&s, "か", entries, 4) == 1);
    CHECK(entries[0].freq == 1 && !strcmp(entries[0].kanji, "同"));
    CHECK(fep_store_user_export(&s, "/db/export.csv") == 0);
    FixtureFile *csv = fixture_find("/db/export.csv", 0);
    CHECK(csv && csv->size == kstrlen("か,同,1\n") &&
          !memcmp(csv->data, "か,同,1\n", csv->size));
    CHECK(fep_store_user_delete(&s, "か", "同") == 0);
    CHECK(fep_store_user_list(&s, 0, entries, 4) == 0);
    fep_store_learn(&s, "か", "同");
    /* Mid-cursor IO must discard learned rows, keep the system handle and
     * retry system alone. Repeated failure must not reopen user indefinitely. */
    void *system_db = s.system.db;
    fail_cursor = s.user.exact; fail_step_at = 2; step_calls = 0;
    CHECK(hits(&s, out) == 3);
    CHECK(out[1].pos_id == 8 && s.system.db == system_db);
    CHECK(s.user.io_retried == 1);
    fail_cursor = s.user.exact; step_calls = 0;
    u32 generation = s.user.group.generation;
    CHECK(hits(&s, out) == 3 && s.user.group.generation == generation);
    fail_step_at = 0;
    CHECK(hits(&s, out) == 3 && s.user.io_retried == 0);
    /* Non-IO cursor failures also discard the complete partial merge. */
    fail_cursor = s.user.exact; fail_step_at = 2; step_calls = 0; step_error = SQLITE_ERROR;
    CHECK(hits(&s, out) == 3 && out[1].pos_id == 8);
    fail_step_at = 0;
    /* System stale recovery with unchanged identity retains user. */
    void *user_db = s.user.db;
    u32 old_generation = s.system.group.generation;
    open_files[os32_sqlite_db_fd(s.system.db)].stale = 1;
    CHECK(hits(&s, out) == 3 && s.user.db == user_db);
    CHECK(s.system.group.generation != old_generation);
    system_db = s.system.db;
    /* Stale user gets its own reopen, without touching system. */
    open_files[os32_sqlite_db_fd(s.user.db)].stale = 1;
    CHECK(fep_store_user_list(&s, 0, entries, 4) == 1);
    CHECK(s.system.db == system_db && !s.user.io_retried);
    /* Hot user journal prevents SQL/replay and leaves system usable. */
    open_files[os32_sqlite_db_fd(s.user.db)].stale = 1;
    CHECK(fixture_create(0, "/etc/fep_user.db-journal", "HOT!", 4) == 0);
    CHECK(hits(&s, out) == 3 && !s.user_enabled && !s.user.db);
    CHECK(fep_store_live_connections(&s) == 1);
    CHECK(fixture_find("/etc/fep_user.db-journal", 0)->size == 4);
    CHECK(fep_store_close(&s) == 0 && fep_store_live_connections(&s) == 0);
    CHECK(fixture_rm("/etc/fep_user.db-journal") == 0);
    /* System hot journal must stop conversion without closing user. */
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
    user_db = s.user.db;
    open_files[os32_sqlite_db_fd(s.system.db)].stale = 1;
    CHECK(fixture_create(0, "/db/fep.db-journal", "HOT!", 4) == 0);
    CHECK(hits(&s, out) == 0 && !s.system.db && s.user.db == user_db);
    CHECK(fep_store_close(&s) == 0);
    CHECK(fixture_rm("/db/fep.db-journal") == 0);
    char overlong[OS32_MAX_PATH + 1];
    memset(overlong, 'x', sizeof(overlong)); overlong[sizeof(overlong) - 1] = 0;
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, overlong) == 0);
    CHECK(!s.user_enabled && !s.user.path[0] && hits(&s, out) == 3);
    CHECK(fep_store_close(&s) == 0);
    /* Full spelling dedup occurs before display truncation and never refills
     * the merged 32-row limit after removing duplicates. */
    db = 0;
    CHECK(setup_open(FEP_DICT_PATH_M, &db, SQLITE_OPEN_READWRITE, 0) == SQLITE_OK);
    sql(db, "DELETE FROM dict;"
        "INSERT INTO dict VALUES('か','abcdefghijklmnopqrstuvwxyz12345A',12,1);"
        "INSERT INTO dict VALUES('か','abcdefghijklmnopqrstuvwxyz12345B',13,2);");
    CHECK(setup_close(db) == SQLITE_OK);
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
    CHECK(fep_store_user_clear(&s) == 0);
    fep_store_learn(&s, "か", "abcdefghijklmnopqrstuvwxyz12345A");
    CHECK(hits(&s, out) == 2 && out[0].pos_id == 0 && out[1].pos_id == 13);
    no_borrow(&s.system); no_borrow(&s.user);
    CHECK(fep_store_close(&s) == 0);
    db = 0;
    CHECK(setup_open(FEP_DICT_PATH_M, &db, SQLITE_OPEN_READWRITE, 0) == SQLITE_OK);
    sql(db, "DELETE FROM dict");
    for (int i = 0; i < IME_MAX_RESULTS; i++)
        sql(db, "INSERT INTO dict VALUES('か','重',14,-20000)");
    sql(db, "INSERT INTO dict VALUES('か','外',15,0)");
    CHECK(setup_close(db) == SQLITE_OK);
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
    CHECK(fep_store_user_clear(&s) == 0);
    CHECK(hits(&s, out) == 1 && !strcmp(out[0].kanji, "重"));
    fep_store_learn(&s, "か", "重");
    CHECK(fep_store_close(&s) == 0);
    /* Existing-user RO failure must preserve system conversion and data. */
    fixture_readonly = 1;
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
    CHECK(!s.user_enabled && fep_store_live_connections(&s) == 1);
    CHECK(hits(&s, out) == 1);
    fep_store_learn(&s, "か", "書けない");
    CHECK(fep_store_user_list(&s, 0, entries, 4) < 0);
    CHECK(fep_store_close(&s) == 0);
    fixture_readonly = 0;
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
    CHECK(s.user_enabled && fep_store_live_connections(&s) == 2);
    CHECK(fep_store_user_list(&s, 0, entries, 4) == 1);
    CHECK(!strcmp(entries[0].kanji, "重") && entries[0].freq == 1);
    CHECK(fep_store_close(&s) == 0);
    fixture_readonly = 1;
    /* Missing user on RO media also leaves only system conversion. */
    CHECK(fixture_rm(FEP_USER_PATH) == 0);
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
    CHECK(!s.user_enabled && fep_store_live_connections(&s) == 1 && hits(&s, out) == 1);
    CHECK(fep_store_close(&s) == 0);
    fixture_readonly = 0;
    probe_error = OS32_ERR_INVAL;
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == OS32_ERR_INVAL);
    CHECK(fep_store_live_connections(&s) == 0);
    probe_error = 0;
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
    /* Close both sides even if user has an unowned statement: quarantine is
     * retained, counted, and cannot be reset by another open. */
    sqlite3_stmt *unowned;
    CHECK(sqlite3_prepare_v2(s.user.db, "SELECT 1", -1, &unowned, 0) == SQLITE_OK);
    CHECK(fep_store_close(&s) == OS32_ERR_BUSY);
    CHECK(!s.system.db && s.user.isolated && fep_store_live_connections(&s) == 1);
    CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == OS32_ERR_BUSY);
    puts("PASS store merge/limit/spelling/borrow/recovery/admin/RO/isolation");
    return 0;
}
