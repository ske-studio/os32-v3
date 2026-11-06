/* DICT_META §§3,5: optional learning connection, separate from system DB. */
#include "ime_store.h"
#include "os32_sqlite_outer.h"
#include "vfs.h"
#include "kstring.h"
#include "kprintf.h"
#include "tvram.h"
#ifdef OS32_FEP_MODULE
#include "fep_imports.h"
#endif

static int user_sql_error(const char *stage, int rc)
{
    kprintf(ATTR_RED, "IME: user %s failed (sqlite rc=%d)\r\n", stage, rc);
    return (rc & 255) == SQLITE_READONLY ? OS32_ERR_ROFS : OS32_ERR_IO;
}

static int user_exec(sqlite3 *db, const char *sql)
{
    int rc = sqlite3_exec(db, sql, 0, 0, 0);
    return rc == SQLITE_OK ? 0 : user_sql_error(sql, rc);
}

static int user_path_valid(const char *path)
{
    u32 len = 0, dir = kstrlen(FEP_USER_DIR);
    while (len < OS32_MAX_PATH && path[len]) len++;
    if (len + kstrlen(FEP_USER_JOURNAL_SUFFIX) >= OS32_MAX_PATH)
        return 0;
    if (len <= dir || kstrncmp(path, FEP_USER_DIR, dir)) return 0;
    const char *name = path + dir;
    if (!kstrcmp(name, ".") || !kstrcmp(name, "..")) return 0;
    for (u32 i = dir; i < len; i++)
        if (path[i] == '/' || path[i] == '\\') return 0;
    return 1;
}

/* Decimal metadata is TEXT; reject prefixes, signs, NULs and overflow. */
static int user_number(const unsigned char *p, int n, u32 expected)
{
    u32 value = 0;
    if (!p || n <= 0) return 0;
    for (int i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return 0;
        u32 digit = p[i] - '0';
        if (value > (0xffffffffu - digit) / 10u) return 0;
        value = value * 10u + digit;
    }
    return value == expected;
}

static int user_text(const unsigned char *p, int n, const char *expected)
{
    if (!p || n != (int)kstrlen(expected)) return 0;
    for (int i = 0; i < n; i++) if (p[i] != (unsigned char)expected[i]) return 0;
    return 1;
}

static int user_created(const unsigned char *p, int n)
{
    /* UTC ISO 8601, as written by this creator. */
    const char *shape = "0000-00-00T00:00:00Z";
    if (!p || n != (int)kstrlen(shape)) return 0;
    for (int i = 0; i < n; i++) {
        if (shape[i] == '0') {
            if (p[i] < '0' || p[i] > '9') return 0;
        } else if (p[i] != (unsigned char)shape[i]) return 0;
    }
    return 1;
}

static int user_meta(FepStore *s)
{
    static const char *keys[] = {
        "format_version", "kind", "dict_id", "pos_table_version", "created"
    };
    sqlite3_stmt *stmt = 0;
    int rc = sqlite3_prepare_v2(s->user.db,
        "SELECT value FROM meta WHERE key=?1", -1, &stmt, 0);
    int result = rc == SQLITE_OK ? 0 : user_sql_error("meta prepare", rc);
    for (u32 i = 0; !result && i < sizeof(keys) / sizeof(keys[0]); i++) {
        rc = sqlite3_bind_text(stmt, 1, keys[i], -1, SQLITE_STATIC);
        if (rc != SQLITE_OK) { result = user_sql_error("meta bind", rc); break; }
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            result = rc == SQLITE_DONE ? OS32_ERR_NOTFOUND :
                user_sql_error("meta read", rc);
            break;
        }
        int valid = 0;
        if (sqlite3_column_type(stmt, 0) == SQLITE_TEXT) {
            const unsigned char *p = sqlite3_column_text(stmt, 0);
            int n = sqlite3_column_bytes(stmt, 0);
            if (i == 0) valid = user_number(p, n, FEP_DICT_FORMAT_VERSION);
            if (i == 1) valid = user_text(p, n, "user");
            if (i == 2) valid = user_text(p, n, s->meta.dict_id);
            if (i == 3) valid = user_number(p, n, s->meta.pos_version);
            if (i == 4) valid = user_created(p, n);
        }
        if (!valid) { result = OS32_ERR_INVAL; break; }
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) { result = user_sql_error("meta end", rc); break; }
        rc = sqlite3_reset(stmt);
        if (rc != SQLITE_OK) result = user_sql_error("meta reset", rc);
    }
    if (stmt) {
        rc = sqlite3_finalize(stmt);
        if (!result && rc != SQLITE_OK) result = user_sql_error("meta finalize", rc);
    }
    return result;
}

static int user_create(FepStore *s)
{
    sqlite3 *db = s->user.db;
    sqlite3_stmt *stmt = 0;
    char version[U32_DEC_BUFSIZE], pos[U32_DEC_BUFSIZE];
    sqlite3_snprintf(sizeof(version), version, "%u", FEP_DICT_FORMAT_VERSION);
    sqlite3_snprintf(sizeof(pos), pos, "%u", s->meta.pos_version);
    int result = user_exec(db, "BEGIN IMMEDIATE");
    if (result) return result;
    result = user_exec(db,
        "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE dict_user(yomi TEXT NOT NULL, kanji TEXT NOT NULL,"
        "freq INTEGER DEFAULT 1, last_ts INTEGER DEFAULT 0, PRIMARY KEY(yomi,kanji));"
        "CREATE INDEX idx_user_yomi ON dict_user(yomi);");
    if (!result) {
        int rc = sqlite3_prepare_v2(db,
            "INSERT INTO meta(key,value) VALUES('format_version',?1),"
            "('kind','user'),('dict_id',?2),('pos_table_version',?3),"
            "('created',strftime('%Y-%m-%dT%H:%M:%SZ','now'))", -1, &stmt, 0);
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, version, -1, SQLITE_STATIC);
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, s->meta.dict_id, -1, SQLITE_STATIC);
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 3, pos, -1, SQLITE_STATIC);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) result = user_sql_error("create meta", rc);
    }
    if (stmt) {
        int rc = sqlite3_finalize(stmt);
        if (!result && rc != SQLITE_OK) result = user_sql_error("create finalize", rc);
    }
    if (!result) result = user_exec(db, "COMMIT");
    if (result) {
        int rollback = user_exec(db, "ROLLBACK");
        if (rollback) return rollback;
    }
    return result;
}

static int user_page_size(sqlite3 *db)
{
    sqlite3_stmt *stmt = 0;
    int rc = sqlite3_prepare_v2(db, "PRAGMA page_size", -1, &stmt, 0);
    int result = rc == SQLITE_OK ? 0 : user_sql_error("page prepare", rc);
    if (!result) {
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) result = user_sql_error("page read", rc);
        else if (sqlite3_column_int(stmt, 0) != FEP_DICT_PAGE_SIZE) result = OS32_ERR_INVAL;
        else {
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) result = user_sql_error("page end", rc);
        }
    }
    if (stmt) {
        rc = sqlite3_finalize(stmt);
        if (!result && rc != SQLITE_OK) result = user_sql_error("page finalize", rc);
    }
    return result;
}

int fep_user_open(FepStore *s)
{
    OS32_Stat st;
    sqlite3 *db = 0;
    char pragma[OS32_MAX_PATH];
    int result, rc;
    if (!s) return OS32_ERR_INVAL;
    s->user_enabled = 0;
    FepConn *c = &s->user;
    if (c->isolated || c->db || c->group.generation) return OS32_ERR_BUSY;
    if (!user_path_valid(c->path)) return OS32_ERR_INVAL;
    if (!SQLITE_OUTER_READY(1)) return OS32_ERR_IO;
    rc = vfs_stat(c->path, &st);
    int create = rc == OS32_ERR_NOTFOUND;
    if (rc && !create) return rc;
    if (!create && (st.st_mode & OS_S_IFMT) != OS_S_IFREG) return OS32_ERR_INVAL;
    if (!rc && st.st_size == 0) create = 1;
    rc = os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT, 0, &c->group);
    if (rc != SQLITE_OK) return user_sql_error("acquire", rc);
    rc = os32_sqlite_group_open(&c->group, c->path,
        SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0), &db);
    c->db = db;
    result = rc == SQLITE_OK ? 0 : user_sql_error("open", rc);
    if (!result) result = fep_conn_ready(c);
    if (!result && create) {
        sqlite3_snprintf(sizeof(pragma), pragma, "PRAGMA page_size=%u", FEP_DICT_PAGE_SIZE);
        result = user_exec(db, pragma);
    }
    if (!result) result = user_page_size(db);
    if (!result && create) result = user_create(s);
    if (!result) result = user_meta(s);
    /* No write SQL precedes compatibility validation for an existing DB. */
    if (!result) {
        sqlite3_stmt *stmt = 0;
        rc = sqlite3_prepare_v2(db,
            "SELECT yomi,kanji,freq,last_ts FROM dict_user LIMIT 0", -1, &stmt, 0);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) result = user_sql_error("schema", rc);
        if (stmt) {
            rc = sqlite3_finalize(stmt);
            if (!result && rc != SQLITE_OK) result = user_sql_error("schema finalize", rc);
        }
    }
    if (!result) {
        sqlite3_snprintf(sizeof(pragma), pragma, "PRAGMA cache_size=%u", FEP_USER_CACHE_PAGES);
        result = user_exec(db, pragma);
    }
    /* An empty BEGIN/COMMIT can succeed after VFS downgrades RW to RO.
     * A write statement checks writability even with no matching rows. Run
     * only after compatibility validation; preserve all learning data. */
    if (!result && !create)
        result = user_exec(db, "UPDATE dict_user SET freq=freq WHERE 0");
    if (!result) { c->io_retried = 0; s->user_enabled = 1; return 0; }
    rc = fep_conn_close(c);
    if (create)
        kprintf(ATTR_RED, "IME: %s is incomplete; delete it to recreate\r\n", c->path);
    kprintf(ATTR_RED, "IME: learning disabled (rc=%d, close=%d)\r\n", result, rc);
    return rc ? rc : result;
}
