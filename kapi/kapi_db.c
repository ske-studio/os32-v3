/* ======================================================================== */
/*  KAPI_DB.C — SQLite DB KAPI ラッパー実装                                  */
/*                                                                          */
/*  外部プログラムが KAPI 関数テーブル経由で SQLite を操作するための            */
/*  カーネル側ブリッジ。DB 接続スロットで最大 DB_MAX_CONNECTIONS 個の           */
/*  同時接続を管理する。                                                      */
/*                                                                          */
/*  結果データは共有メモリ (MEM_SHM_BASE) に DB_ResultHeader +               */
/*  DB_ColumnInfo[] + データ の形式で書き込まれる。                            */
/* ======================================================================== */

#include "kapi_db.h"
#include "os32_kapi_slots.h"   /* v50: 自己診断が slot 番号を名前で見る */
#include "os32_sqlite_config.h"
#include "sqlite3.h"
#include "kstring.h"
#include "kprintf.h"
#include "os32_sqlite_vfs.h"
#include "memmap.h"
#include "fd_redirect.h"
#include "vfs.h"          /* v50: open 前の vfs_stat (本体 size / journal) */
#include "exec.h"         /* v50: ring3_user_range_ok (ユーザポインタ検証) */

/* kapi_db.c はリンカスクリプトで EXCLUDE_FILE に含まれていないため、
 * 通常の .text に配置される。sqlite3_exec 等は .sqlite_text にあるが、
 * カーネルの .text から .sqlite_text を呼ぶのは問題ない。
 * ただし、SQL文字列は外部プログラム空間 (0x400000+) から渡される。
 * sqlite3_exec が SQL を読み取る際にページフォールトが発生する場合、
 * カーネル側にコピーする必要がある。
 */
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
#define DB_SHM_PTR   ((u8 *)MEM_SHM_BASE)

/* ======================================================================== */
/*  ヘルパー: CPL=3 から読める文字列 (票 TASK_DB_ERRSTR §4)                  */
/*                                                                          */
/*  共有メモリはアプリの PD に RW+USER で見えているが、カーネルの            */
/*  .rodata / .data と SQLite の帯 (0x200000〜0x2FFFFF) は見えない。         */
/*  だから `const char *` を返す KAPI は、返す前に共有メモリの**診断領域**   */
/*  (ブロック 0 の末尾、os32_kapi_shared.h の DB_SHM_DIAG_* が唯一の管理元)  */
/*  へ写す。結果データ側の上限は DB_SHM_RESULT_LIMIT なので、両者は重ならない。*/
/* ======================================================================== */

/* 切れたと分かる印。写せなかったぶんを「写せた」ことにしないため。 */
#define DB_SHM_DIAG_ELLIPSIS "..."

/* 常に NUL の 1 バイト。空文字列を返す経路はここを指す。SHM の初期化順に
 * 依らないよう、返すたびに NUL を置く。 */
static const char *db_shm_empty(void)
{
    char *p = (char *)(DB_SHM_PTR + DB_SHM_EMPTY_OFFSET);
    *p = '\0';
    return (const char *)p;
}

/* 診断文を診断領域へ写して、その先頭を返す。
 *   - 上限で切り、**必ず NUL で終える**
 *   - 切ったら印を付ける (切り詰めを成功に見せない)
 *   - 切り口は UTF-8 の文字境界まで戻す (途中で切ると □ が出る)
 * 写した後は SQLite 側が何をしても安全 = sqlite3_errmsg() の寿命に依存しない。 */
static const char *db_shm_diag(const char *msg)
{
    char *dst = (char *)(DB_SHM_PTR + DB_SHM_DIAG_OFFSET);
    u32 cap = (u32)DB_SHM_ERRSTR_MAX - 1u;      /* NUL を除いて書ける量 */
    u32 mark = (u32)sizeof(DB_SHM_DIAG_ELLIPSIS) - 1u;
    u32 len;

    if (!msg) msg = "";
    len = kstrlen(msg);
    if (len <= cap) {
        kmemcpy(dst, msg, len);
        dst[len] = '\0';
        return (const char *)dst;
    }

    /* 切る。印の分を空けてから、継続バイトの上で止まらないよう戻す。 */
    len = cap - mark;
    while (len > 0u && ((u8)msg[len] & 0xC0u) == 0x80u) len--;
    kmemcpy(dst, msg, len);
    kmemcpy(dst + len, DB_SHM_DIAG_ELLIPSIS, mark);
    dst[len + mark] = '\0';
    return (const char *)dst;
}

/* ======================================================================== */
/*  ヘルパー: スロット検証                                                   */
/* ======================================================================== */
static DbSlot *slot_get(int handle)
{
    if (handle < 0 || handle >= DB_MAX_CONNECTIONS) return (DbSlot *)0;
    if (!db_slots[handle].in_use || db_slots[handle].isolated)
        return (DbSlot *)0;
    return &db_slots[handle];
}

/* ======================================================================== */
/*  ヘルパー: 診断専用の slot 引き (v50)                                      */
/*                                                                          */
/*  slot_get() は解放済み / 隔離 slot を弾くので、db_error_code には使えない。 */
/*  「閉じた handle でも slot が再利用されるまで最後の失敗を返す」ためには    */
/*  in_use / isolated を見ない引き方が要る。                                  */
/* ======================================================================== */
static DbSlot *slot_diag(int handle)
{
    if (handle < 0 || handle >= DB_MAX_CONNECTIONS) return (DbSlot *)0;
    return &db_slots[handle];
}

/* v50 の「最後の失敗」規則 (票 §1a)。データ操作 (open_existing / prepare_only /
 * bind_* / step / exec) は **成功で 0 に**、失敗でそのコードに更新する。 */
static void slot_note(DbSlot *slot, int rc)
{
    if (slot) slot->last_error = rc;
}

/* finalize / close は **失敗したときだけ** 更新する。後片付けが原因診断を
 * 消さないための規則 (FOUNDATION §4、Codex 往復 3 の 2)。 */
static void slot_note_teardown(DbSlot *slot, int rc)
{
    if (slot && rc != SQLITE_OK) slot->last_error = rc;
}

/* SQLite の拡張 result code。接続が無い / 拡張コードが無いときは rc のまま。 */
static int slot_ext(DbSlot *slot, int rc)
{
    int ext;
    if (!slot || !slot->db) return rc;
    ext = sqlite3_extended_errcode(slot->db);
    return ext != SQLITE_OK ? ext : rc;
}

/* v50: owner 別の「直前 open 失敗」欄。owner が池の外なら記録しない。 */
static void open_fail_set(int code)
{
    int owner = res_owner_get();
    if (owner < 0 || owner >= DB_OWNER_SLOTS) return;
    db_open_fail[owner] = code;
}

/* ======================================================================== */
/*  ヘルパー: ユーザポインタの検証とコピー (票 S0-K §1a)                      */
/*                                                                          */
/*  ディスパッチャの早期検証は先頭番地しか見ない。長さ付き / NUL 終端の       */
/*  引数はここで範囲を確かめてから **カーネル側のスクラッチへ写す**。         */
/*  CPL=0 の呼び手 (常駐シェル / gshell の直呼び) では ring3_user_range_ok が  */
/*  素通しになるので、NULL と長さと容量だけを見ることになる。                 */
/* ======================================================================== */
static int db_user_range_ok(const void *p, u32 len)
{
    u32 a = (u32)p;
    if (!p) return 0;
    if (a + len < a) return 0;            /* 加算 overflow */
    return ring3_user_range_ok(a, len);
}

/* 上限 cap (NUL 込み) の中で NUL を探しながら dst へ写す。
 * 1 バイトずつ検証するので、途中のページが非 present でもそこで止まる。
 * 戻り値: 1 = 写した / 0 = NULL・帯外・cap 内に NUL が無い (切り捨てない)。 */
static int db_user_str_copy(const char *src, char *dst, u32 cap)
{
    u32 i;
    if (!src) return 0;
    for (i = 0; i < cap; i++) {
        if (!db_user_range_ok(src + i, 1)) return 0;
        dst[i] = src[i];
        if (dst[i] == '\0') return 1;
    }
    return 0;
}

/* prepare_only の pzTail が「次の statement を含まない」か (票 §1a)。
 *
 * 自前の空白 / コメント表は **必ず SQLite とずれる** (Codex 往復 2 の B1):
 * トークナイザの先頭判定は 5 文字だが続きの走査は `sqlite3Isspace()` (6 文字、
 * `\v` を含む) で回るし、UTF-8 BOM は TK_SPACE として読み飛ばされるし、
 * 閉じていないブロックコメントは SQLite 側では空白にならない。
 * そこで **判定そのものを SQLite にさせる**: 末尾をもう一度 prepare して
 *   - rc == SQLITE_OK かつ stmt が NULL → 末尾は空白 / コメントだけ = 単一
 *   - stmt が返る                        → 次の statement がある = 拒否
 *   - rc != SQLITE_OK                    → SQLite が読めない末尾 = 拒否
 * 戻り値: SQLITE_OK = 単一 statement / それ以外 = 拒否する理由のコード。 */
static int sql_tail_check(sqlite3 *db, const char *tail)
{
    sqlite3_stmt *extra = (sqlite3_stmt *)0;
    int rc, code;

    if (!tail || !*tail) return SQLITE_OK;
    rc = sqlite3_prepare_v2(db, tail, -1, &extra, 0);
    if (rc != SQLITE_OK) {
        code = sqlite3_extended_errcode(db);
        if (code == SQLITE_OK) code = rc;
        if (extra) sqlite3_finalize(extra);
        return code;
    }
    if (extra) {
        sqlite3_finalize(extra);
        return SQLITE_MISUSE;             /* 2 本目の statement */
    }
    return SQLITE_OK;
}

/* ======================================================================== */
/*  ヘルパー: 共有メモリにエラー情報を書き込む                                */
/* ======================================================================== */
static void shm_write_error_text(const char *errmsg)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    i32 data_start;
    i32 max_len;

    hdr->status = DB_STATUS_ERROR;
    hdr->column_count = 0;

    /* エラーメッセージをデータ領域に書き込む */
    data_start = (i32)sizeof(DB_ResultHeader);
    hdr->error_offset = data_start;

    max_len = DB_SHM_RESULT_LIMIT - data_start - 1;
    if (max_len > 0) {
        kstrncpy((char *)(DB_SHM_PTR + data_start), errmsg, (u32)max_len);
    }
}

static void shm_write_error(DbSlot *slot)
{
    const char *errmsg;

    if (slot && slot->cleanup_error != SQLITE_OK) {
        errmsg = slot->cleanup_message;
    } else if (slot && slot->db) {
        errmsg = sqlite3_errmsg(slot->db);
    } else {
        errmsg = "invalid handle";
    }
    shm_write_error_text(errmsg);
}

/* ======================================================================== */
/*  ヘルパー: 1 行が 16KB の結果ブロックに収まるか (票 S0-K §1b、F5 の前倒し) */
/*                                                                          */
/*  header + **全列の descriptor** + payload の合計で見る。descriptor 領域だけ */
/*  でも 16KB を超えられる (列数の上限は SQLite 側の 2000) ので、1 列ずつ     */
/*  「入るなら書く」では遅い — 書き始める前に総量を数える。                   */
/*  戻り値: 1 = 収まる / 0 = 超過 (呼び手は -1 で返し、部分 ROW を返さない)。 */
/* ======================================================================== */
/* 純関数の側 (自己診断・ホスト試験が stmt 無しで踏める)。 */
static int shm_row_fits_n(int ncol, u32 payload)
{
    u32 need = (u32)sizeof(DB_ResultHeader);

    if (ncol < 0) return 0;
    if ((u32)ncol > ((u32)DB_SHM_RESULT_LIMIT - need) / (u32)sizeof(DB_ColumnInfo))
        return 0;
    need += (u32)ncol * (u32)sizeof(DB_ColumnInfo);
    if (payload > (u32)DB_SHM_RESULT_LIMIT - need) return 0;
    return 1;
}

/* 行を書く前の 1 巡目 (Codex 往復 2 の B3)。ここで **列値を実体化** し、
 *   - 16KB に収まるか
 *   - 実体化そのものが失敗していないか (MEMSYS5 プールは 384KB しかない。
 *     `sqlite3_column_blob/text` が NULL を返す・`sqlite3_errcode` が NOMEM に
 *     なる = 値が取れていない。長さだけ見て書くと data_offset = 0 の
 *     **欠落 ROW を成功として**返してしまう)
 * を確かめる。2 巡目 (shm_write_row) の accessor は SQLite の中で
 * キャッシュ済みの値を返すので、確保は増えない。
 * 戻り値: SQLITE_OK = 書いてよい / SQLITE_TOOBIG = 溢れる /
 *         その他 = 実体化の失敗コード (NOMEM 等)。 */
static int shm_row_check(sqlite3 *db, sqlite3_stmt *stmt, int ncol)
{
    u32 payload = 0;
    int i;

    if (!shm_row_fits_n(ncol, 0)) return SQLITE_TOOBIG;  /* descriptor で溢れる */
    for (i = 0; i < ncol; i++) {
        u32 add;
        int len;
        const void *p;
        switch (sqlite3_column_type(stmt, i)) {
        case SQLITE_INTEGER:
        case SQLITE_FLOAT:
            add = 4u;
            break;
        case SQLITE_TEXT:
        case SQLITE_BLOB:
            if (sqlite3_column_type(stmt, i) == SQLITE_TEXT)
                p = (const void *)sqlite3_column_text(stmt, i);
            else
                p = sqlite3_column_blob(stmt, i);
            len = sqlite3_column_bytes(stmt, i);
            /* 実体化の失敗の 2 つの形:
             *   (a) ポインタが無いのに長さがある
             *   (b) 長さが 0 に潰れているが SQLite が NOMEM を立てている
             *       (`sqlite3_column_bytes` は確保に失敗すると 0 を返す)
             * 0 バイトの値は NULL ポインタでも正当なので、(b) は errcode で
             * しか見分けられない。TEXT / BLOB の両方で同じ規則。 */
            if ((!p && len > 0) ||
                (db && sqlite3_errcode(db) == SQLITE_NOMEM)) {
                int code = db ? sqlite3_extended_errcode(db) : SQLITE_NOMEM;
                /* 直前の step が ROW / DONE を返していると errcode はそれを
                 * 映したまま。値が取れなかった理由の説明にならないので
                 * NOMEM (確保の失敗) に寄せる。 */
                if (code == SQLITE_OK || code == SQLITE_ROW ||
                    code == SQLITE_DONE) code = SQLITE_NOMEM;
                return code;
            }
            if (len < 0) return SQLITE_TOOBIG;
            add = (u32)len;
            if (sqlite3_column_type(stmt, i) == SQLITE_TEXT)
                add += 1u;                 /* 終端 NUL の分 */
            break;
        default:
            add = 0u;                      /* NULL は payload を持たない */
            break;
        }
        if (add > (u32)DB_SHM_RESULT_LIMIT - payload) return SQLITE_TOOBIG;
        payload += add;
    }
    return shm_row_fits_n(ncol, payload) ? SQLITE_OK : SQLITE_TOOBIG;
}

/* ======================================================================== */
/*  ヘルパー: db_step 結果を共有メモリに書き込む                              */
/* ======================================================================== */
static int shm_write_row(DbSlot *slot)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    DB_ColumnInfo *cols;
    int ncol;
    i32 data_offset;
    int i;

    if (!slot->active_stmt) {
        hdr->status = DB_STATUS_DONE;
        hdr->column_count = 0;
        hdr->error_offset = 0;
        return DB_STATUS_DONE;
    }

    ncol = sqlite3_column_count(slot->active_stmt);
    /* v50 (票 §1b): 収まらない行と **実体化に失敗した行** は 1 バイトも書かず
     * に失敗させる。部分 ROW も返さない — 呼び手が「取れた」と誤解しないため。
     * stmt は生かしたまま返す (finalize は呼び手の db_finalize / db_close)。 */
    {
        int chk = shm_row_check(slot->db, slot->active_stmt, ncol);
        if (chk != SQLITE_OK) {
            slot_note(slot, chk);
            shm_write_error_text(chk == SQLITE_TOOBIG
                                 ? "row exceeds the 16KB result block"
                                 : "column value could not be materialized");
            return DB_STATUS_ERROR;
        }
    }
    hdr->status = DB_STATUS_ROW;
    hdr->column_count = (i32)ncol;
    hdr->error_offset = 0;

    /* カラム情報配列の開始位置 */
    cols = (DB_ColumnInfo *)(DB_SHM_PTR + sizeof(DB_ResultHeader));

    /* データ領域の開始位置 */
    data_offset = (i32)(sizeof(DB_ResultHeader) + (u32)ncol * sizeof(DB_ColumnInfo));

    for (i = 0; i < ncol; i++) {
        int col_type = sqlite3_column_type(slot->active_stmt, i);
        i32 remaining = DB_SHM_RESULT_LIMIT - data_offset;

        cols[i].data_offset = data_offset;

        switch (col_type) {
        case SQLITE_INTEGER: {
            i32 val = (i32)sqlite3_column_int(slot->active_stmt, i);
            cols[i].type = DB_TYPE_INT;
            cols[i].length = 4;
            if (remaining >= 4) {
                *(i32 *)(DB_SHM_PTR + data_offset) = val;
                data_offset += 4;
            }
            break;
        }
        case SQLITE_TEXT: {
            const char *text = (const char *)sqlite3_column_text(slot->active_stmt, i);
            int len = sqlite3_column_bytes(slot->active_stmt, i);
            cols[i].type = DB_TYPE_TEXT;
            cols[i].length = len;
            /* 事前検査 (shm_row_fits) は len + 1 <= remaining を許す。
             * writer もちょうど収まる行は **書く** — 条件がずれると、
             * 事前検査を通った行が payload 無し (data_offset = 0) の
             * 欠落 ROW になり、しかも診断は成功のままになる。 */
            if (text && len >= 0 && len + 1 <= remaining) {
                memcpy(DB_SHM_PTR + data_offset, text, (u32)len);
                DB_SHM_PTR[data_offset + len] = '\0';
                data_offset += len + 1;
            } else {
                /* データが大きすぎる場合はオフセットを無効化 */
                cols[i].data_offset = 0;
            }
            break;
        }
        case SQLITE_FLOAT: {
            /* float は i32 に切り捨て (簡易対応) */
            double dval = sqlite3_column_double(slot->active_stmt, i);
            i32 ival = (i32)dval;
            cols[i].type = DB_TYPE_FLOAT;
            cols[i].length = 4;
            if (remaining >= 4) {
                *(i32 *)(DB_SHM_PTR + data_offset) = ival;
                data_offset += 4;
            }
            break;
        }
        case SQLITE_BLOB: {
            const void *blob = sqlite3_column_blob(slot->active_stmt, i);
            int len = sqlite3_column_bytes(slot->active_stmt, i);
            cols[i].type = DB_TYPE_BLOB;
            cols[i].length = len;
            /* TEXT と同じ理由で境界ちょうどを含める (事前検査は len <= remaining)。*/
            if (blob && len >= 0 && len <= remaining) {
                memcpy(DB_SHM_PTR + data_offset, blob, (u32)len);
                data_offset += len;
            } else {
                cols[i].data_offset = 0;
            }
            break;
        }
        case SQLITE_NULL:
        default:
            cols[i].type = DB_TYPE_NULL;
            cols[i].length = 0;
            cols[i].data_offset = 0;
            break;
        }
    }

    return DB_STATUS_ROW;
}

/* ======================================================================== */
/*  KAPI 関数実装                                                            */
/* ======================================================================== */

static void slot_save_error(DbSlot *slot, int rc);
static int db_journal_fits(const char *path);
static int db_ieq(const char *a, const char *b);

int __cdecl kapi_db_open(const char *path)
{
    int i;
    int rc;

    /* 空きスロットを探す */
    for (i = 0; i < DB_MAX_CONNECTIONS; i++) {
        if (!db_slots[i].in_use) break;
    }
    if (i >= DB_MAX_CONNECTIONS) return -1;

    /* パス文字列をカーネルバッファにコピー (外部プログラム空間からの読み取り問題回避)。
     * **切り詰めない** (票 TASK_VFS_FD_PATH v3 の追記)。以前は kstrncpy で
     * 254 バイトに切っていたので、長い名前は別の DB (と別のジャーナル) を
     * 開いた。上限内に NUL が無ければ断る。 */
    if (!db_user_str_copy(path, path_copy_buf, PATH_COPY_BUF_SIZE)) {
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

    db_slots[i].cleanup_error = SQLITE_OK;
    db_slots[i].cleanup_message[0] = '\0';
    /* v50: slot を配り直したので診断欄も初期化する (前の持ち主の「最後の
     * 失敗」はここで消える = 再利用まで保持、という規則の実装)。 */
    db_slots[i].last_error = SQLITE_OK;
    db_slots[i].used_once = 1;
    db_slots[i].bindable = 0;
    db_slots[i].owner = res_owner_get();
    rc = sqlite3_open(path_copy_buf, &db_slots[i].db);
    if (rc != SQLITE_OK) {
        /* Open can fail with a live connection. Save before teardown. */
        slot_save_error(&db_slots[i], rc);
        slot_note(&db_slots[i], slot_ext(&db_slots[i], rc));
        open_fail_set(db_slots[i].last_error);
        shm_write_error(&db_slots[i]);
        if (db_slots[i].db) {
            db_slots[i].in_use = 1;
            kapi_db_close(i);
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
    DbSlot *slot = slot_get(handle);
    int rc;
    if (!slot) return -1;

    /* 実行中ステートメントの finalize */
    if (slot->active_stmt) {
        int frc = sqlite3_finalize(slot->active_stmt);
        slot_save_error(slot, frc);
        slot_note_teardown(slot, frc);   /* v50: 失敗したときだけ診断へ */
        slot->active_stmt = (sqlite3_stmt *)0;
    }
    slot->bindable = 0;

    if (!sqlite3_get_autocommit(slot->db)) {
        int rrc = sqlite3_exec(slot->db, "ROLLBACK", 0, 0, 0);
        slot_save_error(slot, rrc);
        slot_note_teardown(slot, rrc);
    }
    rc = sqlite3_close(slot->db);
    if (rc != SQLITE_OK) {
        slot_save_error(slot, rc);
        slot_note_teardown(slot, rc);
        /* Never retry, even after this nest/owner is recycled. This does NOT
         * protect main/journal/temp FDs against generic FD cleanup: F2 must
         * establish complete VFS tracking/quarantine before integration. */
        slot->isolated = 1;
        return -1;
    }
    slot->db = (sqlite3 *)0;
    slot->in_use = 0;
    return slot->cleanup_error == SQLITE_OK ? 0 : -1;
}

int __cdecl kapi_db_exec(int handle, const char *sql)
{
    DbSlot *slot = slot_get(handle);
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (!slot) {
        shm_write_error((DbSlot *)0);
        return -1;
    }

    /* 前のステートメントがあれば解放 */
    if (slot->active_stmt) {
        sqlite3_finalize(slot->active_stmt);
        slot->active_stmt = (sqlite3_stmt *)0;
    }
    slot->bindable = 0;

    /* SQL文字列をカーネルバッファにコピー (外部プログラム空間からの読み取り問題回避) */
    kstrncpy(sql_copy_buf, sql, SQL_COPY_BUF_SIZE - 1);
    sql_copy_buf[SQL_COPY_BUF_SIZE - 1] = '\0';



    /* prepare */
    rc = sqlite3_prepare_v2(slot->db, sql_copy_buf, -1, &stmt, 0);
    if (rc != SQLITE_OK || !stmt) {
        kprintf(0x04, "[DB] prepare fail rc=%d\n", rc);
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

int __cdecl kapi_db_prepare(int handle, const char *sql)
{
    DbSlot *slot = slot_get(handle);
    int rc;
    int step_rc;

    if (!slot) {
        shm_write_error((DbSlot *)0);
        return -1;
    }

    /* 前のステートメントがあれば解放 */
    if (slot->active_stmt) {
        sqlite3_finalize(slot->active_stmt);
        slot->active_stmt = (sqlite3_stmt *)0;
    }
    slot->bindable = 0;

    /* SQL文字列をカーネルバッファにコピー */
    kstrncpy(sql_copy_buf, sql, SQL_COPY_BUF_SIZE - 1);
    sql_copy_buf[SQL_COPY_BUF_SIZE - 1] = '\0';

    rc = sqlite3_prepare_v2(slot->db, sql_copy_buf, -1, &slot->active_stmt, 0);
    if (rc != SQLITE_OK) {
        slot_note(slot, slot_ext(slot, rc));
        shm_write_error(slot);
        return -1;
    }

    /* 最初の行を自動取得 */
    step_rc = sqlite3_step(slot->active_stmt);
    if (step_rc == SQLITE_ROW) {
        slot_note(slot, SQLITE_OK);
        return shm_write_row(slot);
    } else if (step_rc == SQLITE_DONE) {
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
        slot_note(slot, slot_ext(slot, step_rc));
        shm_write_error(slot);
        sqlite3_finalize(slot->active_stmt);
        slot->active_stmt = (sqlite3_stmt *)0;
        return -1;
    }
}

int __cdecl kapi_db_step(int handle)
{
    DbSlot *slot = slot_get(handle);
    int rc;

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
    rc = sqlite3_step(slot->active_stmt);
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

int __cdecl kapi_db_column_int(int handle, int col)
{
    DbSlot *slot = slot_get(handle);
    if (!slot || !slot->active_stmt) return 0;
    return sqlite3_column_int(slot->active_stmt, col);
}

const char * __cdecl kapi_db_column_text(int handle, int col)
{
    DbSlot *slot = slot_get(handle);
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    DB_ColumnInfo *info;

    /* 空文字列も**共有メモリ上**を指す。カーネルの .rodata は CPL=3 から
     * 読めないので、`return "";` は msg[0] を見るだけで #PF になる。 */
    if (!slot || !slot->active_stmt) return db_shm_empty();

    /* 共有メモリ上のカラム情報からデータ位置を参照 */
    if (col < 0 || col >= hdr->column_count) return db_shm_empty();
    info = (DB_ColumnInfo *)(DB_SHM_PTR + sizeof(DB_ResultHeader)
                             + (u32)col * sizeof(DB_ColumnInfo));
    if (info->data_offset == 0) return db_shm_empty();
    return (const char *)(DB_SHM_PTR + info->data_offset);
}

/* ステートメント手動 finalize — 結果セット途中放棄時に使用 */
/* db_step(DONE) 時は自動 finalize されるため、通常は不要 */
/* 戻り値: 0=成功, -1=失敗 */
int __cdecl kapi_db_finalize(int handle)
{
    DbSlot *slot = slot_get(handle);
    if (!slot) return -1;
    if (slot->active_stmt) {
        /* v50: 戻り値は従来どおり 0 のまま (既存 10 本の挙動は不変)。
         * 失敗したときだけ診断欄を更新する。 */
        slot_note_teardown(slot, sqlite3_finalize(slot->active_stmt));
        slot->active_stmt = (sqlite3_stmt *)0;
    }
    slot->bindable = 0;
    return 0;
}

/* 返り値は CPL=3 のアプリが読む。**4 経路すべて**が元はカーネル番地だった
 * (.rodata の定数 / slot の .bss / SQLite の帯) ので、返す前に共有メモリの
 * 診断領域へ写す (票 TASK_DB_ERRSTR)。DB_ResultHeader.error_offset は
 * 1 回の db_exec の結果に結びついた別の欄で、こちらは動かさない。 */
const char * __cdecl kapi_db_last_error(int handle)
{
    DbSlot *slot;
    if (handle < 0 || handle >= DB_MAX_CONNECTIONS)
        return db_shm_diag("invalid handle");
    slot = &db_slots[handle];
    if (slot->cleanup_error != SQLITE_OK)
        return db_shm_diag(slot->cleanup_message);
    if (!slot->in_use || !slot->db)
        return db_shm_diag("invalid handle");
    return db_shm_diag(sqlite3_errmsg(slot->db));
}

u32 __cdecl kapi_db_mem_used(void)
{
    return (u32)sqlite3_memory_used();
}

/* ======================================================================== */
/*  v50 (票 S0-K §1a) — 設定レジストリの基盤 7 本                            */
/* ======================================================================== */

/* ASCII の大文字小文字を無視した比較 (PRAGMA の戻り値の照合にだけ使う)。 */
static int db_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

/* RW 要求のとき DELETE journal が成立しているかを **問い合わせだけ** で見る。
 * `PRAGMA journal_mode` (= を付けない形) は照会で、モードを 1 ビットも変えない。
 *
 * 戻り値は診断コード:
 *   SQLITE_OK        DELETE で確定
 *   SQLITE_CANTOPEN  照会は通ったが DELETE ではない (要求 mode 不成立)
 *   その他           照会そのものの失敗 (拡張 result code)。壊れた DB を RW で
 *                    開くと sqlite3_open_v2 は成功し、schema を読むここで
 *                    SQLITE_NOTADB / IOERR / NOMEM が出る。boolean に潰すと
 *                    それが全部 CANTOPEN になって原因が消える。
 * finalize は診断を差し替えるので、拡張コードは **finalize の前**に控える。 */
static int db_journal_mode_check(sqlite3 *db)
{
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    const char *mode;
    int rc, code;

    rc = sqlite3_prepare_v2(db, "PRAGMA journal_mode", -1, &stmt, 0);
    if (rc != SQLITE_OK || !stmt) {
        code = sqlite3_extended_errcode(db);
        if (code == SQLITE_OK) code = (rc != SQLITE_OK) ? rc : SQLITE_MISUSE;
        if (stmt) sqlite3_finalize(stmt);
        return code;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        code = sqlite3_extended_errcode(db);
        if (code == SQLITE_OK) code = (rc != SQLITE_DONE) ? rc : SQLITE_NOTADB;
        sqlite3_finalize(stmt);
        return code;
    }
    mode = (const char *)sqlite3_column_text(stmt, 0);
    code = (mode != (const char *)0 && db_ieq(mode, "delete"))
           ? SQLITE_OK : SQLITE_CANTOPEN;
    sqlite3_finalize(stmt);
    return code;
}

/* `/` 区切りの成分数。空成分と `.` は fs/vfs.c が捨てるので数えない。
 * `..` は **1 成分として数える** (resolver は畳むが、こちらは安全側)。 */
static u32 db_path_depth(const char *p)
{
    u32 n = 0;

    if (!p) return 0;
    while (*p) {
        u32 len = 0;
        while (*p == '/') p++;
        if (!*p) break;
        while (p[len] != '\0' && p[len] != '/') len++;
        if (!(len == 1u && p[0] == '.')) n++;
        p += len;
    }
    return n;
}

/* resolve の **前** に、fs/vfs.c の resolver が入力を「別の絶対名」に化け
 * させないかを見る。化ける口は 2 つあり、どちらも**解決結果からは分からない**:
 *
 *  (1) 長さ (Codex 往復 3)。vfs_resolve_path は VFS_MAX_PATH の作業領域に
 *      `cwd + "/" + input` を **strlcat で切り詰めてから** `.` / `..` を畳む。
 *      cwd=/tmp, input="./"×122 + "a/../b.db" → 連結 259B → 255B で切ると
 *      末尾が `a/../b` → 正規化して `/tmp/b` = **要求と違う DB**。
 *  (2) 深さ (Codex 最終往復)。成分表は VFS_MAX_PATH_DEPTH 本しかなく、
 *      溢れた成分は**黙って捨てられる**。その後ろの `..` は捨てられた成分
 *      ではなく **保持済みの成分**を 1 つ消す。
 *      `("/a"×32) + "/x/.." + ("/.."×31) + "/b.db"` は正しくは `/a/b.db` だが、
 *      `x` が捨てられた直後の `..` が `a` を消して `/b.db` になる。
 *
 * どちらも入口で数えて断る。正規化で短く / 浅くなる入力も巻き添えで断る
 * (安全側。呼び手は正規化済みの絶対名を渡せばよい)。
 * 戻り値: 1 = そのまま解決してよい / 0 = 断る (CANTOPEN, path too long)。 */
static int db_resolve_fits(const char *path)
{
    const char *cwd;
    u32 need, depth;

    if (!path || !path[0]) return 0;
    if (path[0] == '/') {
        need = kstrlen(path) + 1u;                  /* NUL 込み */
        depth = db_path_depth(path);
    } else {
        cwd = vfs_cwd();
        if (!cwd) cwd = "";
        /* fs/vfs.c は cwd が '/' で終わっていなければ 1 文字足す。常に
         * 足したものとして数える (1 バイト厳しい側に倒す)。 */
        need = kstrlen(cwd) + 1u + kstrlen(path) + 1u;
        depth = db_path_depth(cwd) + db_path_depth(path);
    }
    if (need > (u32)VFS_MAX_PATH) return 0;
    if (depth > (u32)VFS_MAX_PATH_DEPTH) return 0;
    return 1;
}

/* `<path>-journal` を journal_buf に組み立てる。1 = 組み立てた /
 * 0 = 下位層の path 容量に収まらない (= path too long、呼び手は CANTOPEN)。 */
static int db_journal_name(const char *path)
{
    u32 want = kstrlen(path) + (u32)sizeof(DB_JOURNAL_SUFFIX) - 1u;

    if (want + 1u > (u32)sizeof(journal_buf)) return 0;
    kstrncpy(journal_buf, path, (u32)sizeof(journal_buf));
    journal_buf[sizeof(journal_buf) - 1] = '\0';
    kstrncat(journal_buf, DB_JOURNAL_SUFFIX, (u32)sizeof(journal_buf));
    return kstrlen(journal_buf) == want;
}

/* `<path>-journal` が下位層の名前の上限 (NUL 抜き 255 バイト) に、解決前の
 * 名前 (SQLite は開いた名前に "-journal" を足す) と解決後の絶対名の両方で
 * 収まるか。1 = 収まる / 0 = 収まらない (か、解決できない)。abs_path_buf を使う */
static int db_journal_fits(const char *path)
{
    u32 sfx = (u32)sizeof(DB_JOURNAL_SUFFIX) - 1u;
    if (kstrlen(path) + sfx + 1u > (u32)VFS_MAX_PATH) return 0;
    abs_path_buf[0] = '\0';
    if (vfs_resolve_path(path, abs_path_buf, (int)sizeof(abs_path_buf)) != VFS_OK)
        return 0;
    return kstrlen(abs_path_buf) + sfx + 1u <= (u32)VFS_MAX_PATH;
}

int __cdecl kapi_db_open_existing(const char *path, int writable)
{
    int i, rc, flags;
    OS32_Stat st;
    sqlite3 *db = (sqlite3 *)0;

    if (writable != 0 && writable != 1) {
        open_fail_set(SQLITE_MISUSE);
        return -1;
    }
    /* 上限内で NUL を探しながら写す。超過は切り捨てず拒否。
     * **ポインタ検証の拒否**と**引数そのものの拒否**は契約上どちらも
     * SQLITE_MISUSE だが、実機で切り分けられないと原因に辿りつけない
     * (K2 の「21 が返る」がまさにこれ)。SHM のエラー文を分けて
     * db_last_error() で見分けられるようにする。 */
    if (!db_user_str_copy(path, path_copy_buf, PATH_COPY_BUF_SIZE)) {
        open_fail_set(SQLITE_MISUSE);
        shm_write_error_text("path pointer rejected by the range check");
        return -1;
    }
    if (path_copy_buf[0] == '\0' ||
        db_ieq(path_copy_buf, ":memory:") ||
        kstrncmp(path_copy_buf, "file:", 5) == 0) {
        open_fail_set(SQLITE_MISUSE);
        shm_write_error_text("path is empty, :memory: or a file: URI");
        return -1;
    }

    /* (0) 以降はすべて **解決後の絶対名** で扱う (Codex 往復 2 の B4)。
     * 解決に渡す前に、下位層の作業領域で切り詰められないことを確かめる
     * (Codex 往復 3: 切り詰め後に正規化されるので、溢れた入力は別の DB の
     * 絶対名として返ってくる)。 */
    if (!db_resolve_fits(path_copy_buf)) {
        open_fail_set(SQLITE_CANTOPEN);      /* path too long (解決前の長さ) */
        shm_write_error_text("path too long or too deep to resolve");
        return -1;
    }
    abs_path_buf[0] = '\0';
    if (vfs_resolve_path(path_copy_buf, abs_path_buf, (int)sizeof(abs_path_buf))
            != VFS_OK || abs_path_buf[0] == '\0') {
        open_fail_set(SQLITE_CANTOPEN);
        return -1;
    }

    /* (1) 本体が存在し size > 0 か。**SQLite を呼ぶ前**に見る (Codex 往復 2 の 1:
     * SQLite の hasHotJournal は RO でも 0 ページの DB に付随する journal を
     * 消してしまうので、その手前で止める = 副作用ゼロ)。
     * stat の失敗は「確定した不存在 (NOTFOUND)」と「それ以外」を分ける —
     * I/O 障害を不存在と同じ扱いにすると、journal の有無が分からないまま
     * SQLite に進むことになる。 */
    rc = vfs_stat(abs_path_buf, &st);
    if (rc != 0) {
        open_fail_set(rc == OS32_ERR_NOTFOUND ? SQLITE_CANTOPEN : SQLITE_IOERR);
        shm_write_error_text(rc == OS32_ERR_NOTFOUND
                             ? "the database file does not exist"
                             : "stat of the database file failed");
        return -1;
    }
    if (st.st_size == 0) {
        open_fail_set(SQLITE_NOTADB);
        shm_write_error_text("the database file is 0 bytes");
        return -1;
    }
    /* (2) hot journal があれば RO / RW とも失敗。回復は S3 の明示操作。
     * 「無い」と言い切れるのは NOTFOUND のときだけ。 */
    if (!db_journal_name(abs_path_buf)) {
        open_fail_set(SQLITE_CANTOPEN);      /* path too long (下位層の容量) */
        return -1;
    }
    rc = vfs_stat(journal_buf, &st);
    if (rc == 0) {
        open_fail_set(SQLITE_BUSY_RECOVERY);
        shm_write_error_text("a hot journal is present");
        return -1;
    }
    if (rc != OS32_ERR_NOTFOUND) {
        open_fail_set(SQLITE_IOERR);
        shm_write_error_text("stat of the journal failed");
        return -1;
    }

    /* 空きスロット */
    for (i = 0; i < DB_MAX_CONNECTIONS; i++) {
        if (!db_slots[i].in_use) break;
    }
    if (i >= DB_MAX_CONNECTIONS) {
        open_fail_set(SQLITE_FULL);
        return -1;
    }

    db_slots[i].cleanup_error = SQLITE_OK;
    db_slots[i].cleanup_message[0] = '\0';
    db_slots[i].last_error = SQLITE_OK;
    db_slots[i].used_once = 1;
    db_slots[i].bindable = 0;
    db_slots[i].active_stmt = (sqlite3_stmt *)0;
    db_slots[i].owner = res_owner_get();

    /* CREATE も URI も付けない。vfs は既定の "os32"。 */
    flags = writable ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY;
    rc = sqlite3_open_v2(abs_path_buf, &db, flags, (const char *)0);
    db_slots[i].db = db;
    if (rc != SQLITE_OK) {
        slot_save_error(&db_slots[i], rc);
        slot_note(&db_slots[i], slot_ext(&db_slots[i], rc));
        open_fail_set(db_slots[i].last_error);
        shm_write_error(&db_slots[i]);
        if (db) {
            db_slots[i].in_use = 1;
            kapi_db_close(i);
        }
        return -1;
    }

    /* RW は DELETE journal の成立を確認する。RO では変更 PRAGMA も照会も
     * 実行しない (FOUNDATION §2-2: RO は元ファイルと journal に触らない)。
     * 照会の失敗 (NOTADB / IOERR / NOMEM ...) と「照会は通ったが DELETE で
     * ない」(CANTOPEN) を区別して残す。 */
    if (writable) {
        int mode_rc = db_journal_mode_check(db);
        if (mode_rc != SQLITE_OK) {
            db_slots[i].in_use = 1;
            slot_note(&db_slots[i], mode_rc);
            open_fail_set(mode_rc);
            shm_write_error(&db_slots[i]);
            kapi_db_close(i);
            return -1;
        }
    }

    db_slots[i].in_use = 1;
    open_fail_set(SQLITE_OK);
    return i;
}

int __cdecl kapi_db_prepare_only(int handle, const char *sql)
{
    DbSlot *slot = slot_get(handle);
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    const char *tail = (const char *)0;
    int rc, tail_rc;

    if (!slot) {
        shm_write_error((DbSlot *)0);
        return -1;
    }

    /* **入口で必ず**旧 stmt を捨てる (Codex 往復 2 の B2)。§1a の「旧 stmt は
     * finalize して置換」は拒否理由に依らない — 引数検査で弾いたときに旧 stmt を
     * 残すと、bind 済みの前の statement が次の db_step で実行されてしまう
     * (呼び手からは「prepare は失敗したのに INSERT が走った」に見える)。 */
    if (slot->active_stmt) {
        slot_note_teardown(slot, sqlite3_finalize(slot->active_stmt));
        slot->active_stmt = (sqlite3_stmt *)0;
    }
    slot->bindable = 0;

    if (!db_user_str_copy(sql, sql_copy_buf, SQL_COPY_BUF_SIZE)) {
        slot_note(slot, SQLITE_MISUSE);
        shm_write_error_text("sql pointer rejected by the range check");
        return -1;
    }
    if (sql_copy_buf[0] == '\0') {
        slot_note(slot, SQLITE_MISUSE);
        shm_write_error_text("sql is empty");
        return -1;
    }

    rc = sqlite3_prepare_v2(slot->db, sql_copy_buf, -1, &stmt, &tail);
    if (rc != SQLITE_OK || !stmt) {
        if (stmt) sqlite3_finalize(stmt);
        slot_note(slot, slot_ext(slot, rc != SQLITE_OK ? rc : SQLITE_MISUSE));
        shm_write_error(slot);
        return -1;
    }
    /* 末尾に何かあるかは SQLite に判定させる (単一 statement の契約)。 */
    tail_rc = sql_tail_check(slot->db, tail);
    if (tail_rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        slot_note(slot, tail_rc);
        shm_write_error_text("only a single statement is allowed");
        return -1;
    }

    slot->active_stmt = stmt;     /* step はしない */
    slot->bindable = 1;
    slot_note(slot, SQLITE_OK);
    return 0;
}

/* bind の共通前提: prepare_only 済み・最初の step の前・index は 1-based。 */
static DbSlot *bind_slot(int handle, int index)
{
    DbSlot *slot = slot_get(handle);
    if (!slot) return (DbSlot *)0;
    if (!slot->active_stmt || !slot->bindable) {
        slot_note(slot, SQLITE_MISUSE);
        return (DbSlot *)0;
    }
    if (index < 1 || index > sqlite3_bind_parameter_count(slot->active_stmt)) {
        slot_note(slot, SQLITE_RANGE);
        return (DbSlot *)0;
    }
    return slot;
}

static int bind_done(DbSlot *slot, int rc)
{
    slot_note(slot, rc == SQLITE_OK ? SQLITE_OK : slot_ext(slot, rc));
    return rc == SQLITE_OK ? 0 : -1;
}

int __cdecl kapi_db_bind_int(int handle, int index, int value)
{
    DbSlot *slot = bind_slot(handle, index);
    if (!slot) return -1;
    return bind_done(slot, sqlite3_bind_int(slot->active_stmt, index, value));
}

int __cdecl kapi_db_bind_text(int handle, int index, const char *text, int length)
{
    DbSlot *slot = bind_slot(handle, index);
    if (!slot) return -1;
    if (length < 0 || length > DB_BIND_TEXT_MAX ||
        !db_user_range_ok(text, (u32)length)) {
        slot_note(slot, SQLITE_MISUSE);
        return -1;
    }
    if (length > 0) kmemcpy(text_copy_buf, text, (u32)length);
    text_copy_buf[length] = '\0';
    /* 0B でも非 NULL の空値。SQLITE_TRANSIENT なので SQLite が写す。 */
    return bind_done(slot, sqlite3_bind_text(slot->active_stmt, index,
                                             text_copy_buf, length,
                                             SQLITE_TRANSIENT));
}

int __cdecl kapi_db_bind_blob(int handle, int index, const void *data, int length)
{
    DbSlot *slot = bind_slot(handle, index);
    if (!slot) return -1;
    if (length < 0 || length > DB_BIND_BLOB_MAX ||
        !db_user_range_ok(data, (u32)length)) {
        slot_note(slot, SQLITE_MISUSE);
        return -1;
    }
    if (length > 0) kmemcpy(blob_copy_buf, data, (u32)length);
    return bind_done(slot, sqlite3_bind_blob(slot->active_stmt, index,
                                             blob_copy_buf, length,
                                             SQLITE_TRANSIENT));
}

int __cdecl kapi_db_bind_null(int handle, int index)
{
    DbSlot *slot = bind_slot(handle, index);
    if (!slot) return -1;
    return bind_done(slot, sqlite3_bind_null(slot->active_stmt, index));
}

int __cdecl kapi_db_error_code(int handle)
{
    DbSlot *slot;
    int owner;

    if (handle == -1) {
        owner = res_owner_get();
        if (owner < 0 || owner >= DB_OWNER_SLOTS) return SQLITE_MISUSE;
        return db_open_fail[owner];
    }
    slot = slot_diag(handle);           /* in_use / isolated を見ない診断引き */
    if (!slot || !slot->used_once) return SQLITE_MISUSE;
    return slot->last_error;
}

/* ======================================================================== */
/*  db_slot_layout_ok — KAPI 表の slot 配置が v50 / v51 の形を保っているか    */
/*                                                                          */
/*  見るのは **既存スロットの位置**であって、表の長さではない。              */
/*                                                                          */
/*  ここは以前 `KAPI_SLOT_COUNT != KAPI_SLOT_HOST_CLOSE + 1` と書いていた。   */
/*  それは「host_close の後ろに 1 本も足されていないこと」を要求するので、    */
/*  **[ABI2] が正当と定めた末尾追記のたびに必ず落ちる**。実際 2026-09-15 に   */
/*  票 H3 の `sys_set_mtime` (slot 213) で `make check` が落ちた。           */
/*  この項の意図は「追記した 7 本が 201..207 に居て、既存の `db_*` が        */
/*  動いていない」であって、表がそこで終わっていることではない。             */
/*                                                                          */
/*  だから長さは **下限だけ** 見る。slot_count を引数で受けるのは、          */
/*  「KAPI をもう 1 本足したら」をホスト試験から直に試せるようにするため      */
/*  (tools/tests/kapi_db_v50_host.c の slot_layout_append)。                 */
/*                                                                          */
/*  数値の直書きはしない ([C4]) — 位置は全部 os32_kapi_slots.h の定数から     */
/*  導く。合っていなければ外部プログラムは「別の関数を呼ぶ」という最も静かな  */
/*  壊れ方をする。                                                          */
/* ======================================================================== */
int db_slot_layout_ok(int slot_count)
{
    /* 表は少なくとも既知の末尾 slot を含む長さがある (**上限は見ない**) */
    if (slot_count <= KAPI_SLOT_HOST_CLOSE) return 0;

    /* v50 で追記した 7 本が 201..207 に居る */
    if (KAPI_SLOT_DB_OPEN_EXISTING != 201) return 0;
    if (KAPI_SLOT_DB_ERROR_CODE != 207) return 0;          /* db 帯の末尾 */
    if (KAPI_SLOT_DB_ERROR_CODE - KAPI_SLOT_DB_OPEN_EXISTING != 6) return 0;

    /* 既存 10 本は動いていない */
    if (KAPI_SLOT_DB_OPEN != 140) return 0;

    /* v51 の host 帯 5 本が 208..212 に居る。以前は「件数 == host_close + 1」
     * が間接的に押さえていただけだったので、**位置を明示して**見る。 */
    if (KAPI_SLOT_HOST_OPEN != 208) return 0;
    if (KAPI_SLOT_HOST_CLOSE != 212) return 0;
    if (KAPI_SLOT_HOST_CLOSE - KAPI_SLOT_HOST_OPEN != 4) return 0;

    return 1;
}

/* ======================================================================== */
/*  db_v50_selftest — ブート時に踏む v50 の骨 (kernel/kselftest.c から)      */
/* ======================================================================== */
u32 db_v50_selftest(void)
{
    u32 bad = 0;
    u32 hdr = (u32)sizeof(DB_ResultHeader);
    u32 desc = (u32)sizeof(DB_ColumnInfo);

    /* (0) slot 配置。判定は db_slot_layout_ok() (下) が持つ。 */
    if (!db_slot_layout_ok(KAPI_SLOT_COUNT)) bad |= 1u << 0;

    /* (1) SHM の境界: ちょうど収まる / 1 バイト超過 / descriptor だけで溢れる。
     * 上限は**結果側の上限** (診断領域を除いた分)。 */
    if (!shm_row_fits_n(1, (u32)DB_SHM_RESULT_LIMIT - hdr - desc)) bad |= 1u << 1;
    if (shm_row_fits_n(1, (u32)DB_SHM_RESULT_LIMIT - hdr - desc + 1u)) bad |= 1u << 1;
    if (shm_row_fits_n((int)(((u32)DB_SHM_RESULT_LIMIT - hdr) / desc) + 1, 0))
        bad |= 1u << 1;
    if (shm_row_fits_n(-1, 0)) bad |= 1u << 1;

    /* (2) ポインタ検証の NULL / overflow (帯と PTE は CPL=3 側でしか踏めない) */
    if (db_user_range_ok((const void *)0, 1u)) bad |= 1u << 2;
    if (db_user_range_ok((const void *)0xFFFFFF00UL, 0x200u)) bad |= 1u << 2;

    /* (3) journal 名の容量規則 (票 §1a / Codex 往復 2 の B4)。
     * 末尾判定は SQLite 自身にさせるので接続が要る = ブート時には踏めない。
     * ここで踏むのは接続の要らない「下位層の path 容量」の方。
     * `<絶対名>-journal` が NUL 込み VFS_MAX_PATH に収まる長さが境界。 */
    {
        static char probe[VFS_MAX_PATH + 8];
        u32 fit = (u32)VFS_MAX_PATH - 1u - ((u32)sizeof(DB_JOURNAL_SUFFIX) - 1u);
        u32 k;
        for (k = 0; k < (u32)sizeof(probe); k++) probe[k] = 'a';
        probe[0] = '/';
        probe[fit] = '\0';
        if (!db_journal_name(probe)) bad |= 1u << 3;
        probe[fit] = 'a';
        probe[fit + 1u] = '\0';
        if (db_journal_name(probe)) bad |= 1u << 3;
    }

    /* (4) owner 別の欄が ID の池 (1 = シェル帯 .. 5) を覆っている */
    if (DB_OWNER_SLOTS < 6) bad |= 1u << 4;

    /* (5) 診断領域と結果領域がブロック 0 を過不足なく分け合っている
     * (票 TASK_DB_ERRSTR §4)。ここがずれると、結果データが
     * db_last_error() の返り先を踏み潰す。 */
    /* 結果の上限がそのまま診断領域の入口 = 隙間も重なりも無い */
    if ((u32)DB_SHM_DIAG_OFFSET != (u32)DB_SHM_RESULT_LIMIT) bad |= 1u << 5;
    /* 診断文のバッファと「常に NUL の 1 バイト」が重ならない */
    if ((u32)DB_SHM_DIAG_OFFSET + (u32)DB_SHM_ERRSTR_MAX
        > (u32)DB_SHM_EMPTY_OFFSET) bad |= 1u << 5;
    /* その 1 バイトは診断領域の中 (= ブロック 0 の中) にある */
    if ((u32)DB_SHM_EMPTY_OFFSET - (u32)DB_SHM_DIAG_OFFSET
        >= (u32)DB_SHM_DIAG_SIZE) bad |= 1u << 5;
    /* 空文字列は必ず NUL で読める (返す側が毎回置く) */
    if (db_shm_empty()[0] != '\0') bad |= 1u << 5;
    /* 上限を超える診断文は切られ、NUL で終わり、診断領域を出ない */
    {
        static char probe[DB_SHM_ERRSTR_MAX + 16];
        const char *cut;
        u32 k;
        for (k = 0; k < (u32)sizeof(probe) - 1u; k++) probe[k] = 'x';
        probe[sizeof(probe) - 1] = '\0';
        cut = db_shm_diag(probe);
        if (cut != (const char *)(DB_SHM_PTR + DB_SHM_DIAG_OFFSET))
            bad |= 1u << 5;
        if (kstrlen(cut) != (u32)DB_SHM_ERRSTR_MAX - 1u) bad |= 1u << 5;
    }

    return bad;
}

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
