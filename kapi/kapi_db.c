/* ======================================================================== */
/*  KAPI_DB.C — SQLite DB KAPI ラッパー実装                                  */
/*                                                                          */
/*  外部プログラムが KAPI 関数テーブル経由で SQLite を操作するための            */
/*  カーネル側ブリッジ。DB 接続スロットで最大 DB_MAX_CONNECTIONS 個の           */
/*  同時接続を管理する。                                                      */
/*                                                                          */
/*  結果データは共有メモリ (MEM_SHM_DB_BASE) に DB_ResultHeader +               */
/*  DB_ColumnInfo[] + データ の形式で書き込まれる。                            */
/* ======================================================================== */

#include "kapi_db.h"
#include "os32_kapi_slots.h"   /* v50: 自己診断が slot 番号を名前で見る */
#include "os32_sqlite_config.h"
#include "sqlite3.h"
#include "os32_sqlite_outer.h"
#include "kstring.h"
#include "utf8.h"
#include "kprintf.h"
#include "os32_sqlite_vfs.h"
#include "memmap.h"
#include "fd_redirect.h"
#include "vfs.h"          /* v50: open 前の vfs_stat (本体 size / journal) */
#include "redir_access.h"

extern int ring3_call_from_user(void);

/* SQLite calls go through the typed outer facade. Stage caller strings in
 * core memory before the module is entered; the module never retains them. */
#ifdef OS32_SQLITE_DIRECT_TEST
#define DB_MODULE_READY() 1
#else
#define DB_MODULE_READY() services_public
#endif
#define SQL_COPY_BUF_SIZE DB_SQL_MAX_BYTES
static char sql_copy_buf[SQL_COPY_BUF_SIZE];

/* パス文字列コピー用バッファ (外部プログラム空間からの読み取り問題回避) */
#define PATH_COPY_BUF_SIZE OS32_MAX_PATH
static char path_copy_buf[PATH_COPY_BUF_SIZE];

/* v50 (票 S0-K §1a): 検証後のコピー先は **1 本ずつの静的スクラッチ**。
 * bind は SQLITE_TRANSIENT なので SQLite が自分で写す = 次の呼び出しで
 * 上書きしてよい。SQLite に呼び手のポインタを保持させないための置き場。 */
static char text_copy_buf[DB_BIND_TEXT_MAX + 1];
static u8   blob_copy_buf[DB_BIND_BLOB_MAX];

/* v50: 呼び手の path を **解決した絶対名**。相対名のまま検査すると cwd の
 * 長さが勘定に入らない (Codex 往復 2 の B4: 251B の cwd + "f.db" は入力 4B で
 * 検査を通るのに、実 VFS が連結して 256B に切り詰めるので本体と journal の
 * 解決名が同じになり BUSY_RECOVERY になる)。stat も open もこの名前で行う
 * — SQLite に相対名を渡さないので、transaction 中に cwd が動いても
 * journal の削除先が変わらない。 */
static char abs_path_buf[VFS_MAX_PATH];

/* `<path>-journal` を組み立てる場所。**下位層の容量に合わせる**:
 * fs/vfs.c の vfs_resolve_path は VFS_MAX_PATH (= OS32_MAX_PATH = 256B) の
 * 作業バッファで正規化するので、それを超える名前は静かに切り詰められて
 * **本体を指してしまう**。SQLite 側の mxPathname も 256。したがって
 * `<絶対名>-journal` が NUL 込み 256B に収まらない path は open の前に断る。 */
#define DB_JOURNAL_SUFFIX "-journal"
static char journal_buf[VFS_MAX_PATH];

/* ======== DB接続スロット ======== */
#define DB_ERROR_SIZE 256
typedef struct {
    int in_use;               /* 1=使用中, 0=空き */
    int owner;                /* open 時の res_owner_get() */
    Os32SqliteGroup group;
    int isolated;             /* close failed: never touch this connection again */
    int cleanup_error;        /* first teardown failure, retained until reuse */
    char cleanup_message[DB_ERROR_SIZE];
    sqlite3 *db;              /* SQLite 接続ハンドル */
    sqlite3_stmt *active_stmt; /* 実行中のステートメント */
    /* ---- v50 (票 S0-K §1a) ---- */
    int last_error;           /* 「最後の失敗」拡張コード (診断専用) */
    int used_once;            /* 一度でも open されたか (未使用 slot = MISUSE) */
    int bindable;             /* prepare_only 済みで、まだ step していない */
} DbSlot;

static DbSlot db_slots[DB_MAX_CONNECTIONS];

/* v50: 呼び手 (owner) ごとの「直前の open 失敗」。db_error_code(-1) で読む。
 * open は slot を取れないまま失敗しうるので、slot とは別の欄が要る。 */
static int db_open_fail[DB_OWNER_SLOTS];

/* 共有メモリベースアドレス (IPC用) */
/* MEM_SHM_DB_BASE と同じ先頭を使う。offset=0 は既存 ABI の結果番地契約。
 * リンカ由来の浮動番地を STATIC_ASSERT の条件に入れない。 */
STATIC_ASSERT(MEM_SHM_DB_OFFSET == 0, db_shm_at_band_start);
#define DB_SHM_PTR   ((u8 *)MEM_SHM_DB_BASE)

#include "kapi_db_helpers.inc"

/* ======================================================================== */
/*  KAPI 関数実装                                                            */
/* ======================================================================== */

static void slot_save_error(DbSlot *slot, int rc);
static int db_journal_fits(const char *path);
static int db_ieq(const char *a, const char *b);

int __cdecl kapi_db_open(const char *path)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(1)) return -1;
    int i;
    int rc;

    /* 空きスロットを探す */
    for (i = 0; i < DB_MAX_CONNECTIONS; i++) {
        if (!db_slots[i].in_use) break;
    }
    if (i >= DB_MAX_CONNECTIONS) {
        open_fail_set(SQLITE_FULL);
        return -1;
    }

    /* パス文字列をカーネルバッファにコピー (外部プログラム空間からの読み取り問題回避)。
     * **切り詰めない** (票 TASK_VFS_FD_PATH v3 の追記)。以前は kstrncpy で
     * 254 バイトに切っていたので、長い名前は別の DB (と別のジャーナル) を
     * 開いた。上限内に NUL が無ければ断る。 */
    if (!db_copy_input(path, path_copy_buf, PATH_COPY_BUF_SIZE, 1)) {
        open_fail_set(SQLITE_CANTOPEN);
        shm_write_error_text("path too long or unreadable");
        return -1;
    }
    /* `<名前>-journal` が 255 バイトに収まらない名前は**開く時点で**断る
     * (Opus 実装レビュー ラリー 1 の nb2)。開けてしまうと、読めるのに最初の
     * 書き込みでジャーナルを開けない接続を返す。下の SQLite VFS (xOpen) も
     * 同じ検査で断るが、ここで断ると理由を db_last_error() に残せる。
     * ":memory:" と "" (一時 DB) はファイルを持たないので見ない。 */
    if (path_copy_buf[0] != '\0' && !db_ieq(path_copy_buf, ":memory:") &&
        !db_journal_fits(path_copy_buf)) {
        open_fail_set(SQLITE_CANTOPEN);
        shm_write_error_text("path too long (NAMETOOLONG): "
                             "<name>-journal exceeds 255 bytes");
        return -1;
    }

    if (os32_sqlite_group_acquire(OS32_SQLITE_EXEC, res_owner_get(),
                                  &db_slots[i].group) != SQLITE_OK) {
        open_fail_set(SQLITE_FULL);
        return -1;
    }
    db_slots[i].db = (sqlite3 *)0;
    db_slots[i].cleanup_error = SQLITE_OK;
    db_slots[i].cleanup_message[0] = '\0';
    /* v50: slot を配り直したので診断欄も初期化する (前の持ち主の「最後の
     * 失敗」はここで消える = 再利用まで保持、という規則の実装)。 */
    db_slots[i].last_error = SQLITE_OK;
    db_slots[i].used_once = 1;
    db_slots[i].bindable = 0;
    db_slots[i].owner = res_owner_get();
    rc = os32_sqlite_group_open(&db_slots[i].group, path_copy_buf,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &db_slots[i].db);
    if (rc != SQLITE_OK) {
        /* Open can fail with a live connection. Save before teardown. */
        slot_save_error(&db_slots[i], rc);
        slot_note(&db_slots[i], slot_ext(&db_slots[i], rc));
        open_fail_set(db_slots[i].last_error);
        shm_write_error(&db_slots[i]);
        if (db_slots[i].db) {
            db_slots[i].in_use = 1;
            kapi_db_close(i);
        } else {
            os32_sqlite_group_begin_close(&db_slots[i].group);
            if (os32_sqlite_group_finish(&db_slots[i].group, SQLITE_OK) != SQLITE_OK)
                db_slots[i].in_use = db_slots[i].isolated = 1;
        }
        return -1;
    }

    /* ジャーナルモード設定 (ファイルDB のクラッシュリカバリ用) */
    sqlite3_exec(db_slots[i].db, "PRAGMA journal_mode=DELETE", 0, 0, 0);

    db_slots[i].in_use = 1;
    db_slots[i].active_stmt = (sqlite3_stmt *)0;
    open_fail_set(SQLITE_OK);
    return i;
}

/* Copy before any subsequent SQLite call can replace the diagnostic. */
static void slot_save_error(DbSlot *slot, int rc)
{
    if (rc == SQLITE_OK || slot->cleanup_error != SQLITE_OK) return;
    slot->cleanup_error = rc;
    kstrncpy(slot->cleanup_message,
             slot->db ? sqlite3_errmsg(slot->db) : sqlite3_errstr(rc),
             DB_ERROR_SIZE - 1);
    slot->cleanup_message[DB_ERROR_SIZE - 1] = '\0';
}

int __cdecl kapi_db_close(int handle)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(1)) return -1;
    DbSlot *slot = slot_get(handle);
    int rc = SQLITE_OK;
    if (!slot) return -1;
    if (os32_sqlite_group_begin_close(&slot->group) != SQLITE_OK) {
        slot->isolated = 1;
        return -1;
    }
    if (slot->active_stmt) {
        rc = sqlite3_finalize(slot->active_stmt);
        slot->active_stmt = (sqlite3_stmt *)0;
        /* The statement is gone even when its last evaluation failed. */
        slot_save_error(slot, rc);
        if (rc != SQLITE_OK) slot_note_teardown(slot, rc);
        rc = SQLITE_OK;
    }
    slot->bindable = 0;
    if (!sqlite3_get_autocommit(slot->db))
        rc = sqlite3_exec(slot->db, "ROLLBACK", 0, 0, 0);
    if (rc != SQLITE_OK) {
        slot_save_error(slot, rc);
        slot_note_teardown(slot, rc);
        os32_sqlite_group_quarantine(&slot->group, OS32_SQLITE_STAGE_CLOSE, rc);
        slot->isolated = 1;
        return -1;
    }
    rc = sqlite3_close(slot->db);
    if (rc == SQLITE_OK) slot->db = (sqlite3 *)0;
    else {
        slot_save_error(slot, rc);
        slot_note_teardown(slot, rc);
    }
    rc = os32_sqlite_group_finish(&slot->group, rc);
    if (rc != SQLITE_OK) {
        slot_save_error(slot, rc);
        slot_note_teardown(slot, rc);
        slot->isolated = 1;
        return -1;
    }
    slot->in_use = 0;
    return 0;
}

int __cdecl kapi_db_exec(int handle, const char *sql)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(1)) return -1;
    DbSlot *slot = slot_get(handle);
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (!slot) {
        shm_write_error((DbSlot *)0);
        return -1;
    }

    /* B3: copy fully before finalizing or changing the slot/SHM. */
    if (!db_copy_input(sql, sql_copy_buf, SQL_COPY_BUF_SIZE, 1) ||
        !sql_copy_buf[0]) return -1;

    /* 前のステートメントがあれば解放 */
    if (kapi_db_finalize(handle) != 0) {
        shm_write_error(slot);
        return -1;
    }

    rc = sqlite3_prepare_v2(slot->db, sql_copy_buf, -1, &stmt, 0);
    if (rc != SQLITE_OK || !stmt) {
        kprintf(KLOG_LEGACY_RED, "[DB] prepare fail rc=%d\n", rc);
        slot_note(slot, slot_ext(slot, rc != SQLITE_OK ? rc : SQLITE_MISUSE));
        shm_write_error(slot);
        return -1;
    }

    /* step */
    rc = sqlite3_step(stmt);
    /* v50: 自動 finalize が診断を差し替える前に拡張コードを控える。 */
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) slot_note(slot, slot_ext(slot, rc));

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        shm_write_error(slot);
        return -1;
    }

    slot_note(slot, SQLITE_OK);
    hdr->status = DB_STATUS_DONE;
    hdr->column_count = 0;
    hdr->error_offset = 0;
    return 0;
}

/* prepare の自動 step と明示的な step の結果を同じ順序で公開する。 */
static int slot_step_result(DbSlot *slot, int rc)
{
    if (rc == SQLITE_ROW) {
        slot_note(slot, SQLITE_OK);
        return shm_write_row(slot);
    } else if (rc == SQLITE_DONE) {
        DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
        slot_note(slot, SQLITE_OK);
        sqlite3_finalize(slot->active_stmt);
        slot->active_stmt = (sqlite3_stmt *)0;
        hdr->status = DB_STATUS_DONE;
        hdr->column_count = 0;
        hdr->error_offset = 0;
        return DB_STATUS_DONE;
    } else {
        /* v50: 自動 finalize より前に拡張コードを控える。 */
        slot_note(slot, slot_ext(slot, rc));
        shm_write_error(slot);
        sqlite3_finalize(slot->active_stmt);
        slot->active_stmt = (sqlite3_stmt *)0;
        return -1;
    }
}

int __cdecl kapi_db_prepare(int handle, const char *sql)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(1)) return -1;
    DbSlot *slot = slot_get(handle);
    int rc;

    if (!slot) {
        shm_write_error((DbSlot *)0);
        return -1;
    }

    /* B3: copy fully before finalizing or changing the slot/SHM. */
    if (!db_copy_input(sql, sql_copy_buf, SQL_COPY_BUF_SIZE, 1) ||
        !sql_copy_buf[0]) return -1;

    /* 前のステートメントがあれば解放 */
    if (kapi_db_finalize(handle) != 0) {
        shm_write_error(slot);
        return -1;
    }

    rc = sqlite3_prepare_v2(slot->db, sql_copy_buf, -1, &slot->active_stmt, 0);
    if (rc != SQLITE_OK) {
        slot_note(slot, slot_ext(slot, rc));
        shm_write_error(slot);
        return -1;
    }

    /* 最初の行を自動取得 */
    return slot_step_result(slot, sqlite3_step(slot->active_stmt));
}

int __cdecl kapi_db_step(int handle)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(1)) return -1;
    DbSlot *slot = slot_get(handle);

    if (!slot) {
        shm_write_error((DbSlot *)0);
        return -1;
    }

    if (!slot->active_stmt) {
        DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
        /* v50: ここも DONE = 成功したデータ操作。記録しないと、直前の
         * prepare_only の失敗コードが db_error_code に残り続ける。 */
        slot_note(slot, SQLITE_OK);
        hdr->status = DB_STATUS_DONE;
        hdr->column_count = 0;
        hdr->error_offset = 0;
        return DB_STATUS_DONE;
    }

    slot->bindable = 0;    /* v50: 最初の step 以降は bind を受け付けない */
    return slot_step_result(slot, sqlite3_step(slot->active_stmt));
}

int __cdecl kapi_db_column_int(int handle, int col)
{
    if (!DB_MODULE_READY() || !SQLITE_OUTER_READY(0)) return 0;
    DbSlot *slot = slot_get(handle);
    if (!slot || !slot->active_stmt) return 0;
    return sqlite3_column_int(slot->active_stmt, col);
}

const char * __cdecl kapi_db_column_text(int handle, int col)
{
    if (!DB_MODULE_READY() || !SQLITE_OUTER_READY(0)) return db_shm_empty();
    DbSlot *slot = slot_get(handle);
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    DB_ColumnInfo *info;

    /* 空文字列も**共有メモリ上**を指す。カーネルの .rodata は CPL=3 から
     * 読めないので、`return "";` は msg[0] を見るだけで #PF になる。 */
    if (!slot || !slot->active_stmt) return db_shm_empty();

    /* 共有メモリ上のカラム情報からデータ位置を参照 */
    /* SHM is caller-writable: its column_count is not a capacity check. */
    if (col < 0 || (u32)col >=
        (DB_SHM_RESULT_LIMIT - sizeof(DB_ResultHeader)) / sizeof(DB_ColumnInfo) ||
        col >= hdr->column_count) return db_shm_empty();
    info = (DB_ColumnInfo *)(DB_SHM_PTR + sizeof(DB_ResultHeader)
                             + (u32)col * sizeof(DB_ColumnInfo));
    if (info->data_offset <= 0 || (u32)info->data_offset >= DB_SHM_RESULT_LIMIT)
        return db_shm_empty();
    return (const char *)(DB_SHM_PTR + info->data_offset);
}

/* ステートメント手動 finalize — 結果セット途中放棄時に使用 */
/* db_step(DONE) 時は自動 finalize されるため、通常は不要 */
/* 戻り値: 0=成功, -1=失敗 */
int __cdecl kapi_db_finalize(int handle)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(1)) return -1;
    DbSlot *slot = slot_get(handle);
    int rc = SQLITE_OK;
    if (!slot) return -1;
    if (slot->active_stmt) {
        rc = sqlite3_finalize(slot->active_stmt);
        slot->active_stmt = (sqlite3_stmt *)0;
    }
    slot->bindable = 0;
    if (rc != SQLITE_OK) {
        slot_note_teardown(slot, rc);
    }
    return 0;
}

/* 返り値は CPL=3 のアプリが読む。**4 経路すべて**が元はカーネル番地だった
 * (.rodata の定数 / slot の .bss / SQLite の帯) ので、返す前に共有メモリの
 * 診断領域へ写す (票 TASK_DB_ERRSTR)。DB_ResultHeader.error_offset は
 * 1 回の db_exec の結果に結びついた別の欄で、こちらは動かさない。 */
const char * __cdecl kapi_db_last_error(int handle)
{
    if (!DB_MODULE_READY()) return db_shm_diag("SQLite module unavailable");
    if (!SQLITE_OUTER_READY(0)) return db_shm_diag("SQLite engine unavailable");
    DbSlot *slot;
    slot = slot_diag(handle);
    if (!slot) return db_shm_diag("invalid handle");
    if (slot->cleanup_error != SQLITE_OK)
        return db_shm_diag(slot->cleanup_message);
    if (!slot->in_use || !slot->db)
        return db_shm_diag("invalid handle");
    return db_shm_diag(sqlite3_errmsg(slot->db));
}

u32 __cdecl kapi_db_mem_used(void)
{
    if (!DB_MODULE_READY() || !SQLITE_OUTER_READY(0)) return 0;
    return (u32)sqlite3_memory_used();
}

#include "kapi_db_v50.inc"

#include "kapi_db_selftest.inc"

/* ======================================================================== */
/*  db_cleanup_all — exec_exit() から呼ばれるリソースクリーンアップ           */
/* ======================================================================== */
void db_cleanup_owned(int owner)
{
    int i;
    for (i = 0; i < DB_MAX_CONNECTIONS; i++) {
        if (db_slots[i].in_use && !db_slots[i].isolated &&
            db_slots[i].owner == owner)
            kapi_db_close(i);
    }
}

void db_cleanup_all(void)
{
    int i;
    for (i = 0; i < DB_MAX_CONNECTIONS; i++) {
        if (db_slots[i].in_use && !db_slots[i].isolated)
            kapi_db_close(i);
    }
}
