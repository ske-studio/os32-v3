#define OS32_SQLITE_OUTER_IMPL
#include "fep_boot.h"
#include "sqlite_engine.h"
#include "ime/ime_store.h"
#include "ime/fep_module.h"
#include "config.h"
#include "kprintf.h"
#include "kstring.h"
#include "../lib/sqlite3/generated/sqlite_facade.h"

extern volatile u32 tick_count;

/* Core owns the immutable boot snapshot; a switch probe never updates it. */
static FepMeta boot_meta;
static u32 boot_reserve_kb;
static int boot_config_saved, boot_user_rc = OS32_ERR_AGAIN;
static char boot_user_path[OS32_MAX_PATH];

int fep_boot_config_get(FepMeta *out, u32 *reserve_kb)
{
    if (!out || !reserve_kb) return OS32_ERR_INVAL;
    if (!boot_config_saved) return OS32_ERR_AGAIN;
    *out = boot_meta;
    *reserve_kb = boot_reserve_kb;
    return 0;
}

void fep_boot_user_path_set(const char *path)
{
    kstrncpy(boot_user_path, path, sizeof(boot_user_path));
}

void fep_boot_user_rc_set(int rc)
{
    boot_user_rc = rc;
}

static void fep_boot_invalidate(void)
{
    boot_config_saved = 0;
    fep_boot_user_rc_set(OS32_ERR_AGAIN);
}

int fep_boot_user_path_get(char *out, u32 capacity)
{
    if (!out || !capacity) return OS32_ERR_INVAL;
    if (boot_user_rc) return boot_user_rc;
    u32 n = kstrlen(boot_user_path);
    if (n >= capacity) return OS32_ERR_INVAL;
    kmemcpy(out, boot_user_path, n + 1);
    return 0;
}

static int probe_close(Os32SqliteGroup *group, sqlite3 *db, sqlite3_stmt *stmt)
{
    int rc = os32_sqlite_outer_group_begin_close(group);
    if (rc) return OS32_ERR_BUSY;
    if (stmt) (void)os32_sqlite_finalize(stmt); /* destroys even after step error */
    rc = db ? os32_sqlite_close(db) : SQLITE_OK;
    return os32_sqlite_outer_group_finish(group, rc) ? OS32_ERR_BUSY : 0;
}

static int meta_uint(const unsigned char *s, int n, u32 *out)
{
    u32 value = 0;
    if (!s || !n) return OS32_ERR_INVAL;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9' ||
            value > (0xffffffffu - (s[i] - '0')) / 10u) return OS32_ERR_INVAL;
        value = value * 10u + s[i] - '0';
    }
    *out = value;
    return 0;
}

int fep_meta_probe(const char *path, FepMeta *out)
{
    if (!path || !out) return OS32_ERR_INVAL;
    u8 header[FEP_DICT_HEADER_SIZE];
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return fd == VFS_ERR_NOTFOUND ? OS32_ERR_NOTFOUND : OS32_ERR_IO;
    int n = vfs_read_fd(fd, header, sizeof(header));
    vfs_close(fd);
    if (n != sizeof(header)) return OS32_ERR_IO;
    if (memcmp(header, "SQLite format 3", 16) ||
        ((u32)header[16] << 8 | header[17]) != FEP_DICT_PAGE_SIZE)
        return OS32_ERR_INVAL;
    u32 version = (u32)header[60] << 24 | (u32)header[61] << 16 |
                  (u32)header[62] << 8 | header[63];
    if (version > FEP_DICT_FORMAT_VERSION) return OS32_ERR_INVAL;
    FepMeta meta = {0};
    meta.format = version;
    meta.variant = !kstrcmp(path, FEP_DICT_PATH_S) ? 'S' :
                   !kstrcmp(path, FEP_DICT_PATH_M) ? 'M' :
                   !kstrcmp(path, FEP_DICT_PATH_L) ? 'L' : 0;
    Os32SqliteGroup group;
    sqlite3 *db = 0;
    sqlite3_stmt *stmt = 0;
    int rc = os32_sqlite_outer_group_acquire(OS32_SQLITE_RESIDENT, 0, &group);
    if (rc) return rc == SQLITE_FULL || rc == SQLITE_BUSY ? OS32_ERR_BUSY : OS32_ERR_IO;
    rc = os32_sqlite_outer_group_open(&group, path, SQLITE_OPEN_READONLY, &db);
    if (rc) { rc = OS32_ERR_IO; goto done; }
    /* Validate the legacy table without scanning dictionary rows. */
    rc = os32_sqlite_prepare_v2(db, "SELECT yomi,kanji,pos_id,cost FROM dict LIMIT 0",
                                -1, &stmt, 0);
    if (rc) { rc = OS32_ERR_IO; goto done; }
    rc = os32_sqlite_finalize(stmt);
    stmt = 0;
    if (rc) { rc = OS32_ERR_IO; goto done; }
    if (!version) {
        meta.min_reader = FEP_DICT_READER_VERSION;
        meta.pos_version = FEP_DICT_POS_VERSION;
        meta.reserve_kb = FEP_DICT_RESERVE_KB;
        meta.cache_pages = FEP_DICT_CACHE_PAGES;
        kstrncpy(meta.dict_id, FEP_DICT_ID, sizeof(meta.dict_id));
    } else {
        static const char *const keys[] = {
            "format_version", "min_fep_version", "dict_id", "kind", "language",
            "charset", "variant", "source", "source_version", "license",
            "attribution", "build_tool", "build_date", "entry_count",
            "pos_table_version", "cost_scale", "mem_reserve_kb", "cache_pages", "checksum"
        };
        u32 seen = 0;
        rc = os32_sqlite_prepare_v2(db, "SELECT key,value FROM meta", -1, &stmt, 0);
        if (rc) { rc = OS32_ERR_IO; goto done; }
        int step;
        while ((step = os32_sqlite_step(stmt)) == SQLITE_ROW) {
            const char *key = (const char *)os32_sqlite_column_text(stmt, 0);
            const unsigned char *value = os32_sqlite_column_text(stmt, 1);
            int bytes = os32_sqlite_column_bytes(stmt, 1);
            if (!key || !value) { rc = OS32_ERR_INVAL; goto done; }
            u32 i;
            for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
                if (!kstrcmp(key, keys[i])) break;
            if (i == sizeof(keys) / sizeof(keys[0])) continue;
            if (bytes <= 0 || seen & (1u << i)) { rc = OS32_ERR_INVAL; goto done; }
            seen |= 1u << i;
            u32 number = 0;
            if (i == 0 || i == 1 || i == 13 || i == 14 || i == 16 || i == 17) {
                rc = meta_uint(value, bytes, &number);
                if (rc) goto done;
            } else if (kstrlen((const char *)value) != (u32)bytes) {
                rc = OS32_ERR_INVAL; goto done;
            }
            switch (i) {
            case 0: if (number != version) rc = OS32_ERR_INVAL; break;
            case 1: meta.min_reader = number;
                if (number > FEP_DICT_READER_VERSION) rc = OS32_ERR_INVAL;
                break;
            case 2:
                if ((u32)bytes >= sizeof(meta.dict_id)) rc = OS32_ERR_INVAL;
                else kstrncpy(meta.dict_id, (const char *)value, sizeof(meta.dict_id));
                break;
            case 3: if (kstrcmp((const char *)value, "system")) rc = OS32_ERR_INVAL; break;
            case 4: if (kstrcmp((const char *)value, "ja")) rc = OS32_ERR_INVAL; break;
            case 5: if (kstrcmp((const char *)value, "UTF-8")) rc = OS32_ERR_INVAL; break;
            case 6:
                if (bytes != 1 || (value[0] != 'S' && value[0] != 'M' && value[0] != 'L') ||
                    (meta.variant && meta.variant != value[0])) rc = OS32_ERR_INVAL;
                else meta.variant = value[0];
                break;
            case 13: meta.entries = number; break;
            case 14: meta.pos_version = number;
                if (number != FEP_DICT_POS_VERSION) rc = OS32_ERR_INVAL;
                break;
            case 15: if (kstrcmp((const char *)value, FEP_DICT_COST_SCALE)) rc = OS32_ERR_INVAL; break;
            case 16: meta.reserve_kb = number; break;
            case 17: meta.cache_pages = number; break;
            case 18:
                if (bytes != 71 || memcmp(value, "sha256:", 7)) rc = OS32_ERR_INVAL;
                else for (int j = 7; j < bytes; j++)
                    if (!((value[j] >= '0' && value[j] <= '9') ||
                          (value[j] >= 'a' && value[j] <= 'f') ||
                          (value[j] >= 'A' && value[j] <= 'F'))) rc = OS32_ERR_INVAL;
                break;
            }
            if (rc) goto done;
        }
        if (step != SQLITE_DONE) { rc = OS32_ERR_IO; goto done; }
        if (seen != (1u << (sizeof(keys) / sizeof(keys[0]))) - 1u) {
            rc = OS32_ERR_NOTFOUND; goto done;
        }
        if (!meta.reserve_kb || !meta.cache_pages ||
            meta.reserve_kb > 0x7fffffffu / KIB ||
            meta.cache_pages > meta.reserve_kb ||
            FEP_USER_CACHE_PAGES > meta.reserve_kb - meta.cache_pages) {
            rc = OS32_ERR_INVAL; goto done;
        }
    }
done:;
    int close_rc = probe_close(&group, db, stmt);
    if (close_rc) return close_rc;
    if (!rc) *out = meta;
    return rc;
}

/* Missing/unreadable reserve settings retain the metadata default. The user
 * path snapshot retains its read error so learning can report that failure. */
static int services_settings(int *reserve, int *present)
{
    Os32SqliteGroup group;
    sqlite3 *db = 0;
    sqlite3_stmt *stmt = 0;
    boot_user_rc = 0;
    fep_boot_user_path_set(FEP_USER_PATH);
    int rc = os32_sqlite_outer_group_acquire(OS32_SQLITE_RESIDENT, 0, &group);
    if (rc) { boot_user_rc = OS32_ERR_IO; return rc; }
    rc = os32_sqlite_outer_group_open(&group, SYS_SETTINGS_DB, SQLITE_OPEN_READONLY, &db);
    int has_settings = 0;
    if (rc) {
        OS32_Stat st;
        rc = vfs_stat(SYS_SETTINGS_DB, &st) == VFS_ERR_NOTFOUND ? 0 : OS32_ERR_IO;
    } else {
        rc = os32_sqlite_prepare_v2(db,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='settings'",
            -1, &stmt, 0);
        if (!rc) {
            int step = os32_sqlite_step(stmt);
            if (step == SQLITE_ROW) has_settings = 1;
            else if (step != SQLITE_DONE) rc = OS32_ERR_IO;
        }
        if (os32_sqlite_finalize(stmt)) rc = OS32_ERR_IO;
        stmt = 0;
    }
    if (!rc && has_settings && !os32_sqlite_prepare_v2(db,
            "SELECT ival FROM settings WHERE scope='system' AND key='fep_mem_reserve_kb' AND type=0",
            -1, &stmt, 0) && os32_sqlite_step(stmt) == SQLITE_ROW) {
        int kb = os32_sqlite_column_int(stmt, 0);
        *present = 1;
        if (kb < OS32_SQLITE_FEP_RESERVE_MIN / (int)KIB) {
            kprintf(KLOG_ATTR, "[services] fep_mem_reserve_kb below minimum: %d; using %d KiB\n",
                    kb, OS32_SQLITE_FEP_RESERVE_MIN / (int)KIB);
            *reserve = OS32_SQLITE_FEP_RESERVE_MIN;
        } else {
            *reserve = kb <= (int)(0x7fffffffu / KIB) ? kb * (int)KIB : -1;
        }
    }
    if (stmt) { (void)os32_sqlite_finalize(stmt); stmt = 0; }
    if (!rc && has_settings) rc = os32_sqlite_prepare_v2(db,
        "SELECT tval FROM settings WHERE scope='system' AND key='fep_user_dict_path' AND type=1",
        -1, &stmt, 0);
    if (!rc && has_settings) {
        int step = os32_sqlite_step(stmt);
        if (step == SQLITE_ROW) {
            const char *text = (const char *)os32_sqlite_column_text(stmt, 0);
            int bytes = os32_sqlite_column_bytes(stmt, 0);
            if (!text || bytes < 0 || (u32)bytes >= sizeof(boot_user_path) ||
                kstrlen(text) != (u32)bytes) rc = OS32_ERR_INVAL;
            else fep_boot_user_path_set(text);
        } else if (step != SQLITE_DONE) rc = OS32_ERR_IO;
    }
    if (stmt && os32_sqlite_finalize(stmt)) rc = OS32_ERR_IO;
    fep_boot_user_rc_set(rc > 0 ? OS32_ERR_IO : rc);
    rc = os32_sqlite_outer_group_begin_close(&group);
    if (rc) return rc;
    rc = services_boot_fault == SERVICES_FAULT_SETTINGS_CLOSE ? SQLITE_BUSY
         : db ? os32_sqlite_close(db) : SQLITE_OK;
    return os32_sqlite_outer_group_finish(&group, rc);
}

int fep_boot_start(const struct module_handle *sqlite, int no_dictionary, int *budget)
{
    int reserve = OS32_SQLITE_FEP_RESERVE, reserve_present = 0;
    int rc = services_settings(&reserve, &reserve_present);
    *budget = reserve;
    if (!rc) rc = os32_sqlite_outer_boot_check(0, 0);
    if (rc) return sqlite ? rc : MODULE_ISOLATED;
    if (sqlite) services_stage = SERVICES_SETTINGS_CLOSED;
    kprintf(KLOG_ATTR, "[services] SETTINGS_CLOSED tick=%u\n", tick_count);
    if (no_dictionary) {
        /* No live resident group and no later FEP load: boot_check(0, 0)
         * has released the FEP reservation along with settings. */
        *budget = 0;
        services_stage = SERVICES_SQLITE_ONLY;
        services_public = 1;
        kprintf(KLOG_ATTR, "[services] SQLITE_ONLY: no FEP dictionary\n");
        return 0;
    }
    FepMeta meta;
    rc = fep_meta_probe(IME_DICT_PATH, &meta);
    if (rc) return !sqlite && rc == OS32_ERR_BUSY ? MODULE_ISOLATED : rc;
    if (!reserve_present) {
        u32 kb = meta.reserve_kb;
        if (kb < FEP_DICT_MIN_RESERVE_KB) {
            kprintf(KLOG_ATTR, "[services] metadata reserve below minimum: %u; using %u KiB\n",
                    kb, FEP_DICT_MIN_RESERVE_KB);
            kb = FEP_DICT_MIN_RESERVE_KB;
        }
        reserve = kb <= 0x7fffffffu / KIB ? (int)(kb * KIB) : -1;
    }
    if (services_boot_fault == SERVICES_FAULT_RESERVE) reserve = SQLITE_MODULE_POOL_SIZE;
    *budget = reserve;
    rc = os32_sqlite_outer_boot_check(0, reserve);
    if (rc) return rc;
    boot_meta = meta;
    boot_reserve_kb = (u32)reserve / KIB;
    boot_config_saved = 1;
    rc = fep_module_load_after_sql(sqlite);
    kprintf(KLOG_ATTR, "[IME] boot dictionary rc=%d\n", rc);
    if (rc) return rc;
    if (sqlite) services_stage = SERVICES_FEP_INTERNAL_READY;
    kprintf(KLOG_ATTR, "[services] FEP_INTERNAL_READY tick=%u\n", tick_count);
    rc = os32_sqlite_outer_boot_check(ime_boot_connections(), reserve);
    if (rc) return rc;
    if (services_boot_fault == SERVICES_FAULT_FEP_CLOSE ||
        services_boot_fault == SERVICES_FAULT_SHUTDOWN) {
        return MODULE_INIT_FAILED;
    }
    services_stage = SERVICES_READY;
    services_public = 1;
    fep_module_publish();
    return 0;
}

int fep_boot_rollback(const struct module_handle *sqlite, int failure)
{
    int blocked = fep_module_abort() != 0;
    fep_boot_invalidate();
    /* Static SQL survives rollback. Keep its reservation if cleanup failed.
     * The module-backed transaction also audits SQL before aborting its owner. */
    if (!sqlite && (blocked || failure == MODULE_ISOLATED)) return 1;
    if (sqlite_module_ready() && os32_sqlite_outer_boot_check(0, 0)) return 1;
    return blocked;
}

int fep_boot_static(void)
{
    static int attempted;
    OS32_Stat dictionary;
    int reserve;
    if (attempted) return MODULE_BUSY;
    if (services_stage != SERVICES_SQLITE_ONLY || !services_public)
        return MODULE_BAD_DEPENDENCY;
    attempted = 1;
    int missing = vfs_stat(IME_DICT_PATH, &dictionary) == VFS_ERR_NOTFOUND;
    int rc = fep_boot_start(0, missing, &reserve);
    if (rc && fep_boot_rollback(0, rc)) return MODULE_ISOLATED;
    return rc;
}
