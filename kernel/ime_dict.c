/* ======================================================================== */
/*  IME_DICT.C - FEP辞書検索エンジン (SQLite ベース)                         */
/*                                                                          */
/*  カーネルVFS上のSQLite DBファイルを辞書バックエンドとして使用する。        */
/*  sqlite3_*関数をカーネル空間から直接呼び出す (KAPI不使用)。               */
/*                                                                          */
/*  検索モード:                                                              */
/*    - 2文字以下: 完全一致 (変換キー押下時のみ呼ばれる前提)                 */
/*    - 3文字以上: 前方一致 (範囲検索でインデックス活用)                     */
/* ======================================================================== */

#include "ime.h"
#include "sqlite3.h"
#include "vfs.h"
#include "utf8.h"
#include "kprintf.h"
#include "serial.h"
#include "kstring.h"
#include "os32_kapi_shared.h"
#include "os32_sqlite_vfs.h"

/* FEP辞書はカーネル常駐の接続なので、基底の VFS fd を exec_exit の
 * FD一括クローズから保護する。これを欠くと外部プログラム (ime.bin 等)
 * の終了時に fd が回収され、以後の検索が全て SQLITE_IOERR になる。 */
static void dict_fd_protect(IME_Dict *dict, int on)
{
    int fd;
    if (!dict || !dict->db) return;
    fd = os32_sqlite_db_fd(dict->db);
    if (fd >= 0) vfs_fd_set_protect(fd, on);
}

/* プリペアドステートメントのSQL */
static const char SQL_EXACT[] =
    "SELECT kanji, pos_id, cost FROM ("
    "  SELECT kanji, 0 as pos_id, (-10000 - freq) as cost FROM dict_user WHERE yomi = ?1"
    "  UNION ALL "
    "  SELECT kanji, pos_id, cost FROM dict WHERE yomi = ?1"
    ") ORDER BY cost LIMIT 32";

static const char SQL_PREFIX[] =
    "SELECT kanji, pos_id, cost FROM ("
    "  SELECT kanji, 0 as pos_id, (-10000 - freq) as cost"
    "    FROM dict_user WHERE yomi >= ?1 AND yomi < ?1 || X'EFBFBF'"
    "  UNION ALL "
    "  SELECT kanji, pos_id,"
    "    CASE WHEN yomi = ?1 THEN cost - 500 ELSE cost END as cost"
    "    FROM dict WHERE yomi >= ?1 AND yomi < ?1 || X'EFBFBF'"
    ") ORDER BY cost LIMIT 32";

static const char SQL_LEARN[] =
    "INSERT INTO dict_user (yomi, kanji, freq, last_ts) "
    "VALUES (?1, ?2, 1, ?3) "
    "ON CONFLICT(yomi, kanji) DO UPDATE SET "
    "freq = freq + 1, last_ts = ?3";

/* ======================================================================== */
/*  公開API                                                                  */
/* ======================================================================== */

static void ime_dict_close(IME_Dict *dict);

int ime_dict_open(IME_Dict *dict, const char *path)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    kmemset(dict, 0, sizeof(IME_Dict));
    /* 開き直しに使う名前。収まらなければ覚えない (= 開き直さない)。
     * 切り詰めた名前で別の DB を開き直さない */
    if (path && kstrlen(path) < sizeof(dict->path))
        kstrncpy(dict->path, path, sizeof(dict->path));

    /* DBファイルを読み書き可能で開く (ユーザー辞書のため) */
    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, (const char *)0);
    if (rc != SQLITE_OK) {
        kprintf(ATTR_RED, "IME: sqlite3_open failed: %s (rc=%d)\r\n",
                path, rc);
        if (db) sqlite3_close(db);
        return -1;
    }
    dict->db = (void *)db;

    /* 学習用テーブルの作成 (存在しない場合) */
    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS dict_user ("
        "  yomi TEXT NOT NULL,"
        "  kanji TEXT NOT NULL,"
        "  freq INTEGER DEFAULT 1,"
        "  last_ts INTEGER DEFAULT 0,"
        "  PRIMARY KEY (yomi, kanji)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_user_yomi ON dict_user(yomi);",
        0, 0, 0);
    if (rc != SQLITE_OK) {
        kprintf(ATTR_RED, "IME: create user dict failed (rc=%d)\r\n", rc);
        /* 学習はできないが検索は続行するためエラーにはしない */
    }

    /* 完全一致検索用プリペアドステートメント */
    rc = sqlite3_prepare_v2(db, SQL_EXACT, -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK) {
        kprintf(ATTR_RED, "IME: prepare exact failed (rc=%d)\r\n", rc);
        sqlite3_close(db);
        dict->db = (void *)0;
        return -2;
    }
    dict->exact_stmt = (void *)stmt;

    /* 前方一致検索用プリペアドステートメント */
    rc = sqlite3_prepare_v2(db, SQL_PREFIX, -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK) {
        kprintf(ATTR_RED, "IME: prepare prefix failed (rc=%d)\r\n", rc);
        sqlite3_finalize((sqlite3_stmt *)dict->exact_stmt);
        sqlite3_close(db);
        dict->db = (void *)0;
        dict->exact_stmt = (void *)0;
        return -3;
    }
    dict->prefix_stmt = (void *)stmt;

    /* 学習(UPSERT)用プリペアドステートメント */
    rc = sqlite3_prepare_v2(db, SQL_LEARN, -1, &stmt, (const char **)0);
    if (rc == SQLITE_OK) {
        dict->learn_stmt = (void *)stmt;
    } else {
        kprintf(ATTR_RED, "IME: prepare learn failed (rc=%d)\r\n", rc);
        dict->learn_stmt = (void *)0;
    }

    /* 常駐接続の fd を exec クリーンアップから保護 */
    dict_fd_protect(dict, 1);

    /* 成功の知らせはシリアルだけに出す。GUI (gshell) 中に SHIFT+SPACE で
     * 初めて辞書を開くと、この行が TVRAM に残って GFX 画面に透けるため
     * (2026-09-06 G3 で実測)。失敗は今までどおり画面にも出す。 */
    serial_puts("IME: Dict loaded (SQLite): ");
    serial_puts(path);
    serial_puts("\r\n");
    return 0;
}

/* ======================================================================== */
/*  常駐接続の開き直し (票 TASK_VFS_FD_PATH 方針 v3 の 6 / ラリー 3)          */
/*                                                                          */
/*  辞書の実体が消える・作り直される (`rm` → 作り直し、別の実体への置き換え   */
/*  rename、umount) と、VFS は開いていた FD に失効の印を付け、以後の読みは    */
/*  OS32_ERR_STALE になる (SQLite には SQLITE_IOERR として届く)。そのときは   */
/*  **SQLite 接続ごと**開き直す — ステートメント 3 本の finalize →          */
/*  sqlite3_close → open → prepare → dict_fd_protect                       */
/*  (ime_dict_close + ime_dict_open)。                                      */
/*                                                                          */
/*  hsync の差し替え (置き換え rename) は**ここへ来ない**: 辞書を開いている   */
/*  間は /db/fep.db の置き換えを VFS が BUSY で断る (開いている SQLite DB の  */
/*  rename は BUSY、票のユーザー決裁 ①)。hsync は「使用中で置き換えられ      */
/*  なかった」と表示する。失効して開き直すのは rm と作り直しの経路だけ。      */
/*                                                                          */
/*  - **旧接続に SQL を流す前に**失効を確かめる (dict_ready、Codex 実装      */
/*    レビュー ラリー 1 の B1)。失効した接続で sqlite3_step をすると、SQLite  */
/*    は本体を読む前に hot journal を判定し、ジャーナルを再生・削除しうる。   */
/*    失効していたら step をせずに閉じ、ジャーナルを stat してから開き直す。  */
/*  - 失効 1 回につき 1 回。新しい FD はまた失効しうる (次の作り直し) ので、  */
/*    印の付いた FD を見たら開き直す。失効を伴わない I/O エラーは 1 回だけ   */
/*    (io_retried、成功した操作で戻す)。                                     */
/*  - `<名前>-journal` が中身を持って残っていたら (hot journal) **開かない**。 */
/*    辞書無しで動き、画面に出す。前の接続の途中の書き込みを、新しい実体に    */
/*    巻き戻させない。                                                       */
/*  - 検索・学習だけでなく、ユーザー辞書の一覧・削除・書き出し・全消去も同じ  */
/*    経路を通る (Codex 実装レビュー ラリー 1 の B5)。                        */
/*  戻り値: 1 = 開き直した (呼び手は 1 回だけやり直してよい) / 0 = しない     */
/* ======================================================================== */
static int dict_recover(IME_Dict *dict)
{
    /* `<名前>-journal` を組み、stat の後で末尾を落として名前に戻す
     * (ime_dict_open は dict を消してから名前を写すので、別に持つ) */
    static char name[OS32_MAX_PATH];
    OS32_Stat st;
    int fd, stale, rc;
    u32 n;

    if (!dict || !dict->db || !dict->path[0]) return 0;
    fd = os32_sqlite_db_fd(dict->db);
    stale = (fd >= 0) ? vfs_fd_is_stale(fd) : 0;
    if (!stale && dict->io_retried) return 0;

    n = kstrlen(dict->path);
    if (n + 9u > sizeof(name)) return 0;   /* "-journal" + NUL */
    kstrncpy(name, dict->path, sizeof(name));
    kstrncat(name, "-journal", sizeof(name));

    ime_dict_close(dict);
    rc = vfs_stat(name, &st);
    if (rc != OS32_ERR_NOTFOUND && !(rc == 0 && st.st_size == 0)) {
        /* hot journal (か、有無を判定できない)。開かない */
        kprintf(ATTR_RED, "IME: %s remains (rc=%d), dictionary disabled\r\n",
                name, rc);
        return 0;
    }
    name[n] = '\0';
    if (ime_dict_open(dict, name) != 0) {
        kprintf(ATTR_RED, "IME: dictionary reopen failed, running without it\r\n");
        return 0;
    }
    /* 失効による開き直しは印を持ち越さない。I/O エラーによる開き直しは 1 回 */
    dict->io_retried = stale ? 0 : 1;
    serial_puts("IME: dictionary reopened\r\n");
    return 1;
}

/* 旧接続に SQL を流す**前**に呼ぶ (B1)。FD が失効していたら、その接続では
 * 何も実行せずに閉じて開き直す (hot journal なら辞書無し)。
 * 戻り値: 1 = 使える接続がある / 0 = 辞書無し */
static int dict_ready(IME_Dict *dict)
{
    int fd;
    if (!dict || !dict->db) return 0;
    fd = os32_sqlite_db_fd(dict->db);
    if (fd >= 0 && vfs_fd_is_stale(fd)) (void)dict_recover(dict);
    return dict->db != (void *)0;
}

static int is_ioerr(int rc) { return (rc & 0xFF) == SQLITE_IOERR; }

static int ime_dict_search_once(IME_Dict *dict, const char *yomi,
                                IME_Result *results, int max_results,
                                int *io_error);

int ime_dict_search(IME_Dict *dict, const char *yomi,
                    IME_Result *results, int max_results)
{
    int io_error = 0;
    int n;
    if (!dict_ready(dict)) return 0;
    n = ime_dict_search_once(dict, yomi, results, max_results, &io_error);
    if (io_error && dict_recover(dict)) {
        n = ime_dict_search_once(dict, yomi, results, max_results, &io_error);
    }
    if (n > 0 && dict) dict->io_retried = 0;
    return n;
}

static int ime_dict_search_once(IME_Dict *dict, const char *yomi,
                                IME_Result *results, int max_results,
                                int *io_error)
{
    sqlite3_stmt *stmt;
    int yomi_chars;
    int count;
    int yomi_len;
    const char *kanji_text;
    int copy_len;

    *io_error = 0;
    if (!dict || !dict->db || !yomi || !results || max_results <= 0)
        return 0;
    if (yomi[0] == '\0') return 0;

    /* 最大取得数を制限 */
    if (max_results > IME_MAX_RESULTS)
        max_results = IME_MAX_RESULTS;

    /* UTF-8 文字数に応じてクエリを切り替え */
    yomi_chars = utf8_strlen((const u8 *)yomi);
    yomi_len = (int)kstrlen(yomi);

    if (yomi_chars <= 2) {
        /* 2文字以下: 完全一致のみ */
        stmt = (sqlite3_stmt *)dict->exact_stmt;
    } else {
        /* 3文字以上: 前方一致 */
        stmt = (sqlite3_stmt *)dict->prefix_stmt;
    }

    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, yomi, yomi_len, SQLITE_STATIC);

    /* 結果取得ループ */
    count = 0;
    {
        int step_rc;
        while (count < max_results &&
               (step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        IME_Result *r = &results[count];

        /* 漢字 (カラム0) */
        kanji_text = (const char *)sqlite3_column_text(stmt, 0);
        if (!kanji_text) continue;
        copy_len = (int)kstrlen(kanji_text);
        if (copy_len > 31) copy_len = 31;
        kmemcpy(r->kanji, kanji_text, copy_len);
        r->kanji[copy_len] = '\0';

        /* 読みをコピー */
        copy_len = yomi_len < 31 ? yomi_len : 31;
        kmemcpy(r->yomi, yomi, copy_len);
        r->yomi[copy_len] = '\0';

        /* 品詞ID (カラム1) */
        r->pos_id = (u16)sqlite3_column_int(stmt, 1);

        /* コスト (カラム2) */
        r->cost = sqlite3_column_int(stmt, 2);

        /* ひらがな/カタカナそのまま表記に対する動的ペナルティ */
        if (r->cost >= 0 && kstrlen(r->kanji) >= (u32)yomi_len) {
            r->cost += 300;
        }

        count++;
        }

        /* 診断: step がエラーで終わった場合のみ表示 (SQLITE_DONE は正常) */
        if (count == 0 && step_rc != SQLITE_DONE) {
            /* 読めなかった (FD の失効を含む) — 呼び手が開き直しを判断する */
            if ((step_rc & 0xFF) == SQLITE_IOERR) *io_error = 1;
            kprintf(ATTR_RED,
                    "IME: search 0hit len=%d chars=%d step=%d err=%d ext=%d %s\r\n",
                    yomi_len, yomi_chars, step_rc,
                    sqlite3_errcode((sqlite3 *)dict->db),
                    sqlite3_extended_errcode((sqlite3 *)dict->db),
                    sqlite3_errmsg((sqlite3 *)dict->db));
        }
    }

    return count;
}

void ime_dict_learn(IME_Dict *dict, const char *yomi, const char *kanji)
{
    sqlite3_stmt *stmt;
    int rc;

    if (!dict || !yomi || !kanji) return;
    if (!dict_ready(dict) || !dict->learn_stmt) return;

    stmt = (sqlite3_stmt *)dict->learn_stmt;
    sqlite3_reset(stmt);

    sqlite3_bind_text(stmt, 1, yomi, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, kanji, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, 0 /* sys_time() is not easily available here, use 0 for now */);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && is_ioerr(rc) && dict_recover(dict) &&
        dict->learn_stmt) {
        /* 開き直した接続で 1 回だけやり直す */
        stmt = (sqlite3_stmt *)dict->learn_stmt;
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, yomi, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, kanji, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, 0);
        rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_DONE) {
        kprintf(ATTR_RED, "IME: Learn failed (rc=%d)\r\n", rc);
    } else {
        dict->io_retried = 0;
    }
}

/* 辞書クローズ内部関数 */
static void ime_dict_close(IME_Dict *dict)
{
    if (!dict || !dict->db) return;
    /* 保護を解除してから閉じる (fd スロット再利用に備える) */
    dict_fd_protect(dict, 0);
    if (dict->exact_stmt) {
        sqlite3_finalize((sqlite3_stmt *)dict->exact_stmt);
        dict->exact_stmt = NULL;
    }
    if (dict->prefix_stmt) {
        sqlite3_finalize((sqlite3_stmt *)dict->prefix_stmt);
        dict->prefix_stmt = NULL;
    }
    if (dict->learn_stmt) {
        sqlite3_finalize((sqlite3_stmt *)dict->learn_stmt);
        dict->learn_stmt = NULL;
    }
    sqlite3_close((sqlite3 *)dict->db);
    dict->db = NULL;
}

/* 辞書再オープン */
int ime_dict_reopen(IME_Dict *dict, const char *path)
{
    ime_dict_close(dict);
    return ime_dict_open(dict, path);
}

/* ======================================================================== */
/*  ユーザー学習辞書の管理 (ime コマンドの user list / delete / export /      */
/*  clear)。どれも検索・学習と同じ回復経路を通る (Codex 実装レビュー         */
/*  ラリー 1 の B5): 旧接続に SQL を流す前に失効を確かめ (dict_ready)、       */
/*  I/O エラーなら 1 回だけ開き直してやり直す (dict_recover)。                */
/*  *_once の戻り値は各 API の戻り値そのもの、*io_error = 1 は「I/O エラーで  */
/*  失敗した」(開き直して 1 回やり直す)。                                   */
/* ======================================================================== */

/* 一覧: 件数 / -4 = 読めなかった (部分的な一覧を成功にしない)。
 * prepare の失敗も -4 — 以前は 0 を返したので、ime コマンドが「学習なし」と
 * 区別できなかった (実装レビュー ラリー 2 の非 blocker) */
static int user_list_once(IME_Dict *dict, const char *yomi_prefix,
                          IME_UserEntry *out, int max, int *io_error)
{
    sqlite3 *db = (sqlite3 *)dict->db;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int count = 0;
    const char *sql;

    *io_error = 0;
    if (yomi_prefix && yomi_prefix[0] != '\0') {
        sql = "SELECT yomi, kanji, freq FROM dict_user WHERE yomi >= ?1 AND yomi < ?1 || X'EFBFBF' ORDER BY freq DESC, yomi ASC LIMIT ?2";
    } else {
        sql = "SELECT yomi, kanji, freq FROM dict_user ORDER BY freq DESC, yomi ASC LIMIT ?2";
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        *io_error = is_ioerr(sqlite3_extended_errcode(db));
        kprintf(ATTR_RED, "IME: user_list prepare failed (rc=%d)\r\n", rc);
        return -4;
    }

    if (yomi_prefix && yomi_prefix[0] != '\0') {
        sqlite3_bind_text(stmt, 1, yomi_prefix, -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, 2, max);

    while (count < max && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        IME_UserEntry *e = &out[count];
        const char *y_text = (const char *)sqlite3_column_text(stmt, 0);
        const char *k_text = (const char *)sqlite3_column_text(stmt, 1);
        int freq = sqlite3_column_int(stmt, 2);

        if (y_text) {
            kstrncpy(e->yomi, y_text, sizeof(e->yomi));
        } else {
            e->yomi[0] = '\0';
        }

        if (k_text) {
            kstrncpy(e->kanji, k_text, sizeof(e->kanji));
        } else {
            e->kanji[0] = '\0';
        }

        e->freq = freq;
        count++;
    }

    sqlite3_finalize(stmt);
    if (count < max && rc != SQLITE_DONE) {
        *io_error = is_ioerr(rc);
        kprintf(ATTR_RED, "IME: user_list step failed (rc=%d)\r\n", rc);
        return -4;
    }
    return count;
}

/* ユーザー学習辞書列挙 */
int ime_user_list(IME_Dict *dict, const char *yomi_prefix,
                  IME_UserEntry *out, int max)
{
    int io_error = 0, n;

    if (!dict || !out || max <= 0) return 0;
    if (!dict_ready(dict)) return 0;
    n = user_list_once(dict, yomi_prefix, out, max, &io_error);
    if (io_error && dict_recover(dict))
        n = user_list_once(dict, yomi_prefix, out, max, &io_error);
    if (n >= 0 && !io_error) dict->io_retried = 0;
    return n;
}

static int user_delete_once(IME_Dict *dict, const char *yomi,
                            const char *kanji, int *io_error)
{
    sqlite3 *db = (sqlite3 *)dict->db;
    sqlite3_stmt *stmt = NULL;
    int rc;
    const char *sql;

    *io_error = 0;
    if (kanji && kanji[0] != '\0') {
        sql = "DELETE FROM dict_user WHERE yomi = ?1 AND kanji = ?2";
    } else {
        sql = "DELETE FROM dict_user WHERE yomi = ?1";
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        *io_error = is_ioerr(sqlite3_extended_errcode(db));
        kprintf(ATTR_RED, "IME: user_delete prepare failed (rc=%d)\r\n", rc);
        return -2;
    }

    sqlite3_bind_text(stmt, 1, yomi, -1, SQLITE_STATIC);
    if (kanji && kanji[0] != '\0') {
        sqlite3_bind_text(stmt, 2, kanji, -1, SQLITE_STATIC);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        *io_error = is_ioerr(rc);
        kprintf(ATTR_RED, "IME: user_delete execute failed (rc=%d)\r\n", rc);
        return -3;
    }

    return 0;
}

/* ユーザー学習辞書エントリ削除 */
int ime_user_delete(IME_Dict *dict, const char *yomi, const char *kanji)
{
    int io_error = 0, rc;

    if (!dict || !yomi || yomi[0] == '\0') return -1;
    if (!dict_ready(dict)) return -1;
    rc = user_delete_once(dict, yomi, kanji, &io_error);
    if (io_error && dict_recover(dict))
        rc = user_delete_once(dict, yomi, kanji, &io_error);
    if (rc == 0) dict->io_retried = 0;
    return rc;
}

/* 書き出し: 0 = 成功 / -2 = 書き出し先を開けない / -3 = prepare 失敗 /
 * -4 = 途中で読めなかった (sqlite3_step の異常終了を EOF と区別する) /
 * -5 = 書き出し先へ書けなかった */
static int user_export_once(IME_Dict *dict, const char *path, int *io_error)
{
    sqlite3 *db = (sqlite3 *)dict->db;
    sqlite3_stmt *stmt = NULL;
    int rc, step_rc, fd, result = 0;
    u32 len;
    const char *sql;
    char line[128];

    *io_error = 0;
    fd = vfs_open(path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) {
        kprintf(ATTR_RED, "IME: export open failed: %s (fd=%d)\r\n", path, fd);
        return -2;
    }

    sql = "SELECT yomi, kanji, freq FROM dict_user ORDER BY freq DESC, yomi ASC";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        *io_error = is_ioerr(sqlite3_extended_errcode(db));
        kprintf(ATTR_RED, "IME: export prepare failed (rc=%d)\r\n", rc);
        vfs_close(fd);
        return -3;
    }

    while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *y_text = (const char *)sqlite3_column_text(stmt, 0);
        const char *k_text = (const char *)sqlite3_column_text(stmt, 1);
        int freq = sqlite3_column_int(stmt, 2);

        if (!y_text || !k_text) continue;

        sqlite3_snprintf(sizeof(line), line, "%s,%s,%d\n", y_text, k_text, freq);
        len = (u32)kstrlen(line);
        if (vfs_write_fd(fd, line, len) != (int)len) {
            kprintf(ATTR_RED, "IME: export write failed: %s\r\n", path);
            result = -5;
            break;
        }
    }
    if (result == 0 && step_rc != SQLITE_DONE) {
        *io_error = is_ioerr(step_rc);
        kprintf(ATTR_RED, "IME: export step failed (rc=%d)\r\n", step_rc);
        result = -4;
    }

    sqlite3_finalize(stmt);
    vfs_close(fd);
    return result;
}

/* ユーザー学習辞書CSVエクスポート */
int ime_user_export(IME_Dict *dict, const char *path)
{
    int io_error = 0, rc;

    if (!dict || !path || path[0] == '\0') return -1;
    if (!dict_ready(dict)) return -1;
    rc = user_export_once(dict, path, &io_error);
    if (io_error && dict_recover(dict))
        rc = user_export_once(dict, path, &io_error);   /* 書き出し先は O_TRUNC で作り直す */
    if (rc == 0) dict->io_retried = 0;
    return rc;
}

/* ユーザー学習辞書全消去 */
int ime_user_clear(IME_Dict *dict)
{
    int rc;

    if (!dict) return -1;
    if (!dict_ready(dict)) return -1;
    rc = sqlite3_exec((sqlite3 *)dict->db, "DELETE FROM dict_user;", NULL, NULL, NULL);
    if (rc != SQLITE_OK &&
        is_ioerr(sqlite3_extended_errcode((sqlite3 *)dict->db)) &&
        dict_recover(dict)) {
        rc = sqlite3_exec((sqlite3 *)dict->db, "DELETE FROM dict_user;",
                          NULL, NULL, NULL);
    }
    if (rc != SQLITE_OK) {
        kprintf(ATTR_RED, "IME: user_clear execute failed (rc=%d)\r\n", rc);
        return -2;
    }
    dict->io_retried = 0;
    return 0;
}
