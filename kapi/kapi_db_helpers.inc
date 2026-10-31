/* Shared-memory diagnostics, slot validation, caller input and row encoding. */
/* ======================================================================== */
/*  ヘルパー: CPL=3 から読める文字列 (票 TASK_DB_ERRSTR §4)                  */
/*                                                                          */
/*  共有メモリはアプリの PD に RW+USER で見えているが、カーネルの            */
/*  .rodata / .data と SQLite module は見えない。              */
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
    while (len > 0u && UTF8_IS_CONT((u8)msg[len])) len--;
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
    /* USER handles belong to the opening app; kernel/WM remain trusted. */
    if (ring3_call_from_user() && db_slots[handle].owner != res_owner_get())
        return (DbSlot *)0;
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
    /* USER handles belong to the opening app; kernel/WM remain trusted. */
    if (ring3_call_from_user() && db_slots[handle].owner != res_owner_get())
        return (DbSlot *)0;
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

/* Direct CPL0/WM calls use the existing explicit trusted convention; USER
 * calls consume the fixed dispatcher identity. Staging is kernel BSS, not SHM.
 * Caps: paths 256 incl. NUL, SQL 1024 incl. NUL; binds retain DB_BIND_*_MAX. */
static int db_copy_input(const void *src, void *dst, u32 size, int cstr)
{
    struct caller_access c;
    if (!src || !redir_access_capture(&c)) return 0;
    return cstr ? copy_caller_cstr(&c, src, dst, size) :
                  copy_caller_bytes(&c, src, dst, size);
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
 *   - 実体化そのものが失敗していないか (MEMSYS5 の空きが足りないと、
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
            add = sizeof(i32);
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
            cols[i].length = sizeof(i32);
            if (remaining >= (i32)sizeof(i32)) {
                *(i32 *)(DB_SHM_PTR + data_offset) = val;
                data_offset += (i32)sizeof(i32);
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
            cols[i].length = sizeof(i32);
            if (remaining >= (i32)sizeof(i32)) {
                *(i32 *)(DB_SHM_PTR + data_offset) = ival;
                data_offset += (i32)sizeof(i32);
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

