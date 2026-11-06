/* DICT_META_DESIGN §§2,3,5,6: private core/FEP contract, freestanding GNU11.
 * No SQLite handle crosses KAPI. No entry here yields or calls back into IME.
 * Implementations/import registration belong to the following component stages.
 */
#ifndef OS32_IME_STORE_H
#define OS32_IME_STORE_H

#include "types.h"
#include "os32_kapi_shared.h"
#include "vfs.h"
#include "../../lib/sqlite3/os32_sqlite_vfs.h"

#define FEP_DICT_READER_VERSION 1u
#define FEP_DICT_FORMAT_VERSION 1u
#define FEP_DICT_POS_VERSION 1u
#define FEP_DICT_ID_CAP 64u
#define FEP_DICT_PAGE_SIZE 1024u
#define FEP_DICT_HEADER_SIZE 100u
#define FEP_DICT_CACHE_PAGES 16u
#define FEP_USER_CACHE_PAGES 8u
#define FEP_DICT_RESERVE_KB (OS32_SQLITE_FEP_RESERVE / 1024u)
#define FEP_DICT_MIN_RESERVE_KB (OS32_SQLITE_FEP_RESERVE_MIN / 1024u)
#define FEP_DICT_ID "ipadic-2.7.0"
#define FEP_DICT_COST_SCALE "os32-log2047-boost-v1"
#define FEP_DICT_PATH_S "/db/fep_s.db"
#define FEP_DICT_PATH_M IME_DICT_PATH
#define FEP_DICT_PATH_L "/db/fep_l.db"
#define FEP_USER_PATH "/etc/fep_user.db"
#define FEP_USER_DIR "/etc/"
#define FEP_USER_JOURNAL_SUFFIX "-journal"
/* settings table keys (scope=system), not dotted configuration labels. */
#define FEP_SETTING_RESERVE "fep_mem_reserve_kb"
#define FEP_SETTING_USER_PATH "fep_user_dict_path"

typedef struct {
    u32 format, min_reader, pos_version, reserve_kb, cache_pages, entries;
    char dict_id[FEP_DICT_ID_CAP];
    char variant; /* S/M/L. Long metadata stays in SQL, never in this copy. */
} FepMeta;

typedef struct {
    void *db, *exact, *prefix, *learn;
    Os32SqliteGroup group;
    char path[OS32_MAX_PATH];
    int isolated, io_retried;
} FepConn;

typedef struct {
    FepConn system, user;
    FepMeta meta;
    u32 boot_reserve_kb;
    int user_enabled;
} FepStore;

/* Define store types before ime.h: IME_State owns a store by value. */
#include "ime.h"

/* New store calls: 0 success, negative OS32_ERR_*; never raw SQLite rc.
 * INVAL: arguments/header/meta incompatibility, IO: SQL/read/partial creation,
 * NOTFOUND: missing file/key, STALE: invalidated FD, BUSY: isolated teardown,
 * ROFS: unwritable user DB, AGAIN: switch needs a larger boot reservation.
 * Existing ime_user_* operation-specific error numbers remain unchanged.
 * Init once with {0}; never zero/reuse an isolated connection. Close order:
 * group_begin_close -> finalize all statements -> close -> group_finish.
 */
int fep_meta_probe(const char *path, FepMeta *out);
/* Core owns both results. Outputs copied only on success; AGAIN until saved.
 * Probe uses OS32_SQLITE_RESIDENT (not EXEC), validates header before SQL,
 * and closes its temporary RO group before
 * return. It neither changes the boot reservation nor loads FEP.
 */
int fep_boot_config_get(FepMeta *out, u32 *reserve_kb);
/* Same saved boot snapshot; default path if no setting, no settings re-open.
 * Core only copies: no path validation. Capacity overflow returns OS32_ERR_INVAL;
 * missing DB/table/row uses the default; other read failures return OS32_ERR_IO. Only fep_user_open (D) checks
 * the normalized /etc direct-child restriction. */
int fep_boot_user_path_get(char *out, u32 capacity);
int fep_store_open(FepStore *s, const char *path, const char *user_path);
int fep_store_switch(FepStore *s, const char *path);
int fep_user_open(FepStore *s);
/* user.path is copied before fep_user_open; absent file is created atomically.
 * Custom user_path: normalized /etc direct child, including NUL and journal
 * suffix within OS32_MAX_PATH. Failure disables learning, preserves system,
 * retains the file and returns the reason. Store open still succeeds when
 * only learning fails. Switch probes/budget-checks before closing old system;
 * retains user, reopens old path on failure, never grows boot_reserve_kb.
 */
int fep_conn_close(FepConn *conn);
/* A: 0 when usable; INVAL for NULL, AGAIN without db, BUSY if isolated,
 * IO if engine/FD unavailable, STALE before any SQL. I owns reopen/recovery. */
int fep_conn_ready(FepConn *conn);
int fep_store_close(FepStore *s); /* Both connections; preserve isolation. */
u32 fep_store_live_connections(const FepStore *s); /* Actual live groups, 0..2. */

/* I owns search/admin routing. Results retain IME_MAX_RESULTS and the old
 * LIMIT-before-dedup semantics. User cursor failure restarts system alone.
 */
int fep_store_search(FepStore *s, const char *yomi, IME_Result *out, int max);
void fep_store_learn(FepStore *s, const char *yomi, const char *kanji);
int fep_store_user_list(FepStore *s, const char *prefix, IME_UserEntry *out, int max);
int fep_store_user_delete(FepStore *s, const char *yomi, const char *kanji);
int fep_store_user_export(FepStore *s, const char *path);
int fep_store_user_clear(FepStore *s);

/* C owns system-only metadata access in ime_dict_info.c, including the
 * store reader. J will bind ime_dict_info_facade and ime_current_dict() to it.
 * Same 20 keys, byte offsets/cap clamp and errors as IME_DICT_INFO_* in the
 * shared SDK header. No open/recover/yield; error leaves out untouched.
 */
int fep_store_info(FepStore *s, int key, int offset, char *out, int capacity);

/* F owns this userland pure-function implementation in hsync_protect.inc.
 * Signature shared here for the contract test; hsync need not include the
 * kernel types above. Copy normalized /etc direct-child path, 0 on success,
 * OS32_ERR_INVAL on invalid/overlong path. Default name stays protected.
 */
int hsp_set_fep_user_path(const char *absolute_path);

#endif /* OS32_IME_STORE_H */
