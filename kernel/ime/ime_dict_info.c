/* Dictionary metadata query. No open/recovery, retained SQLite pointers or
 * resident copy of the source text; only one bounded caller staging buffer. */
#include "ime.h"
#include "redir_access.h"
#include "exec.h"
#include "kstring.h"
#include "../../lib/sqlite3/os32_sqlite_outer.h"
#ifdef OS32_FEP_MODULE
#include "fep_imports.h"
#define services_public fep_services_public()
#endif

/* The variant fallback must agree with ime_switch_dict's fixed paths. */
#define IME_INFO_SMALL_PATH "/db/fep_s.db"
#define IME_INFO_LARGE_PATH "/db/fep_l.db"

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

static int ime_meta_read(const IME_Dict *dict, int key, int offset,
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
            n = ime_meta_read(dict, key, offset, staging, capacity);
            goto copy;
        } else if (key == IME_DICT_INFO_VARIANT) {
            if (!kstrcmp(dict->path, IME_INFO_SMALL_PATH)) value = "S";
            else if (!kstrcmp(dict->path, IME_DICT_PATH)) value = "M";
            else if (!kstrcmp(dict->path, IME_INFO_LARGE_PATH)) value = "L";
            else return OS32_ERR_NOTFOUND;
        } else return OS32_ERR_NOTFOUND;
    }
    n = ime_info_slice(value, (int)kstrlen(value), offset, staging, capacity);
copy:
    if (n >= 0 && !copy_to_caller(&caller, out, staging, (u32)n + 1))
        return OS32_ERR_IO;
    return n;
}
