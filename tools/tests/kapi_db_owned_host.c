/* Real wrapper, deterministic SQLite boundary faults; no copied DB logic. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"
#include "../../kapi/kapi_db.c"

unsigned char test_shm[DB_SHM_BLOCK_SIZE];
struct sqlite3 {
    int closed, transaction, close_rc, rollback_rc, finalize_rc;
    int closes, rollbacks, finalizes, steps;
    char trace[32];
    const char *error;
};
struct sqlite3_stmt { sqlite3 *db; };
static sqlite3 connections[64];
static sqlite3_stmt statements[64];
static int next_db, owner, open_rc, open_null;
extern void db_cleanup_owned(int owner) __attribute__((weak));

int res_owner_get(void) { return owner; }
int kprintf(int color, const char *fmt, ...) { return 0; }
/* v50 (票 S0-K): kapi_db.c が使う exec / VFS の口。この票 (F1) の対象では
 * ないので素通しの模型にする。実物は tools/tests/kapi_db_v50_host.c が踏む。 */
int ring3_user_range_ok(u32 p, u32 len) { return 1; }
/* BSD strlcat 相当 (lib/kstring.h の kstrncat と同じく n はバッファ全体)。 */
char *host_strlcat(char *dst, const char *src, unsigned long n)
{
    unsigned long used = strlen(dst);
    unsigned long i = 0;
    while (used + i + 1 < n && src[i]) { dst[used + i] = src[i]; i++; }
    if (used + i < n) dst[used + i] = '\0';
    return dst;
}
static int stat_size = 4096;
int vfs_stat(const char *path, OS32_Stat *st)
{
    if (strstr(path, "-journal")) return OS32_ERR_NOTFOUND;   /* hot journal 無し */
    if (stat_size < 0) return OS32_ERR_NOTFOUND;
    if (st) { memset(st, 0, sizeof(*st)); st->st_size = (u32)stat_size; }
    return 0;
}
static void event(sqlite3 *db, const char *text)
{
    assert(!db->closed);
    strcat(db->trace, text);
}
int sqlite3_open(const char *path, sqlite3 **out)
{
    sqlite3 *db = &connections[next_db++];
    db->error = "open first";
    *out = open_null ? 0 : db;
    return open_rc;
}
/* v50 で kapi_db.c が使う SQLite の口。この票 (F1) は既存 10 本の寿命だけを
 * 見るので、ここは「呼べる」だけの最小実装。中身は kapi_db_v50_host.c が
 * 本物の SQLite で踏む。 */
int sqlite3_open_v2(const char *path, sqlite3 **out, int flags, const char *vfs)
{
    sqlite3 *db = &connections[next_db++];
    db->error = "open first";
    *out = open_null ? 0 : db;
    return open_rc;
}
int sqlite3_extended_errcode(sqlite3 *db) { return 0; }
int sqlite3_errcode(sqlite3 *db) { return 0; }
/* fs/vfs.c の cwd と path 正規化。この票 (F1) は絶対名しか使わないので
 * 素通し。解決の規則そのものは tools/tests/kapi_db_v50_host.c が踏む。 */
const char *vfs_cwd(void) { return "/"; }
int vfs_resolve_path(const char *in, char *out, int size)
{
    int i;
    for (i = 0; i < size - 1 && in && in[i]; i++) out[i] = in[i];
    out[i] = '\0';
    return (in && in[i]) ? VFS_ERR_NAMETOOLONG : VFS_OK;
}
int sqlite3_bind_parameter_count(sqlite3_stmt *s) { return 3; }
int sqlite3_bind_int(sqlite3_stmt *s, int i, int v) { return SQLITE_OK; }
int sqlite3_bind_null(sqlite3_stmt *s, int i) { return SQLITE_OK; }
int sqlite3_bind_text(sqlite3_stmt *s, int i, const char *t, int n,
                      void (*d)(void *)) { return SQLITE_OK; }
int sqlite3_bind_blob(sqlite3_stmt *s, int i, const void *b, int n,
                      void (*d)(void *)) { return SQLITE_OK; }
const char *sqlite3_errmsg(sqlite3 *db) { return db->error; }
const char *sqlite3_errstr(int rc) { return "out of memory"; }
int sqlite3_close(sqlite3 *db)
{
    event(db, "C"); db->closes++;
    db->error = "close failure";
    if (!db->close_rc) db->closed = 1;
    return db->close_rc;
}
int sqlite3_get_autocommit(sqlite3 *db) { return !db->transaction; }
int sqlite3_exec(sqlite3 *db, const char *sql,
                 int (*cb)(void *, int, char **, char **), void *arg, char **err)
{
    if (!strcmp(sql, "ROLLBACK")) {
        event(db, "R"); db->rollbacks++;
        db->error = "rollback failure";
        if (!db->rollback_rc) db->transaction = 0;
        return db->rollback_rc;
    }
    return SQLITE_OK;
}
int sqlite3_prepare_v2(sqlite3 *db, const char *sql, int n,
                       sqlite3_stmt **stmt, const char **tail)
{
    sqlite3_stmt *s = &statements[db - connections];
    assert(!db->closed); s->db = db; *stmt = s; return SQLITE_OK;
}
int sqlite3_step(sqlite3_stmt *stmt)
{
    assert(!stmt->db->closed); stmt->db->steps++; return SQLITE_ROW;
}
int sqlite3_finalize(sqlite3_stmt *stmt)
{
    sqlite3 *db = stmt->db;
    event(db, "F"); db->finalizes++; db->error = "finalize failure";
    return db->finalize_rc;
}
int sqlite3_column_count(sqlite3_stmt *s) { return 0; }
int sqlite3_column_type(sqlite3_stmt *s, int c) { return SQLITE_INTEGER; }
int sqlite3_column_int(sqlite3_stmt *s, int c) { return s->db->steps; }
const unsigned char *sqlite3_column_text(sqlite3_stmt *s, int c) { return 0; }
int sqlite3_column_bytes(sqlite3_stmt *s, int c) { return 0; }
double sqlite3_column_double(sqlite3_stmt *s, int c) { return 0; }
const void *sqlite3_column_blob(sqlite3_stmt *s, int c) { return 0; }
sqlite3_int64 sqlite3_memory_used(void) { return 0; }

static void reset(void)
{
    memset(test_shm, 0, sizeof(test_shm));
    memset(db_slots, 0, sizeof(db_slots));
    memset(connections, 0, sizeof(connections));
    memset(statements, 0, sizeof(statements));
    next_db = owner = open_rc = open_null = 0;
}
static void test_owned(void)
{
    int a, b, b2;
    reset(); owner = 1; a = kapi_db_open("parent");
    assert(a == 0);
    assert(kapi_db_prepare(a, "SELECT") == DB_STATUS_ROW);
    assert(kapi_db_column_int(a, 0) == 1); /* prepare consumes first row */
    owner = 2; b = kapi_db_open("child"); b2 = kapi_db_open("child2");
    assert(b == 1 && b2 == 2);
    assert(db_cleanup_owned != 0); /* assertion RED, not missing-symbol error */
    owner = 3; db_cleanup_owned(2); /* argument, not current owner */
    assert(!connections[0].closed);
    assert(connections[1].closed && connections[2].closed);
    assert(kapi_db_step(a) == DB_STATUS_ROW);
    assert(kapi_db_column_int(a, 0) == 2); /* no new owner restriction */
    db_cleanup_owned(2);
    assert(connections[1].closes == 1 && connections[2].closes == 1);
    assert(kapi_db_open("reuse") == b);
    db_cleanup_all(); /* legacy entry point still closes every active owner */
    assert(connections[0].closed && connections[3].closed);
    assert(connections[0].finalizes == 1);
}
static void test_teardown_order(void)
{
    int mode, h;
    for (mode = 0; mode < 3; mode++) {
        reset(); owner = 2; h = kapi_db_open("transaction");
        assert(kapi_db_prepare(h, "SELECT") == DB_STATUS_ROW);
        connections[0].transaction = 1;
        if (mode == 0) assert(kapi_db_close(h) == 0);
        else if (mode == 1) db_cleanup_owned(2);
        else db_cleanup_all();
        assert(!strcmp(connections[0].trace, "FRC"));
        assert(kapi_db_open("reuse") == h);
        assert(kapi_db_close(h) == 0);
        assert(!strcmp(connections[1].trace, "C"));
    }
}
static void test_finalize_failure(void)
{
    int h;
    reset(); h = kapi_db_open("finalize-fault");
    assert(kapi_db_prepare(h, "SELECT") == DB_STATUS_ROW);
    connections[0].finalize_rc = SQLITE_IOERR;
    connections[0].transaction = 1;
    assert(kapi_db_close(h) == -1);
    assert(!strcmp(connections[0].trace, "FRC"));
    assert(!strcmp(kapi_db_last_error(h), "finalize failure"));
    assert(connections[0].closed);
    assert(kapi_db_open("reuse-after-close-success") == h);
    assert(strcmp(kapi_db_last_error(h), "finalize failure"));
}
static void test_rollback_failure(void)
{
    int h;
    reset(); h = kapi_db_open("rollback-fault");
    connections[0].transaction = 1;
    connections[0].rollback_rc = SQLITE_IOERR;
    assert(kapi_db_close(h) == -1);
    assert(!strcmp(connections[0].trace, "RC"));
    assert(!strcmp(kapi_db_last_error(h), "rollback failure"));
    assert(connections[0].closed);
    assert(kapi_db_open("close-success-reuse") == h);
}
static void test_busy_isolation(void)
{
    int h, fresh, stage, mode, i;
    const char *message;
    for (mode = 0; mode < 3; mode++) {
        for (stage = 0; stage < 3; stage++) {
            reset(); owner = 2; h = kapi_db_open("busy");
            assert(kapi_db_prepare(h, "SELECT") == DB_STATUS_ROW);
            connections[0].transaction = 1;
            connections[0].close_rc = SQLITE_BUSY;
            if (stage > 0) connections[0].rollback_rc = SQLITE_IOERR;
            if (stage > 1) connections[0].finalize_rc = SQLITE_NOMEM;
            message = stage == 2 ? "finalize failure" :
                      stage == 1 ? "rollback failure" : "close failure";
            if (mode == 0) assert(kapi_db_close(h) == -1);
            else if (mode == 1) db_cleanup_owned(2);
            else db_cleanup_all();
            assert(!strcmp(kapi_db_last_error(h), message));
            assert(!strcmp(connections[0].trace, "FRC"));
            assert(!connections[0].closed);
            /* Simulate returning to parent then recycling the same nest. */
            owner = 1; db_cleanup_owned(2);
            owner = 2; fresh = kapi_db_open("new-child");
            assert(fresh != h && fresh >= 0);
            assert(kapi_db_close(h) == -1);
            assert(kapi_db_prepare(h, "SELECT") == -1);
            assert(kapi_db_exec(h, "UPDATE") == -1);
            assert(kapi_db_step(h) == -1);
            assert(kapi_db_finalize(h) == -1);
            db_cleanup_owned(2); db_cleanup_all();
            assert(connections[1].closed);
            assert(!strcmp(connections[0].trace, "FRC"));
            assert(!strcmp(kapi_db_last_error(h), message));
            for (i = 0; i < DB_MAX_CONNECTIONS - 1; i++)
                assert(kapi_db_open("capacity") >= 0);
            assert(kapi_db_open("full-not-isolated-reuse") == -1);
        }
    }
}
static void test_open_failure(void)
{
    int busy, h;
    DB_ResultHeader *hdr;
    for (busy = 0; busy < 2; busy++) {
        reset(); owner = 2; open_rc = SQLITE_CANTOPEN;
        connections[0].close_rc = busy ? SQLITE_BUSY : SQLITE_OK;
        connections[0].transaction = 1;
        connections[0].rollback_rc = SQLITE_IOERR;
        assert(kapi_db_open("bad-open") == -1);
        hdr = (DB_ResultHeader *)test_shm;
        assert(hdr->status == DB_STATUS_ERROR);
        assert(!strcmp((char *)test_shm + hdr->error_offset, "open first"));
        assert(!strcmp(connections[0].trace, "RC"));
        assert(!strcmp(kapi_db_last_error(0), "open first"));
        open_rc = SQLITE_OK;
        owner = 1; db_cleanup_owned(2);
        owner = 2; h = kapi_db_open("recycled-owner");
        assert(h == (busy ? 1 : 0));
        db_cleanup_owned(2); db_cleanup_all();
        assert(!strcmp(connections[0].trace, "RC"));
        if (busy) assert(!strcmp(kapi_db_last_error(0), "open first"));
    }
    reset(); open_rc = SQLITE_NOMEM; open_null = 1;
    assert(kapi_db_open("null-on-failure") == -1);
    assert(connections[0].closes == 0);
    hdr = (DB_ResultHeader *)test_shm;
    assert(hdr->status == DB_STATUS_ERROR);
    assert(!strcmp((char *)test_shm + hdr->error_offset, "out of memory"));
    assert(!strcmp(kapi_db_last_error(0), "out of memory"));
    open_rc = SQLITE_OK; open_null = 0;
    assert(kapi_db_open("reuse-null-failure") == 0);
}
int main(void)
{
    test_open_failure();
    test_busy_isolation();
    test_rollback_failure();
    test_owned();
    test_teardown_order();
    test_finalize_failure();
    puts("PASS owned, teardown order, finalize/rollback faults, BUSY isolation, open faults");
    return 0;
}
