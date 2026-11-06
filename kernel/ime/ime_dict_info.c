/* Dictionary metadata query. No open/recovery, retained SQLite pointers or
 * resident copy of the source text; only one bounded caller staging buffer. */
#include "ime_store.h"
#include "redir_access.h"
#include "exec.h"
#include "kstring.h"
#include "../../lib/sqlite3/os32_sqlite_outer.h"
#ifdef OS32_FEP_MODULE
#include "fep_imports.h"
#define services_public fep_services_public()
#endif

static const char *const ime_meta_keys[] = {
    NULL, NULL, "format_version", "min_fep_version", "dict_id", "kind",
    "language", "charset", "variant", "source", "source_version", "license",
    "attribution", "build_tool", "build_date", "entry_count", "pos_table_version",
    "cost_scale", "mem_reserve_kb", "cache_pages", "checksum"
};
STATIC_ASSERT(sizeof(ime_meta_keys) / sizeof(ime_meta_keys[0]) ==
              IME_DICT_INFO_CHECKSUM + 1, ime_meta_key_count);

static int ime_info_slice(const void *value, int len, int offset,
                          char *out, int capacity)
{
    int n = 0;
    if (offset < len) {
        n = len - offset;
        if (n > capacity - 1) n = capacity - 1;
        kmemcpy(out, (const char *)value + offset, (u32)n);
    }
    out[n] = 0;
    return n;
}

static int ime_meta_read(FepConn *conn, int key, int offset,
                         char *out, int capacity)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(conn->db,
        "SELECT value FROM meta WHERE key = ?", -1, &stmt, NULL);
    int result = OS32_ERR_IO;
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_text(stmt, 1, ime_meta_keys[key], -1, SQLITE_STATIC);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) result = OS32_ERR_IO;
        else if (rc == SQLITE_ROW) {
            int type = sqlite3_column_type(stmt, 0);
            const void *value = sqlite3_column_blob(stmt, 0);
            int len = sqlite3_column_bytes(stmt, 0);
            /* NULL/failed conversion is not a recorded empty string. */
            if (type != SQLITE_NULL &&
                (value || (len == 0 && sqlite3_errcode(conn->db) != SQLITE_NOMEM)))
                result = ime_info_slice(value, len, offset, out, capacity);
        }
    }
    if (sqlite3_finalize(stmt) != SQLITE_OK) result = OS32_ERR_IO; /* store */
    return result;
}

/* Only the compatibility defaults specified for a validated legacy file. */
static int ime_legacy_info(FepStore *s, int key, int offset,
                           char *out, int capacity)
{
    const char *value;
    char number[11]; /* u32 decimal plus NUL */
    u32 numeric;
    switch (key) {
    case IME_DICT_INFO_FORMAT_VERSION: numeric = 0; break;
    case IME_DICT_INFO_POS_TABLE_VERSION: numeric = FEP_DICT_POS_VERSION; break;
    case IME_DICT_INFO_MEM_RESERVE_KB: numeric = FEP_DICT_RESERVE_KB; break;
    case IME_DICT_INFO_CACHE_PAGES: numeric = FEP_DICT_CACHE_PAGES; break;
    case IME_DICT_INFO_DICT_ID: value = FEP_DICT_ID; goto text;
    case IME_DICT_INFO_COST_SCALE: value = FEP_DICT_COST_SCALE; goto text;
    case IME_DICT_INFO_VARIANT:
        if (!kstrcmp(s->system.path, FEP_DICT_PATH_S)) value = "S";
        else if (!kstrcmp(s->system.path, FEP_DICT_PATH_M)) value = "M";
        else if (!kstrcmp(s->system.path, FEP_DICT_PATH_L)) value = "L";
        else return OS32_ERR_NOTFOUND;
        goto text;
    default: return OS32_ERR_NOTFOUND;
    }
    int start = sizeof(number) - 1;
    number[start] = 0;
    do {
        number[--start] = '0' + numeric % 10;
        numeric /= 10;
    } while (numeric);
    value = number + start;
text:
    return ime_info_slice(value, (int)kstrlen(value), offset, out, capacity);
}

int fep_store_info(FepStore *s, int key, int offset, char *out, int capacity)
{
    char staging[IME_DICT_INFO_CAP];
    int n;
    const char *value;
    if (!s || key < IME_DICT_INFO_STATUS || key > IME_DICT_INFO_CHECKSUM ||
        offset < 0 || capacity < 2 || !out) return OS32_ERR_INVAL;
    if (capacity > IME_DICT_INFO_CAP) capacity = IME_DICT_INFO_CAP;
    int rc = fep_conn_ready(&s->system);
    if (rc == OS32_ERR_AGAIN) {
        if (key != IME_DICT_INFO_STATUS) return rc;
        value = "unloaded";
    } else {
        if (rc) return rc;
        if (key == IME_DICT_INFO_STATUS) {
            value = s->meta.format ? "meta" : "legacy";
        } else if (!s->meta.format) {
            n = ime_legacy_info(s, key, offset, staging, capacity);
            goto copy;
        } else {
            n = ime_meta_read(&s->system, key, offset, staging, capacity);
            goto copy;
        }
    }
    n = ime_info_slice(value, (int)kstrlen(value), offset, staging, capacity);
copy:
    if (n >= 0) kmemcpy(out, staging, (u32)n + 1);
    return n;
}

/* The schema probe distinguishes an absent table from a failed read. */
static int ime_meta_present(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'meta'",
        -1, &stmt, NULL);
    int result = OS32_ERR_IO;
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) result = 1;
        else if (rc == SQLITE_DONE) result = 0;
    }
    if (sqlite3_finalize(stmt) != SQLITE_OK) result = OS32_ERR_IO;
    return result;
}

static int ime_dict_meta_read(const IME_Dict *dict, int key, int offset,
                         char *out, int capacity)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(dict->db,
        "SELECT value FROM meta WHERE key = ?", -1, &stmt, NULL);
    int result = OS32_ERR_IO;
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_text(stmt, 1, ime_meta_keys[key], -1, SQLITE_STATIC);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) result = OS32_ERR_NOTFOUND;
        else if (rc == SQLITE_ROW) {
            int type = sqlite3_column_type(stmt, 0);
            const void *value = sqlite3_column_blob(stmt, 0);
            int len = sqlite3_column_bytes(stmt, 0);
            /* NULL/failed conversion is not a recorded empty string. */
            if (type != SQLITE_NULL &&
                (value || (len == 0 && sqlite3_errcode(dict->db) != SQLITE_NOMEM)))
                result = ime_info_slice(value, len, offset, out, capacity);
        }
    }
    if (sqlite3_finalize(stmt) != SQLITE_OK) result = OS32_ERR_IO;
    return result;
}

int ime_dict_info_facade(int key, int offset, char *out, int capacity)
{
    struct caller_access caller;
    char staging[IME_DICT_INFO_CAP];
    int n;
    if (!services_public) return OS32_ERR_NOSYS;
    if (key < IME_DICT_INFO_STATUS || key > IME_DICT_INFO_CHECKSUM ||
        offset < 0 || capacity < 2 || !out) return OS32_ERR_INVAL;
    if (capacity > IME_DICT_INFO_CAP) capacity = IME_DICT_INFO_CAP;
    if (!caller_access_get(&caller)) return OS32_ERR_INVAL;
    if (!check_caller_write_range(&caller, out, (u32)capacity)) {
        ring3_fault_kill();
        return OS32_ERR_INVAL;
    }
    const IME_Dict *dict = ime_current_dict();
    const char *value = NULL;
    if (!dict->db) {
        if (key != IME_DICT_INFO_STATUS) return OS32_ERR_AGAIN;
        value = "unloaded";
    } else {
        int fd = os32_sqlite_db_fd(dict->db);
        if (fd >= 0 && vfs_fd_is_stale(fd)) return OS32_ERR_STALE;
        if (dict->isolated || fd < 0 || !sqlite_engine_available(1)) return OS32_ERR_IO;
        int present = ime_meta_present(dict->db);
        if (present < 0) return present;
        if (key == IME_DICT_INFO_STATUS) {
            value = present ? "meta" : "metadata-unavailable";
        } else if (present) {
            n = ime_dict_meta_read(dict, key, offset, staging, capacity);
            goto copy;
        } else if (key == IME_DICT_INFO_VARIANT) {
            if (!kstrcmp(dict->path, FEP_DICT_PATH_S)) value = "S";
            else if (!kstrcmp(dict->path, IME_DICT_PATH)) value = "M";
            else if (!kstrcmp(dict->path, FEP_DICT_PATH_L)) value = "L";
            else return OS32_ERR_NOTFOUND;
        } else return OS32_ERR_NOTFOUND;
    }
    n = ime_info_slice(value, (int)kstrlen(value), offset, staging, capacity);
copy:
    if (n >= 0 && !copy_to_caller(&caller, out, staging, (u32)n + 1))
        return OS32_ERR_IO;
    return n;
}
