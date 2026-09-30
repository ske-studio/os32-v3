/* ======================================================================== */
/*  OS32_SQLITE_VFS.H — OS32 カスタム SQLite VFS ヘッダ                     */
/* ======================================================================== */
#ifndef OS32_SQLITE_VFS_H
#define OS32_SQLITE_VFS_H

#include "vfs.h"

/* Internal, non-reentrant API. Pass a COPY of this cookie, never a pointer
 * to mutable registry state: stale callers must not acquire a new lifetime. */

#define OS32_SQLITE_EXEC 1
#define OS32_SQLITE_RESIDENT 2
#define OS32_SQLITE_EXEC_GROUPS DB_MAX_CONNECTIONS
#define OS32_SQLITE_RESIDENT_GROUPS 1
#define OS32_SQLITE_GROUPS (OS32_SQLITE_EXEC_GROUPS + OS32_SQLITE_RESIDENT_GROUPS)
#define OS32_SQLITE_GROUP_GENERATION_MAX 0xffffffffUL
#define OS32_SQLITE_FREE 0
#define OS32_SQLITE_OPENING 1
#define OS32_SQLITE_LIVE 2
#define OS32_SQLITE_CLOSING 3
#define OS32_SQLITE_QUARANTINED 4
typedef VfsSqliteCookie Os32SqliteGroup;
int os32_sqlite_group_acquire(int kind, int owner, Os32SqliteGroup *out);

const char *os32_sqlite_group_vfs_name(const Os32SqliteGroup *group);
int os32_sqlite_group_opened(const Os32SqliteGroup *group, void *db);
int os32_sqlite_group_begin_close(const Os32SqliteGroup *group);
/* Caller must clear its own db immediately after sqlite3_close returns OK,
 * BEFORE finish/diagnostics, including when finish reports an orphan. */
int os32_sqlite_group_finish(const Os32SqliteGroup *group, int close_rc);
#define OS32_SQLITE_STAGE_CLOSE 1
#define OS32_SQLITE_STAGE_XCLOSE 2
#define OS32_SQLITE_STAGE_LEASE 3
#define OS32_SQLITE_STAGE_OPEN 4
int os32_sqlite_group_quarantine(const Os32SqliteGroup *group, int stage, int rc);

typedef struct {
    Os32SqliteGroup cookie;
    int state, kind, owner;
    int first_stage, first_rc, sticky_rc;
    int fd_count, has_db;
} Os32SqliteGroupInfo;
/* Copies fixed-table evidence only; never reads SQLite or sqlite3_file.
 * A FREE lifetime's snapshot remains readable until the next acquire. */
int os32_sqlite_group_snapshot(const Os32SqliteGroup *group,
                               Os32SqliteGroupInfo *out);

struct sqlite3;
/* Sole supported connection-open entry for dedicated groups. URI and VFS
 * override flags/prefix are rejected before SQLite. One attempt per acquire.
 * On error, retain the group and any returned db through normal teardown. */
int os32_sqlite_group_open(const Os32SqliteGroup *group, const char *path,
                           int flags, struct sqlite3 **out);

/* SQLite エンジン初期化 (MEMSYS5 + VFS登録 + sqlite3_initialize) */
/* 戻り値: 0=成功, 非0=失敗 */
int os32_sqlite_init(void);

/* カーネル内 SQLite 動作テスト (メモリDB で CRUD 検証) */
/* 戻り値: 0=全テスト成功, -1=失敗あり */
int os32_sqlite_test(void);

/* MEMSYS5 プール canary 検証 (デバッグ用) */
/* 戻り値: 0=正常, -1=前方破壊, -2=後方破壊 */
int memsys5_check_canary(void);

/* DB接続の main ファイルが使う OS32 VFS fd を返す (失敗時 -1) */
/* 引数は sqlite3* (ヘッダ依存を避けるため void* で受ける) */
int os32_sqlite_db_fd(void *db);

/* VFS メソッドテーブルアドレスダンプ (デバッグ用) */
void os32_sqlite_dump_vfs(void);

#endif /* OS32_SQLITE_VFS_H */
