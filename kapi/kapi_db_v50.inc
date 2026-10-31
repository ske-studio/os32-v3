/* v50 existing-file open, prepared statements, parameter binding and error codes. */
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
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(1)) return -1;
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
    if (!db_copy_input(path, path_copy_buf, PATH_COPY_BUF_SIZE, 1)) {
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

    if (os32_sqlite_group_acquire(OS32_SQLITE_EXEC, res_owner_get(),
                                  &db_slots[i].group) != SQLITE_OK) {
        open_fail_set(SQLITE_FULL);
        return -1;
    }
    db_slots[i].db = (sqlite3 *)0;
    db_slots[i].cleanup_error = SQLITE_OK;
    db_slots[i].cleanup_message[0] = '\0';
    db_slots[i].last_error = SQLITE_OK;
    db_slots[i].used_once = 1;
    db_slots[i].bindable = 0;
    db_slots[i].active_stmt = (sqlite3_stmt *)0;
    db_slots[i].owner = res_owner_get();

    /* CREATE/URI は付けず、この接続専用 group の VFS を使う。 */
    flags = writable ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY;
    rc = os32_sqlite_group_open(&db_slots[i].group, abs_path_buf, flags, &db);
    db_slots[i].db = db;
    if (rc != SQLITE_OK) {
        slot_save_error(&db_slots[i], rc);
        slot_note(&db_slots[i], slot_ext(&db_slots[i], rc));
        open_fail_set(db_slots[i].last_error);
        shm_write_error(&db_slots[i]);
        if (db) {
            db_slots[i].in_use = 1;
            kapi_db_close(i);
        } else {
            os32_sqlite_group_begin_close(&db_slots[i].group);
            if (os32_sqlite_group_finish(&db_slots[i].group, SQLITE_OK) != SQLITE_OK)
                db_slots[i].in_use = db_slots[i].isolated = 1;
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
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(1)) return -1;
    DbSlot *slot = slot_get(handle);
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    const char *tail = (const char *)0;
    int rc, tail_rc;

    if (!slot) {
        shm_write_error((DbSlot *)0);
        return -1;
    }

    /* B3: rejected input leaves the old statement and result untouched. */
    if (!db_copy_input(sql, sql_copy_buf, SQL_COPY_BUF_SIZE, 1) ||
        !sql_copy_buf[0]) return -1;
    if (kapi_db_finalize(handle) != 0) {
        shm_write_error(slot);
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
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(0)) return -1;
    DbSlot *slot = bind_slot(handle, index);
    if (!slot) return -1;
    return bind_done(slot, sqlite3_bind_int(slot->active_stmt, index, value));
}

int __cdecl kapi_db_bind_text(int handle, int index, const char *text, int length)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(0)) return -1;
    DbSlot *slot = bind_slot(handle, index);
    if (!slot) return -1;
    if (length < 0 || length > DB_BIND_TEXT_MAX ||
        !db_copy_input(text, text_copy_buf, (u32)length, 0)) {
        slot_note(slot, SQLITE_MISUSE);
        return -1;
    }
    text_copy_buf[length] = '\0';
    /* 0B でも非 NULL の空値。SQLITE_TRANSIENT なので SQLite が写す。 */
    return bind_done(slot, sqlite3_bind_text(slot->active_stmt, index,
                                             text_copy_buf, length,
                                             SQLITE_TRANSIENT));
}

int __cdecl kapi_db_bind_blob(int handle, int index, const void *data, int length)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(0)) return -1;
    DbSlot *slot = bind_slot(handle, index);
    if (!slot) return -1;
    if (length < 0 || length > DB_BIND_BLOB_MAX ||
        !db_copy_input(data, blob_copy_buf, (u32)length, 0)) {
        slot_note(slot, SQLITE_MISUSE);
        return -1;
    }
    return bind_done(slot, sqlite3_bind_blob(slot->active_stmt, index,
                                             blob_copy_buf, length,
                                             SQLITE_TRANSIENT));
}

int __cdecl kapi_db_bind_null(int handle, int index)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(0)) return -1;
    DbSlot *slot = bind_slot(handle, index);
    if (!slot) return -1;
    return bind_done(slot, sqlite3_bind_null(slot->active_stmt, index));
}

int __cdecl kapi_db_error_code(int handle)
{
    if (!DB_MODULE_READY()) return OS32_ERR_NOSYS;
    if (!SQLITE_OUTER_READY(0)) return -1;
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

