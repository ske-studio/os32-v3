/* Freestanding ILP32: real metadata reader, connection readiness and getter.
 * Ordered SQLite replies use A's shared substrate, starting from BSS. */
#include "kernel/ime/ime_store.h"
#include "redir_access.h"
#include "kstring.h"
#include "tools/tests/fep_store_fixture.c"
#include "lib/sqlite3/os32_sqlite_outer.h"
static IME_State g_ime;
/* CURRENT_GETTERS */
/* PRODUCT_INFO */
/* PRODUCT_CONN */
static FepTestFixture fixture;
static int available = 1, fd = 7, stale, sql_calls, bad;
static int type = SQLITE_TEXT, length, err = SQLITE_OK;
static int identity = 1, writable = 1, killed, copies, checked, copy_ok = 1;
static char row[800];
static const char *bound;
static void check(int value) { __asm__ volatile ("" : "+r"(value) : : "memory"); if (!value) bad = 1; }
void *kmemcpy(void *d, const void *s, u32 n)
{ for (u32 i = 0; i < n; i++) ((char *)d)[i] = ((const char *)s)[i]; return d; }
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
int kstrcmp(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
char *kstrncpy(char *d, const char *s, u32 n)
{ u32 i = 0; if (n) { for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = 0; } return d; }
int sqlite_engine_available(int io) { check(io == 1); return available; }
int os32_sqlite_outer_db_fd(void *db) { check(db == fixture.store.system.db); return fd; }
int vfs_fd_is_stale(int value) { check(value == fd); return stale; }
int caller_access_get(struct caller_access *c) { c->origin = CALLER_USER; return identity; }
int check_caller_write_range(const struct caller_access *c, void *p, u32 n)
{ (void)c; (void)p; checked = n; return writable; }
int copy_to_caller(const struct caller_access *c, void *d, const void *s, u32 n)
{ (void)c; copies++; if (!copy_ok) return 0; kmemcpy(d, s, n); return 1; }
void ring3_fault_kill(void) { killed++; }
int services_public;
static const FepTestReply *next(enum FepTestOp op)
{ sql_calls++; return fep_test_next(&fixture, op); }
int os32_sqlite_prepare_v2(sqlite3 *db, const char *sql, int n, sqlite3_stmt **s, const char **tail)
{
    check(db == fixture.store.system.db && n == -1 && !tail);
    check(!kstrcmp(sql, "SELECT value FROM meta WHERE key = ?"));
    const FepTestReply *r = next(FEP_TEST_PREPARE);
    *s = r->rc == SQLITE_OK ? (void *)2 : 0; return r->rc;
}
int os32_sqlite_bind_text(sqlite3_stmt *s, int i, const char *v, int n, void (*destroy)(void *))
{ check(s == (void *)2 && i == 1 && n == -1 && destroy == SQLITE_STATIC); bound = v; return next(FEP_TEST_BIND)->rc; }
int os32_sqlite_step(sqlite3_stmt *s)
{ check(s == (void *)2); return next(FEP_TEST_STEP)->rc; }
int os32_sqlite_column_type(sqlite3_stmt *s, int i)
{ check(s == (void *)2 && i == 0); next(FEP_TEST_COLUMN); return type; }
const void *os32_sqlite_column_blob(sqlite3_stmt *s, int i)
{ check(s == (void *)2 && i == 0); return next(FEP_TEST_COLUMN)->data; }
int os32_sqlite_column_bytes(sqlite3_stmt *s, int i)
{ check(s == (void *)2 && i == 0); next(FEP_TEST_COLUMN); return length; }
int os32_sqlite_errcode(sqlite3 *db) { check(db == fixture.store.system.db); return err; }
int os32_sqlite_finalize(sqlite3_stmt *s)
{
    check(s == (void *)2 || !s);
    int rc = next(FEP_TEST_FINALIZE)->rc;
    for (u32 i = 0; i < sizeof(row); i++) row[i] = '!';
    return rc;
}
static FepTestReply replies[7];
static void script(int phase, int failure)
{
    enum FepTestOp ops[] = {FEP_TEST_PREPARE, FEP_TEST_BIND, FEP_TEST_STEP,
        FEP_TEST_COLUMN, FEP_TEST_COLUMN, FEP_TEST_COLUMN, FEP_TEST_FINALIZE};
    u32 n = 0;
    for (int i = 0; i < 7; i++) {
        if (i > phase && i < 6) continue;
        replies[n++] = (FepTestReply){ops[i], i == phase ? failure :
            (i == 2 ? SQLITE_ROW : SQLITE_OK), i == 4 ? row : 0, 0};
    }
    fep_test_script(&fixture, replies, n);
}
static char output[260];
static int call(int key, int offset, int cap, int facade)
{
    for (u32 i = 0; i < sizeof(output); i++) output[i] = '#';
    copies = checked = killed = 0;
    return facade ? ime_dict_info_facade(key, offset, output + 1, cap) :
        fep_store_info(&fixture.store, key, offset, output + 1, cap);
}
static void unchanged(void)
{ for (u32 i = 0; i < sizeof(output); i++) check(output[i] == '#'); }
static void value(int key, const char *expected, int facade)
{
    int n = call(key, 0, 257, facade);
    check(n == (int)kstrlen(expected) && !kstrcmp(output + 1, expected));
    check(output[0] == '#' && output[n + 2] == '#');
    if (facade) check(copies == 1 && checked == 256);
}
static int test(void)
{
    fep_test_reset(&fixture);
    value(1, "unloaded", 0);
    check(call(2, 0, 256, 0) == OS32_ERR_AGAIN); unchanged();
    check(fep_store_info(0, 1, 0, output, 256) == OS32_ERR_INVAL);
    check(call(0, 0, 256, 0) == OS32_ERR_INVAL); unchanged();
    check(call(21, 0, 256, 0) == OS32_ERR_INVAL);
    check(call(1, -1, 256, 0) == OS32_ERR_INVAL);
    check(call(1, 0, 0, 0) == OS32_ERR_INVAL);
    check(call(1, 0, 1, 0) == OS32_ERR_INVAL);
    check(fep_store_info(&fixture.store, 1, 0, 0, 256) == OS32_ERR_INVAL);
    fixture.store.system.db = (void *)1;
    fixture.store.user.db = (void *)3; /* Must never be queried. */
    static const int legacy_keys[] = {1, 2, 4, 8, 16, 17, 18, 19};
    static const char *const legacy_values[] = {"legacy", "0", "ipadic-2.7.0", "M", "1", "os32-log2047-boost-v1", "192", "16"};
    kstrncpy(fixture.store.system.path, FEP_DICT_PATH_M, OS32_MAX_PATH);
    for (u32 i = 0; i < sizeof(legacy_keys)/sizeof(legacy_keys[0]); i++) value(legacy_keys[i], legacy_values[i], 0);
    kstrncpy(fixture.store.system.path, FEP_DICT_PATH_S, OS32_MAX_PATH); value(8, "S", 0);
    kstrncpy(fixture.store.system.path, FEP_DICT_PATH_L, OS32_MAX_PATH); value(8, "L", 0);
    kstrncpy(fixture.store.system.path, "/db/custom.db", OS32_MAX_PATH);
    check(call(8, 0, 256, 0) == OS32_ERR_NOTFOUND); unchanged();
    check(call(11, 0, 256, 0) == OS32_ERR_NOTFOUND); unchanged();
    check(sql_calls == 0);
    fixture.store.meta.format = 1;
    value(1, "meta", 0);
    static const char *const keys[] = {"format_version", "min_fep_version", "dict_id", "kind", "language", "charset", "variant", "source", "source_version", "license", "attribution", "build_tool", "build_date", "entry_count", "pos_table_version", "cost_scale", "mem_reserve_kb", "cache_pages", "checksum"};
    for (int key = 2; key <= 20; key++) {
        script(6, SQLITE_OK); kstrncpy(row, "recorded", sizeof(row)); length = 8;
        value(key, "recorded", 0); check(!kstrcmp(bound, keys[key - 2])); check(fep_test_done(&fixture));
    }
    char original[769], joined[769];
    for (int i = 0; i < 768; i++) original[i] = "あ"[i % 3];
    original[768] = 0;
    int caps[] = {2, 256, 257};
    for (u32 c = 0; c < 3; c++) {
        int offset = 0;
        do {
            script(6, SQLITE_OK); kmemcpy(row, original, sizeof(original)); length = 768;
            int n = call(11, offset, caps[c], 0);
            check(n >= 0 && n <= 255 && n <= caps[c] - 1);
            if (n < 0 || n > 255 || offset + n > 768) return 1;
            check(output[n + 1] == 0 && output[n + 2] == '#');
            check(fep_test_done(&fixture)); kmemcpy(joined + offset, output + 1, n);
            offset += n; if (!n) break;
        } while (offset <= 768);
        check(offset == 768); joined[offset] = 0; check(!kstrcmp(joined, original));
    }
    script(6, SQLITE_OK); length = 8;
    check(call(11, 0x7fffffff, 256, 0) == 0 && output[1] == 0);
    script(6, SQLITE_OK); length = 0; check(call(12, 0, 256, 0) == 0);
    for (int phase = 0; phase <= 6; phase++) {
        if (phase > 2 && phase < 6) continue;
        script(phase, SQLITE_IOERR); length = 8;
        check(call(11, 0, 256, 0) == OS32_ERR_IO); unchanged(); check(fep_test_done(&fixture));
    }
    script(2, SQLITE_DONE); check(call(12, 0, 256, 0) == OS32_ERR_IO); unchanged();
    script(6, SQLITE_OK); type = SQLITE_NULL; check(call(12, 0, 256, 0) == OS32_ERR_IO); unchanged(); type = SQLITE_TEXT;
    script(6, SQLITE_OK); replies[4].data = 0; length = 0; err = SQLITE_NOMEM;
    check(call(12, 0, 256, 0) == OS32_ERR_IO); unchanged(); err = SQLITE_OK;
    int before = sql_calls; stale = 1;
    for (int key = 1; key <= 20; key++) { check(call(key, 0, 256, 0) == OS32_ERR_STALE); unchanged(); }
    check(sql_calls == before); stale = 0;
    fixture.store.system.isolated = 1; check(call(1, 0, 256, 0) == OS32_ERR_BUSY); fixture.store.system.isolated = 0;
    available = 0; check(call(1, 0, 256, 0) == OS32_ERR_IO); available = 1;
    fd = -1; check(call(1, 0, 256, 0) == OS32_ERR_IO); fd = 7;
    /* J has not opened store: the live compatibility dictionary owns boot. */
    g_ime.dict.db = (void *)99;
    check(ime_current_dict() == &g_ime.dict);
    check(ime_current_store() == &g_ime.store);
    return bad;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
