/* Unit scenarios start at BSS through the shared stage-A fixture. */
#include "kernel/ime/ime_user_store.c"
#include "kernel/ime/ime_conn.c"
#include "tools/tests/fep_store_fixture.c"
#include <stdarg.h>

static FepTestFixture f;
/* A scenario has fewer than 128 outer calls; capacity is checked on append. */
#define SCRIPT_CAP 128
static FepTestReply script[SCRIPT_CAP];
static u32 used;
static int bad, row_type, row_bytes, available, flags_seen, writes;
static int incomplete_warnings;
static char row[OS32_MAX_PATH];
static void add(enum FepTestOp op, int rc, const void *p, u32 n)
{
    if (used == SCRIPT_CAP) { bad = 1; return; }
    script[used++] = (FepTestReply){op, rc, p, n};
}
static const FepTestReply *next(enum FepTestOp op) { return fep_test_next(&f, op); }
static void sql_check(const FepTestReply *r, const char *sql)
{
    if (r->data && kstrcmp(r->data, sql)) bad = 1;
}
static void invalidate(void) { for (u32 i = 0; i < sizeof(row); i++) row[i] = '!'; }
int sqlite_engine_available(int io) { return available && io; }
int vfs_stat(const char *path, OS32_Stat *st)
{
    if (kstrcmp(path, f.store.user.path)) bad = 1;
    const FepTestReply *r = next(FEP_TEST_HEADER_READ);
    st->st_mode = OS_S_IFREG; st->st_size = r->bytes; return r->rc;
}
int os32_sqlite_outer_group_acquire(int kind, int owner, Os32SqliteGroup *g)
{
    const FepTestReply *r = next(FEP_TEST_ACQUIRE);
    if (kind != OS32_SQLITE_RESIDENT || owner) bad = 1;
    if (r->rc) return r->rc;
    int rc = fep_test_resident_take(&f);
    if (!rc) g->generation = 1;
    return rc;
}
int os32_sqlite_outer_group_open(const Os32SqliteGroup *g, const char *path,
                                int flags, sqlite3 **out)
{
    const FepTestReply *r = next(FEP_TEST_OPEN);
    if (!g->generation || kstrcmp(path, f.store.user.path)) bad = 1;
    flags_seen = flags; *out = (sqlite3 *)r->data; return r->rc;
}
int os32_sqlite_outer_db_fd(void *db) { if (db != (void *)1) bad = 1; return 7; }
int vfs_fd_is_stale(int fd) { if (fd != 7) bad = 1; return next(FEP_TEST_STALE)->rc; }
int os32_sqlite_prepare_v2(sqlite3 *db, const char *sql, int n,
                          sqlite3_stmt **out, const char **tail)
{
    (void)n; (void)tail;
    if (db != (void *)1) bad = 1;
    const FepTestReply *r = next(FEP_TEST_PREPARE); sql_check(r, sql);
    *out = r->rc ? 0 : (void *)2; return r->rc;
}
int os32_sqlite_exec(sqlite3 *db, const char *sql, SqliteExecCallback cb,
                    void *arg, char **error)
{
    if (db != (void *)1 || cb || arg || error) bad = 1;
    writes++;
    const FepTestReply *r = next(FEP_TEST_PREPARE); sql_check(r, sql); return r->rc;
}
int os32_sqlite_bind_text(sqlite3_stmt *stmt, int i, const char *p, int n,
                         SqliteDestructor destroy)
{
    if (stmt != (void *)2 || n != -1 || destroy != SQLITE_STATIC) bad = 1;
    const FepTestReply *r = next(FEP_TEST_BIND);
    if (i != (int)r->bytes || (r->data && kstrcmp(p, r->data))) bad = 1;
    return r->rc;
}
int os32_sqlite_step(sqlite3_stmt *stmt)
{ if (stmt != (void *)2) bad = 1; invalidate(); return next(FEP_TEST_STEP)->rc; }
int os32_sqlite_column_type(sqlite3_stmt *stmt, int col)
{
    if (stmt != (void *)2 || col) bad = 1;
    const FepTestReply *r = next(FEP_TEST_COLUMN);
    row_type = r->rc; row_bytes = (int)r->bytes;
    if (r->data && r->bytes < sizeof(row)) kmemcpy(row, r->data, r->bytes);
    return row_type;
}
const unsigned char *os32_sqlite_column_text(sqlite3_stmt *stmt, int col)
{ (void)stmt; (void)col; return (const unsigned char *)row; }
int os32_sqlite_column_bytes(sqlite3_stmt *stmt, int col)
{ (void)stmt; (void)col; return row_bytes; }
int os32_sqlite_column_int(sqlite3_stmt *stmt, int col)
{ (void)stmt; (void)col; return next(FEP_TEST_COLUMN)->rc; }
int os32_sqlite_reset(sqlite3_stmt *stmt)
{ (void)stmt; invalidate(); return next(FEP_TEST_RESET)->rc; }
int os32_sqlite_finalize(sqlite3_stmt *stmt)
{ if (stmt != (void *)2) bad = 1; invalidate(); return next(FEP_TEST_FINALIZE)->rc; }
int os32_sqlite_outer_group_begin_close(const Os32SqliteGroup *g)
{ if (!g->generation) bad = 1; return next(FEP_TEST_BEGIN_CLOSE)->rc; }
int os32_sqlite_close(sqlite3 *db)
{ if (db != (void *)1) bad = 1; return next(FEP_TEST_CLOSE)->rc; }
int os32_sqlite_outer_group_finish(const Os32SqliteGroup *g, int rc)
{
    if (!g->generation) bad = 1;
    const FepTestReply *r = next(FEP_TEST_FINISH);
    if (rc != (int)r->bytes) bad = 1;
    fep_test_resident_finish(&f, r->rc != SQLITE_OK); return r->rc;
}
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (!kstrcmp(fmt, "IME: %s is incomplete; delete it to recreate\r\n")) {
        va_list ap; va_start(ap, fmt);
        if (kstrcmp(va_arg(ap, const char *), f.store.user.path)) bad = 1;
        va_end(ap);
        incomplete_warnings++;
    }
}
/* Only the product's %u and literal pragma formats are needed here. */
char *os32_sqlite_snprintf(int cap, char *out, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    u32 n = va_arg(ap, u32); va_end(ap);
    char digits[U32_DEC_BUFSIZE]; int count = 0, j = 0;
    do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
    for (int i = 0; fmt[i] && j < cap - 1; i++) {
        if (fmt[i] == '%' && fmt[i+1] == 'u') {
            i++; while (count && j < cap - 1) out[j++] = digits[--count];
        } else out[j++] = fmt[i];
    }
    out[j] = 0; return out;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void setup(void)
{
    fep_test_reset(&f); used = 0; bad = flags_seen = writes = 0; available = 1;
    incomplete_warnings = 0;
    /* Inputs supplied by store-open after the real zero initialization. */
    kstrncpy(f.store.meta.dict_id, FEP_DICT_ID, sizeof(f.store.meta.dict_id));
    f.store.meta.pos_version = FEP_DICT_POS_VERSION;
    kstrncpy(f.store.user.path, FEP_USER_PATH, sizeof(f.store.user.path));
    f.store.system.db = (void *)9; f.store.system.group.generation = 42;
    if (fep_test_resident_take(&f) != SQLITE_OK) bad = 1;
}
static void opening(int create)
{
    add(FEP_TEST_HEADER_READ, create ? OS32_ERR_NOTFOUND : 0, 0, create ? 0 : 8192);
    add(FEP_TEST_ACQUIRE, SQLITE_OK, 0, 0);
    add(FEP_TEST_OPEN, SQLITE_OK, (void *)1, 0);
    add(FEP_TEST_STALE, 0, 0, 0);
}
static void exec_sql(const char *sql, int rc) { add(FEP_TEST_PREPARE, rc, sql, 0); }
static void page(int size)
{
    exec_sql("PRAGMA page_size", SQLITE_OK);
    add(FEP_TEST_STEP, SQLITE_ROW, 0, 0); add(FEP_TEST_COLUMN, size, 0, 0);
    if (size == 1024) add(FEP_TEST_STEP, SQLITE_DONE, 0, 0); /* design §3 */
    add(FEP_TEST_FINALIZE, SQLITE_OK, 0, 0);
}
static void closing(int begin, int close_rc, int finish)
{
    add(FEP_TEST_BEGIN_CLOSE, begin, 0, 0);
    if (begin) return;
    add(FEP_TEST_CLOSE, close_rc, 0, 0); add(FEP_TEST_FINISH, finish, 0, close_rc);
}
static void metadata(int fail_key, const char *value, int type, int step_rc)
{
    const char *keys[] = {"format_version", "kind", "dict_id", "pos_table_version", "created"};
    const char *values[] = {"1", "user", "ipadic-2.7.0", "1", "2026-11-05T00:00:00Z"};
    exec_sql("SELECT value FROM meta WHERE key=?1", SQLITE_OK);
    for (int i = 0; i < 5; i++) {
        add(FEP_TEST_BIND, SQLITE_OK, keys[i], 1);
        int rc = i == fail_key ? step_rc : SQLITE_ROW;
        add(FEP_TEST_STEP, rc, 0, 0);
        if (rc != SQLITE_ROW) break;
        const char *p = i == fail_key ? value : values[i];
        add(FEP_TEST_COLUMN, i == fail_key ? type : SQLITE_TEXT, p, p ? kstrlen(p) : 0);
        if (i == fail_key) break;
        add(FEP_TEST_STEP, SQLITE_DONE, 0, 0); add(FEP_TEST_RESET, SQLITE_OK, 0, 0);
    }
    add(FEP_TEST_FINALIZE, SQLITE_OK, 0, 0);
}
static void schema(int rc)
{
    exec_sql("SELECT yomi,kanji,freq,last_ts FROM dict_user LIMIT 0", rc);
    if (!rc) {
        add(FEP_TEST_STEP, SQLITE_DONE, 0, 0); add(FEP_TEST_FINALIZE, SQLITE_OK, 0, 0);
    }
}
static int run(int expected)
{
    fep_test_script(&f, script, used);
    int rc = fep_user_open(&f.store);
    CHECK(rc == expected);
    CHECK(fep_test_done(&f) && !bad);
    CHECK(f.store.system.db == (void *)9 && f.store.system.group.generation == 42);
    CHECK(f.store.user_enabled == (expected == 0));
    if (expected && !f.store.user.isolated) CHECK(!f.store.user.db && f.resident_live == 1);
    return 0;
}
static void creation(int fail_stage)
{
    exec_sql("PRAGMA page_size=1024", SQLITE_OK); page(1024);
    exec_sql("BEGIN IMMEDIATE", fail_stage == 1 ? SQLITE_READONLY : SQLITE_OK);
    if (fail_stage == 1) return;
    exec_sql("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE dict_user(yomi TEXT NOT NULL, kanji TEXT NOT NULL,"
        "freq INTEGER DEFAULT 1, last_ts INTEGER DEFAULT 0, PRIMARY KEY(yomi,kanji));"
        "CREATE INDEX idx_user_yomi ON dict_user(yomi);", fail_stage == 2 ? SQLITE_IOERR : SQLITE_OK);
    if (fail_stage != 2) {
        exec_sql("INSERT INTO meta(key,value) VALUES('format_version',?1),"
            "('kind','user'),('dict_id',?2),('pos_table_version',?3),"
            "('created',strftime('%Y-%m-%dT%H:%M:%SZ','now'))", SQLITE_OK);
        add(FEP_TEST_BIND, SQLITE_OK, "1", 1);
        add(FEP_TEST_BIND, SQLITE_OK, FEP_DICT_ID, 2);
        add(FEP_TEST_BIND, SQLITE_OK, "1", 3);
        add(FEP_TEST_STEP, fail_stage == 3 ? SQLITE_IOERR : SQLITE_DONE, 0, 0);
        add(FEP_TEST_FINALIZE, fail_stage == 3 ? SQLITE_IOERR : SQLITE_OK, 0, 0);
        if (fail_stage != 3) exec_sql("COMMIT", fail_stage == 4 ? SQLITE_IOERR : SQLITE_OK);
    }
    if (fail_stage) exec_sql("ROLLBACK", SQLITE_OK);
}
/* A failed initial CREATE can leave an empty file after ROLLBACK. */
static int empty_retry(void)
{
    setup(); opening(1); creation(2);
    for (u32 i = 0; i < used; i++)
        if (script[i].rc == SQLITE_IOERR) script[i].rc = SQLITE_FULL;
    closing(0,0,0);
    CHECK(!run(OS32_ERR_IO) && incomplete_warnings == 1);
    /* Next boot sees the retained zero-byte file and retries creation. */
    setup(); opening(0); script[0].bytes = 0; creation(2); closing(0,0,0);
    CHECK(!run(OS32_ERR_IO) && incomplete_warnings == 1);
    CHECK(flags_seen == (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE));
    setup(); opening(0); script[0].bytes = 0; creation(0);
    metadata(-1,0,0,0); schema(0); exec_sql("PRAGMA cache_size=8", SQLITE_OK);
    CHECK(!run(0) && incomplete_warnings == 0);
    CHECK(flags_seen == (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE));
    return 0;
}
static int faults(void)
{
    static FepTestReply baseline[SCRIPT_CAP];
    setup(); opening(0); page(1024); metadata(-1,0,0,0); schema(0);
    exec_sql("PRAGMA cache_size=8", SQLITE_OK);
    exec_sql("BEGIN IMMEDIATE", SQLITE_OK); exec_sql("COMMIT", SQLITE_OK);
    u32 count = used;
    for (u32 i = 0; i < count; i++) baseline[i] = script[i];
    int active = 0;
    for (u32 i = 0; i < count; i++) {
        enum FepTestOp op = baseline[i].op;
        if (op == FEP_TEST_PREPARE) {
            const char *sql = baseline[i].data;
            active = !kstrcmp(sql, "PRAGMA page_size") ||
                     !kstrncmp(sql, "SELECT ", 7);
        }
        if (op != FEP_TEST_PREPARE && op != FEP_TEST_BIND && op != FEP_TEST_STEP &&
            op != FEP_TEST_RESET && op != FEP_TEST_FINALIZE) continue;
        setup();
        for (u32 j = 0; j <= i; j++) script[j] = baseline[j];
        used = i + 1; script[i].rc = SQLITE_IOERR;
        if (op != FEP_TEST_PREPARE && op != FEP_TEST_FINALIZE && active)
            add(FEP_TEST_FINALIZE, SQLITE_IOERR, 0, 0);
        if (op == FEP_TEST_FINALIZE) active = 0;
        if (op == FEP_TEST_PREPARE && !kstrcmp(baseline[i].data, "COMMIT"))
            exec_sql("ROLLBACK", SQLITE_OK);
        closing(0,0,0); CHECK(!run(OS32_ERR_IO));
    }
    return 0;
}
static int test(void)
{
    CHECK(!faults());
    CHECK(!empty_retry());
    CHECK(fep_user_open(0) == OS32_ERR_INVAL);
    fep_test_reset(&f); CHECK(!f.store.user_enabled && !f.store.user.db && !f.resident_live);
    const char *invalid[] = {"", "/etc/", "/db/u.db", "/etc/sub/u.db", "/etc//u.db",
        "/etc/../u.db", "/etc/.", "/etc/..", "/ETC/u.db", "/etc/a\\b"};
    for (u32 i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        setup(); kstrncpy(f.store.user.path, invalid[i], sizeof(f.store.user.path));
        CHECK(!run(OS32_ERR_INVAL));
    }
    setup(); f.store.user.isolated = 1; CHECK(!run(OS32_ERR_BUSY));
    setup(); available = 0; CHECK(!run(OS32_ERR_IO));
    setup(); add(FEP_TEST_HEADER_READ, OS32_ERR_IO, 0, 0); CHECK(!run(OS32_ERR_IO));
    setup(); add(FEP_TEST_HEADER_READ, 0, 0, 8192);
    add(FEP_TEST_ACQUIRE, SQLITE_FULL, 0, 0); CHECK(!run(OS32_ERR_IO));
    setup(); add(FEP_TEST_HEADER_READ, 0, 0, 8192); add(FEP_TEST_ACQUIRE, 0, 0, 0);
    add(FEP_TEST_OPEN, SQLITE_READONLY, (void *)1, 0); closing(0,0,0);
    CHECK(!run(OS32_ERR_ROFS));
    setup(); opening(0); script[used-1].rc = 1; closing(0,0,0); CHECK(!run(OS32_ERR_STALE));
    setup(); opening(0); page(4096); closing(0,0,0); CHECK(!run(OS32_ERR_INVAL));
    const char *bad_values[] = {"2", "system", "other-id", "2", "not-a-date"};
    for (int i = 0; i < 5; i++) {
        setup(); opening(0); page(1024); metadata(i,bad_values[i],SQLITE_TEXT,SQLITE_ROW);
        closing(0,0,0); CHECK(!run(OS32_ERR_INVAL) && writes == 0);
        setup(); opening(0); page(1024); metadata(i,0,SQLITE_TEXT,SQLITE_DONE);
        closing(0,0,0); CHECK(!run(OS32_ERR_NOTFOUND) && writes == 0);
    }
    const char *numbers[] = {"1x", "-1", "4294967297", "", " 1"};
    for (u32 i = 0; i < sizeof(numbers)/sizeof(numbers[0]); i++) {
        setup(); opening(0); page(1024); metadata(0,numbers[i],SQLITE_TEXT,SQLITE_ROW);
        closing(0,0,0); CHECK(!run(OS32_ERR_INVAL));
    }
    setup(); opening(0); page(1024); metadata(0,"1",SQLITE_INTEGER,SQLITE_ROW);
    closing(0,0,0); CHECK(!run(OS32_ERR_INVAL));
    setup(); opening(0); page(1024); metadata(2,"other",SQLITE_TEXT,SQLITE_ROW);
    closing(SQLITE_BUSY,0,0); CHECK(!run(OS32_ERR_BUSY) && f.store.user.isolated);
    setup(); opening(0); page(1024); metadata(2,"other",SQLITE_TEXT,SQLITE_ROW);
    closing(0,SQLITE_BUSY,SQLITE_BUSY); CHECK(!run(OS32_ERR_BUSY) && f.store.user.isolated);
    setup(); opening(0); page(1024); metadata(-1,0,0,0); schema(SQLITE_ERROR);
    closing(0,0,0); CHECK(!run(OS32_ERR_IO));
    setup(); opening(0); page(1024); metadata(-1,0,0,0); schema(0);
    exec_sql("PRAGMA cache_size=8", SQLITE_NOMEM); closing(0,0,0); CHECK(!run(OS32_ERR_IO));
    setup(); opening(0); page(1024); metadata(-1,0,0,0); schema(0);
    exec_sql("PRAGMA cache_size=8", SQLITE_OK);
    exec_sql("BEGIN IMMEDIATE", SQLITE_OK); exec_sql("COMMIT", SQLITE_OK); CHECK(!run(0));
    CHECK(flags_seen == SQLITE_OPEN_READWRITE && writes == 3 && f.resident_live == 2);
    setup(); opening(0); page(1024); metadata(-1,0,0,0); schema(0);
    exec_sql("PRAGMA cache_size=8", SQLITE_OK);
    exec_sql("BEGIN IMMEDIATE", SQLITE_READONLY); closing(0,0,0); CHECK(!run(OS32_ERR_ROFS));
    setup(); opening(0); page(1024); metadata(-1,0,0,0); schema(0);
    exec_sql("PRAGMA cache_size=8", SQLITE_OK); exec_sql("BEGIN IMMEDIATE", SQLITE_OK);
    exec_sql("COMMIT", SQLITE_IOERR); exec_sql("ROLLBACK", SQLITE_OK);
    closing(0,0,0); CHECK(!run(OS32_ERR_IO));
    for (int stage = 1; stage <= 4; stage++) {
        setup(); opening(1); creation(stage); closing(0,0,0);
        CHECK(!run(stage == 1 ? OS32_ERR_ROFS : OS32_ERR_IO));
    }
    setup(); opening(1); creation(0); metadata(-1,0,0,0); schema(0);
    exec_sql("PRAGMA cache_size=8", SQLITE_OK); CHECK(!run(0));
    CHECK(flags_seen == (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE));
    /* Journal plus NUL boundary: maximum accepted length is 247 for 256. */
    for (u32 len = OS32_MAX_PATH - kstrlen(FEP_USER_JOURNAL_SUFFIX) - 1;
         len <= OS32_MAX_PATH; len++) {
        setup();
        for (u32 i = kstrlen(FEP_USER_DIR); i < sizeof(f.store.user.path); i++)
            f.store.user.path[i] = 'a';
        if (len < sizeof(f.store.user.path)) f.store.user.path[len] = 0;
        if (len == OS32_MAX_PATH - kstrlen(FEP_USER_JOURNAL_SUFFIX) - 1) {
            add(FEP_TEST_HEADER_READ, OS32_ERR_IO, 0, 0); CHECK(!run(OS32_ERR_IO));
        } else CHECK(!run(OS32_ERR_INVAL));
    }
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? (rc % 254) + 1 : 0) : "memory");
    __builtin_unreachable();
}
