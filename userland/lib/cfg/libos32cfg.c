/* ======================================================================== */
/*  LIBOS32CFG.C — 設定レジストリの中核 (open / close / get / txn / set)     */
/*                                                                          */
/*  票 docs/archive/settings/TASK_S2.md §1。C89 [C1]。malloc しない。        */
/*  列挙は cfg_enum.c、生成は cfg_init.c、tsv reader は cfg_tsv.c に分けて    */
/*  ある — 読むだけのアプリが列挙 / 生成の作業領域を背負わないため。          */
/* ======================================================================== */

#include "cfg_internal.h"

/* ======================================================================== */
/*  KAPI 境界                                                                */
/* ======================================================================== */

static const CfgBackend *g_backend;

void cfg_set_backend(const CfgBackend *b)
{
    g_backend = b;
}

const CfgBackend *cfg_backend(void)
{
    if (!g_backend) g_backend = cfg_backend_platform();
    return g_backend;
}

/* ======================================================================== */
/*  状態 (1 プロセス 1 接続、静的 1 本)                                      */
/* ======================================================================== */

static CfgDb g_db;
static int g_close_error;

void cfg_i_set_close_error(int code)
{
    g_close_error = code;
}

int cfg_last_close_error(void)
{
    return g_close_error;
}

int cfg_status(const CfgDb *db)
{
    if (!db) return CFG_ERROR;
    return db->status;
}

int cfg_schema_version(const CfgDb *db)
{
    if (!db) return 0;
    return db->schema_version;
}

int cfg_last_sqlite(const CfgDb *db)
{
    if (!db) return 0;
    return db->last_sqlite;
}

/* ======================================================================== */
/*  文字列と UTF-8                                                           */
/* ======================================================================== */

static int cfg_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

int cfg_i_len_ok(const char *s, int max)
{
    int n;
    if (!s) return 0;
    for (n = 0; s[n]; n++) {
        if (n >= max) return 0;
    }
    return n > 0;
}

static int name_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

int cfg_i_valid_scope(const char *s)
{
    int i;
    if (!cfg_i_len_ok(s, CFG_SCOPE_MAX)) return 0;
    if (s[0] == 's' && s[1] == 'y' && s[2] == 's' && s[3] == 't' &&
        s[4] == 'e' && s[5] == 'm' && s[6] == '\0') return 1;
    if (s[0] == 'g' && s[1] == 's' && s[2] == 'h' && s[3] == 'e' &&
        s[4] == 'l' && s[5] == 'l' && s[6] == '\0') return 1;
    if (s[0] == 'u' && s[1] == 's' && s[2] == 'e' && s[3] == 'r' &&
        s[4] == '\0') return 1;
    if (!(s[0] == 'a' && s[1] == 'p' && s[2] == 'p' && s[3] == ':')) return 0;
    if (!s[4]) return 0;
    for (i = 4; s[i]; i++) {
        if (!name_char((unsigned char)s[i])) return 0;
    }
    return 1;
}

int cfg_i_valid_key(const char *s)
{
    int i, seg = 0;
    if (!cfg_i_len_ok(s, CFG_KEY_MAX)) return 0;
    for (i = 0; s[i]; i++) {
        if (s[i] == '/') {
            if (seg == 0) return 0;
            seg = 0;
            continue;
        }
        if (!name_char((unsigned char)s[i])) return 0;
        seg++;
    }
    return seg > 0;
}

void cfg_i_u8_reset(CfgU8 *s)
{
    s->need = 0;
    s->cp = 0;
    s->lo = 0;
}

int cfg_i_u8_byte(CfgU8 *s, int b)
{
    b &= 0xFF;
    if (s->need > 0) {
        if (b < 0x80 || b > 0xBF) return -1;
        s->cp = (s->cp << 6) | (u32)(b & 0x3F);
        s->need--;
        if (s->need == 0) {
            if (s->cp < s->lo) return -1;                    /* 冗長符号 */
            if (s->cp >= 0xD800UL && s->cp <= 0xDFFFUL) return -1;
            if (s->cp > 0x10FFFFUL) return -1;
        }
        return 0;
    }
    if (b < 0x80) return 0;
    if (b < 0xC2) return -1;            /* 継続バイト単独 / C0 C1 */
    if (b < 0xE0) { s->need = 1; s->cp = (u32)(b & 0x1F); s->lo = 0x80UL; return 0; }
    if (b < 0xF0) { s->need = 2; s->cp = (u32)(b & 0x0F); s->lo = 0x800UL; return 0; }
    if (b < 0xF5) { s->need = 3; s->cp = (u32)(b & 0x07); s->lo = 0x10000UL; return 0; }
    return -1;
}

int cfg_i_u8_done(const CfgU8 *s)
{
    return s->need == 0;
}

int cfg_i_utf8_check(const void *p, int n)
{
    const unsigned char *q = (const unsigned char *)p;
    CfgU8 st;
    int i;
    if (n < 0) return -1;
    if (n > 0 && !q) return -1;
    cfg_i_u8_reset(&st);
    for (i = 0; i < n; i++) {
        if (cfg_i_u8_byte(&st, q[i]) != 0) return -1;
    }
    return cfg_i_u8_done(&st) ? 0 : -1;
}

/* ======================================================================== */
/*  SHM の列 (libos32db と同じ読み方)                                        */
/* ======================================================================== */

static DB_ColumnInfo *col_info(int col)
{
    unsigned char *shm = cfg_backend()->shm();
    DB_ResultHeader *hdr = (DB_ResultHeader *)shm;
    if (!shm) return (DB_ColumnInfo *)0;
    if (col < 0 || col >= (int)hdr->column_count) return (DB_ColumnInfo *)0;
    return (DB_ColumnInfo *)(shm + sizeof(DB_ResultHeader)
                             + (u32)col * sizeof(DB_ColumnInfo));
}

int cfg_i_col_count(void)
{
    unsigned char *shm = cfg_backend()->shm();
    if (!shm) return 0;
    return (int)((DB_ResultHeader *)shm)->column_count;
}

int cfg_i_col_type(int col)
{
    DB_ColumnInfo *info = col_info(col);
    return info ? (int)info->type : DB_TYPE_NULL;
}

int cfg_i_col_len(int col)
{
    DB_ColumnInfo *info = col_info(col);
    return info ? (int)info->length : 0;
}

i32 cfg_i_col_int(int col)
{
    DB_ColumnInfo *info = col_info(col);
    if (!info || info->type != DB_TYPE_INT || info->data_offset == 0) return 0;
    return *(i32 *)(cfg_backend()->shm() + info->data_offset);
}

const void *cfg_i_col_ptr(int col)
{
    DB_ColumnInfo *info = col_info(col);
    if (!info || info->data_offset == 0) return (const void *)0;
    return (const void *)(cfg_backend()->shm() + info->data_offset);
}

/* ======================================================================== */
/*  SQL の下働き                                                             */
/* ======================================================================== */

void cfg_i_note(CfgDb *db)
{
    if (db->handle >= 0) db->last_sqlite = cfg_backend()->db_error_code(db->handle);
}

int cfg_i_exec(CfgDb *db, const char *sql)
{
    if (db->handle < 0) return -1;
    if (cfg_backend()->db_exec(db->handle, sql) != 0) {
        cfg_i_note(db);
        return -1;
    }
    return 0;
}

int cfg_i_prepare(CfgDb *db, const char *sql)
{
    if (db->handle < 0) return -1;
    if (cfg_backend()->db_prepare_only(db->handle, sql) != 0) {
        cfg_i_note(db);
        return -1;
    }
    return 0;
}

/* ======================================================================== */
/*  schema 検査 (票 §1-1b) — 表全体を 1 本の SQL で見る                      */
/* ======================================================================== */

static const char SQL_SCHEMA[] =
    "SELECT COUNT(*), MIN(typeof(schema_version)), MAX(typeof(schema_version)),"
    " MIN(schema_version), MAX(schema_version),"
    " MIN(schema_version BETWEEN 1 AND 2147483647) FROM meta";

static int is_integer_typeof(int col)
{
    const char *s = (const char *)cfg_i_col_ptr(col);
    if (cfg_i_col_type(col) != DB_TYPE_TEXT || !s) return 0;
    return s[0] == 'i' && s[1] == 'n' && s[2] == 't' && s[3] == 'e' &&
           s[4] == 'g' && s[5] == 'e' && s[6] == 'r' && s[7] == '\0';
}

/* SQLite のコード → CFG_*。schema 検査の途中で出た失敗はここで写す。
 * 「meta 表が無い」の SQLITE_ERROR、非 SQLite ファイルの NOTADB、
 * ページの破損 CORRUPT はどれも CORRUPT。IOERR / NOMEM / 通常の BUSY は
 * 「壊れている」とは違うので ERROR のまま (往復 1 の ⑦)。 */
static int map_sql_failure(int code)
{
    if (code == CFG_SQLITE_BUSY_RECOVERY) return CFG_CORRUPT;
    switch (CFG_SQLITE_PRIMARY(code)) {
    case CFG_SQLITE_ERROR:
    case CFG_SQLITE_NOTADB:
    case CFG_SQLITE_CORRUPT:
        return CFG_CORRUPT;
    default:
        return CFG_ERROR;
    }
}

int cfg_i_schema_check(CfgDb *db, int *version_out)
{
    const CfgBackend *b = cfg_backend();
    int rc, count, lo, hi, in_range, ver;

    *version_out = 0;
    if (db->handle < 0) return CFG_ERROR;
    if (b->db_prepare_only(db->handle, SQL_SCHEMA) != 0) {
        cfg_i_note(db);
        return map_sql_failure(db->last_sqlite);
    }
    rc = b->db_step(db->handle);
    if (rc != DB_STATUS_ROW) {
        cfg_i_note(db);
        b->db_finalize(db->handle);
        return map_sql_failure(db->last_sqlite);
    }
    count = (int)cfg_i_col_int(0);
    lo = (int)cfg_i_col_int(3);
    hi = (int)cfg_i_col_int(4);
    in_range = (cfg_i_col_type(5) == DB_TYPE_INT) ? (int)cfg_i_col_int(5) : 0;
    ver = lo;
    if (count != 1 || !is_integer_typeof(1) || !is_integer_typeof(2) ||
        lo != hi || in_range != 1 ||
        cfg_i_col_type(3) != DB_TYPE_INT || cfg_i_col_type(4) != DB_TYPE_INT) {
        b->db_finalize(db->handle);
        return CFG_CORRUPT;
    }
    b->db_finalize(db->handle);
    *version_out = ver;
    if (ver > CFG_SCHEMA_VERSION) return CFG_VERSION;
    return CFG_OK;
}

/* ======================================================================== */
/*  cfg_open / cfg_close                                                     */
/* ======================================================================== */

static int map_open_failure(CfgDb *db)
{
    const CfgBackend *b = cfg_backend();
    int code = b->db_error_code(-1);
    OS32_Stat st;

    db->last_sqlite = code;
    if (code == CFG_SQLITE_BUSY_RECOVERY) return CFG_CORRUPT;   /* hot journal */
    switch (CFG_SQLITE_PRIMARY(code)) {
    case CFG_SQLITE_CANTOPEN:
        /* 「無い」と「KAPI / 下位層が断った」を stat で分ける (票 §1-1a)。 */
        if (b->sys_stat(CFG_DB_PATH, &st) == OS32_ERR_NOTFOUND) return CFG_MISSING;
        return CFG_ERROR;
    case CFG_SQLITE_NOTADB:
    case CFG_SQLITE_CORRUPT:
        return CFG_CORRUPT;
    default:
        return CFG_ERROR;
    }
}

/* 内部で接続を捨てる。close が失敗した slot は F1 で隔離されているので
 * **再 close しない**が、失敗したことは記録して公開 close へ伝える
 * (往復 1 の ⑨: 隔離された接続が残ったのを呼び手が検出できるように)。 */
static void drop_handle(CfgDb *db)
{
    const CfgBackend *b = cfg_backend();
    int code;

    if (db->handle < 0) return;
    if (b->db_close(db->handle) != 0) {
        code = b->db_error_code(db->handle);
        if (!db->orphan_close) db->orphan_close = code ? code : -1;
        /* close の診断で **元の操作診断を上書きしない** (往復 2 の 3)。 */
        if (!db->last_sqlite) db->last_sqlite = code;
    }
    db->handle = -1;
}

int cfg_open(CfgDb **out, int writable)
{
    const CfgBackend *b;
    CfgDb *db = &g_db;
    int st, ver;

    if (!out) return OS32_ERR_INVAL;
    *out = (CfgDb *)0;
    if (db->in_use) return OS32_ERR_INVAL;      /* 1 プロセス 1 接続 */
    b = cfg_backend();
    if (!b) return OS32_ERR_INVAL;

    db->in_use = 1;
    db->handle = -1;
    db->want_write = writable ? 1 : 0;
    db->rw = 0;
    db->status = CFG_OK;
    db->schema_version = 0;
    db->last_sqlite = 0;
    db->txn = 0;
    db->in_enum = 0;
    db->orphan_close = 0;
    db->cleanup_sqlite = 0;

    /* (a) 必ず RO で開いて検査する。 */
    db->handle = b->db_open_existing(CFG_DB_PATH, 0);
    if (db->handle < 0) {
        db->handle = -1;
        db->status = map_open_failure(db);
        *out = db;
        return 0;
    }
    st = cfg_i_schema_check(db, &ver);
    db->status = st;
    db->schema_version = ver;
    if (st == CFG_CORRUPT || st == CFG_ERROR) {
        drop_handle(db);
        *out = db;
        return 0;
    }
    /* (c) RW へ切り替えるのは RO 検査が CFG_OK のときだけ。 */
    if (writable && st == CFG_OK) {
        drop_handle(db);
        if (db->orphan_close) {
            db->status = CFG_ERROR;
            *out = db;
            return 0;
        }
        db->handle = b->db_open_existing(CFG_DB_PATH, 1);
        if (db->handle < 0) {
            db->handle = -1;
            db->status = map_open_failure(db);
            if (db->status == CFG_OK) db->status = CFG_ERROR;
            *out = db;
            return 0;
        }
        st = cfg_i_schema_check(db, &ver);
        if (st != CFG_OK || ver != db->schema_version) {
            drop_handle(db);
            db->status = (st == CFG_CORRUPT) ? CFG_CORRUPT : CFG_ERROR;
            db->schema_version = ver;
            *out = db;
            return 0;
        }
        db->rw = 1;
    }
    /* (d) open は BEGIN しない。 */
    *out = db;
    return 0;
}

int cfg_close(CfgDb *db)
{
    const CfgBackend *b = cfg_backend();
    int first, saved, code;

    if (!db || !db->in_use) return OS32_ERR_INVAL;
    if (db->in_enum) return OS32_ERR_INVAL;
    /* open の途中で捨てた接続の close 失敗もここで報告する。 */
    first = db->orphan_close;
    if (db->cleanup_sqlite && !first) first = db->cleanup_sqlite;
    if (db->handle >= 0) {
        if (db->txn != 0) {
            /* 成功した ROLLBACK は診断を 0 に戻す。先に保存する (票 §1-4)。*/
            saved = db->last_sqlite;
            if (b->db_exec(db->handle, "ROLLBACK") != 0) {
                cfg_i_note(db);
                code = db->last_sqlite ? db->last_sqlite : -1;
                if (!db->cleanup_sqlite) db->cleanup_sqlite = code;
                if (!first) first = code;
                /* 後片付けの失敗で**元の診断を消さない** (往復 1 の ⑧)。 */
                if (saved) db->last_sqlite = saved;
            } else {
                db->last_sqlite = saved;
            }
            db->txn = 0;
        }
        if (b->db_close(db->handle) != 0) {
            code = b->db_error_code(db->handle);
            if (!first) first = code ? code : -1;
            if (!db->cleanup_sqlite) db->cleanup_sqlite = code ? code : -1;
            /* close の診断は close 欄へ。操作の原因は残す (往復 2 の 3)。 */
            if (!db->last_sqlite) db->last_sqlite = code;
        }
        db->handle = -1;
    }
    db->in_use = 0;
    db->rw = 0;
    g_close_error = first;
    return first ? OS32_ERR_IO : 0;
}

/* ======================================================================== */
/*  読み                                                                     */
/* ======================================================================== */

/* 保存済みの値の検査は **SQL 側でも** 行う。SHM は整数を 32bit に落とすので、
 * `ival=4294967297` や `type=4294967296` は C まで来た時点で正当値に化ける
 * (往復 2 の 5)。縮小の前に SQLite に判定させ、その結果を受け取る。
 *   col0 = 正規化した宣言型 (0/1/2) か -1 (契約外)
 *   col1 = ival の状態 (0 = NULL / 1 = int32 の範囲内 / -1 = 契約外)
 *   col2..4 = 生の ival / tval / bval */
static const char SQL_GET[] =
    "SELECT CASE WHEN typeof(type)='integer' AND type>=0 AND type<=2"
    " THEN type ELSE -1 END,"
    "CASE WHEN ival IS NULL THEN 0 WHEN typeof(ival)='integer'"
    " AND ival>=-2147483648 AND ival<=2147483647 THEN 1 ELSE -1 END,"
    "ival,tval,bval FROM settings WHERE scope=? AND key=?";

/* 使える接続か (get 用): 状態 OK か VERSION なら読める。
 * **再入 (enum の callback の中) は「読めない」とは別物** なので分けて見る
 * (往復 1 の ⑫)。 */
static int reentered(const CfgDb *db)
{
    return db && db->in_enum;
}

static int readable(CfgDb *db)
{
    if (!db || !db->in_use || db->in_enum) return 0;
    if (db->handle < 0) return 0;
    return db->status == CFG_OK || db->status == CFG_VERSION;
}

static int names_ok(const char *scope, const char *key)
{
    /* 読みでは長さだけを見る。DB の中身を規則で切り捨てない
     * (規則の強制は set / delete 側、票 §1-3)。 */
    return cfg_i_len_ok(scope, CFG_SCOPE_MAX) && cfg_i_len_ok(key, CFG_KEY_MAX);
}

/* 1 行を取りに行く。DB_STATUS_ROW なら列が SHM に載っている。
 * 呼び手は読み終えたら get_finish() を必ず呼ぶ。 */
static int get_row(CfgDb *db, const char *scope, const char *key)
{
    const CfgBackend *b = cfg_backend();
    int rc;

    if (cfg_i_prepare(db, SQL_GET) != 0) { db->status = CFG_ERROR; return -1; }
    if (b->db_bind_text(db->handle, 1, scope, cfg_strlen(scope)) != 0 ||
        b->db_bind_text(db->handle, 2, key, cfg_strlen(key)) != 0) {
        /* bind の失敗 (NOMEM 等) も障害。未設定と取り違えさせない (⑩)。 */
        cfg_i_note(db);
        b->db_finalize(db->handle);
        db->status = CFG_ERROR;
        return -1;
    }
    rc = b->db_step(db->handle);
    if (rc == DB_STATUS_ERROR) {
        cfg_i_note(db);
        b->db_finalize(db->handle);
        db->status = CFG_ERROR;
        return -1;
    }
    if (rc != DB_STATUS_ROW) {
        b->db_finalize(db->handle);
        return 0;
    }
    return 1;
}

static void get_finish(CfgDb *db)
{
    cfg_backend()->db_finalize(db->handle);
}

/* 共通の入口検査。0 = 進んでよい / 負 = そのまま返す値。 */
static int read_ready(CfgDb *db, const char *scope, const char *key)
{
    if (reentered(db)) return OS32_ERR_INVAL;
    if (!names_ok(scope, key)) return OS32_ERR_INVAL;
    if (!db || !db->in_use) return OS32_ERR_INVAL;
    if (!readable(db)) {
        /* 障害で読めないのと「無い」を混ぜない (⑤)。 */
        return db->status == CFG_ERROR ? OS32_ERR_IO : OS32_ERR_NOTFOUND;
    }
    return 0;
}

/* 行を読み込み、宣言型と「値が NULL か」を返す。0 を返したときだけ行は
 * **読み込まれたまま** (呼び手が get_finish を呼ぶ)。負のときは始末済み。 */
static int load_row(CfgDb *db, const char *scope, const char *key,
                    int *type_out, int *null_out)
{
    int rc, t, state, ct;

    rc = read_ready(db, scope, key);
    if (rc != 0) return rc;
    rc = get_row(db, scope, key);
    if (rc < 0) return OS32_ERR_IO;
    if (rc == 0) return OS32_ERR_NOTFOUND;

    if (cfg_i_col_type(0) != DB_TYPE_INT) { get_finish(db); return OS32_ERR_NOSYS; }
    t = (int)cfg_i_col_int(0);
    if (t != CFG_TYPE_INT && t != CFG_TYPE_TEXT && t != CFG_TYPE_BLOB) {
        get_finish(db);
        return OS32_ERR_NOSYS;          /* 契約外 / 未知の type 列 */
    }
    if (t == CFG_TYPE_INT) {
        if (cfg_i_col_type(1) != DB_TYPE_INT) { get_finish(db); return OS32_ERR_NOSYS; }
        state = (int)cfg_i_col_int(1);
        if (state == 0) *null_out = 1;
        else if (state == 1) *null_out = 0;
        else { get_finish(db); return OS32_ERR_NOSYS; }   /* int32 の範囲外 */
    } else {
        ct = cfg_i_col_type(t == CFG_TYPE_TEXT ? 3 : 4);
        if (ct == DB_TYPE_NULL) *null_out = 1;
        else if (ct == (t == CFG_TYPE_TEXT ? DB_TYPE_TEXT : DB_TYPE_BLOB)) *null_out = 0;
        else { get_finish(db); return OS32_ERR_NOSYS; }   /* 宣言と違う型 */
    }
    *type_out = t;
    return 0;
}

int cfg_get_info(CfgDb *db, const char *scope, const char *key)
{
    int t = 0, isnull = 0, rc = load_row(db, scope, key, &t, &isnull);
    if (rc != 0) return rc;
    get_finish(db);
    return t | (isnull ? CFG_INFO_NULL : 0);
}

int cfg_read_int(CfgDb *db, const char *scope, const char *key, int *out)
{
    int t = 0, isnull = 0, rc;
    if (!out) return OS32_ERR_INVAL;
    rc = load_row(db, scope, key, &t, &isnull);
    if (rc != 0) return rc;
    if (t != CFG_TYPE_INT || isnull) { get_finish(db); return OS32_ERR_NOTFOUND; }
    *out = (int)cfg_i_col_int(2);
    get_finish(db);
    return 0;
}

int cfg_get_int(CfgDb *db, const char *scope, const char *key, int def)
{
    int v;
    return cfg_read_int(db, scope, key, &v) == 0 ? v : def;
}

/* text (col 3) / blob (col 4) の共通取り出し。
 * 保存済みの値にも契約の上限・UTF-8・埋込み NUL を当て、**out を 1 バイトも
 * 書く前に**断る (往復 2 の 6)。書き手側の検査では既存 DB / VERSION /
 * 手で壊された行は覆えない。 */
static int get_value(CfgDb *db, const char *scope, const char *key,
                     int want_type, int col, void *out, int cap, int is_text)
{
    const unsigned char *src;
    unsigned char *dst = (unsigned char *)out;
    int len, i, t = 0, isnull = 0, rc;

    if (!out || cap < 0) return OS32_ERR_INVAL;
    rc = load_row(db, scope, key, &t, &isnull);
    if (rc != 0) return rc;
    if (t != want_type || isnull) { get_finish(db); return OS32_ERR_NOTFOUND; }

    len = cfg_i_col_len(col);
    if (len < 0 || len > (is_text ? CFG_TEXT_MAX : CFG_BLOB_MAX)) {
        get_finish(db);
        return OS32_ERR_NOSYS;              /* 契約の上限を超えた保存値 */
    }
    src = (const unsigned char *)cfg_i_col_ptr(col);
    if (!src && len > 0) {
        cfg_i_note(db);
        get_finish(db);
        db->status = CFG_ERROR;
        return OS32_ERR_IO;
    }
    if (is_text) {
        for (i = 0; i < len; i++) {
            if (src[i] == 0) { get_finish(db); return OS32_ERR_NOSYS; }
        }
        if (cfg_i_utf8_check(src, len) != 0) {
            get_finish(db);
            return OS32_ERR_NOSYS;          /* 不正 UTF-8 の保存値 */
        }
    }
    if (len > cap - (is_text ? 1 : 0)) {
        get_finish(db);
        return OS32_ERR_NOSPC;              /* out は 1 バイトも書かない */
    }
    for (i = 0; i < len; i++) dst[i] = src[i];   /* SHM から即コピー */
    if (is_text) dst[len] = 0;
    get_finish(db);
    return len;
}

int cfg_get_text(CfgDb *db, const char *scope, const char *key,
                 char *out, int cap)
{
    return get_value(db, scope, key, CFG_TYPE_TEXT, 3, out, cap, 1);
}

int cfg_get_blob(CfgDb *db, const char *scope, const char *key,
                 void *out, int cap)
{
    return get_value(db, scope, key, CFG_TYPE_BLOB, 4, out, cap, 0);
}

/* ======================================================================== */
/*  トランザクションと書き                                                   */
/* ======================================================================== */

static int writable_now(CfgDb *db)
{
    if (!db || !db->in_use || db->in_enum) return 0;
    if (!db->rw || db->handle < 0) return 0;
    return db->status == CFG_OK;
}

int cfg_begin(CfgDb *db)
{
    if (!writable_now(db)) return OS32_ERR_INVAL;
    if (db->txn != 0) return OS32_ERR_INVAL;
    if (cfg_i_exec(db, "BEGIN IMMEDIATE") != 0) return OS32_ERR_IO;
    db->txn = 1;
    return 0;
}

/* 後片付けの ROLLBACK。**元の診断 (`saved`) は必ず残す**: COMMIT が IOERR で
 * 落ちたとき SQLite は自分で txn を巻き戻すので、続く明示 ROLLBACK は
 * 「transaction がない」で失敗する。その失敗で原因が消えないように、
 * 後片付けの失敗は `cleanup_sqlite` に別建てで持つ (往復 1 の ⑧)。 */
static int cleanup_rollback(CfgDb *db, int saved)
{
    int rc = cfg_i_exec(db, "ROLLBACK");
    if (rc != 0) {
        if (!db->cleanup_sqlite)
            db->cleanup_sqlite = db->last_sqlite ? db->last_sqlite : -1;
    }
    if (saved) db->last_sqlite = saved;
    db->txn = 0;
    return rc;
}

int cfg_rollback(CfgDb *db)
{
    if (!db || !db->in_use || db->in_enum) return OS32_ERR_INVAL;
    if (db->txn == 0 || db->handle < 0) return OS32_ERR_INVAL;
    return cleanup_rollback(db, db->last_sqlite) != 0 ? OS32_ERR_IO : 0;
}

int cfg_commit(CfgDb *db)
{
    int saved;
    if (!db || !db->in_use || db->in_enum) return OS32_ERR_INVAL;
    if (db->txn == 0 || db->handle < 0) return OS32_ERR_INVAL;
    /* failed、または途中で接続が壊れたときは commit を拒否して rollback。 */
    if (db->txn == 2 || db->status != CFG_OK) {
        db->txn = 2;
        cleanup_rollback(db, db->last_sqlite);
        return OS32_ERR_IO;
    }
    if (cfg_i_exec(db, "COMMIT") != 0) {
        saved = db->last_sqlite;             /* COMMIT 自身の失敗コード */
        cleanup_rollback(db, saved);
        return OS32_ERR_IO;
    }
    db->txn = 0;
    return 0;
}

static const char SQL_SET[] =
    "INSERT OR REPLACE INTO settings(scope,key,type,ival,tval,bval)"
    " VALUES(?,?,?,?,?,?)";
static const char SQL_DEL[] =
    "DELETE FROM settings WHERE scope=? AND key=?";
/* S3 (`cfg import` の置換と `v:null`)。scope は必ず bind する。 */
static const char SQL_DEL_SCOPE[] =
    "DELETE FROM settings WHERE scope=?";
static const char SQL_DEL_ALL[] =
    "DELETE FROM settings";
static const char SQL_SET_NULL[] =
    "INSERT OR REPLACE INTO settings(scope,key,type,ival,tval,bval)"
    " VALUES(?,?,?,NULL,NULL,NULL)";

/* 実行中の txn の中で set / delete が**どんな理由で**断られても、その txn は
 * failed にする。FOUNDATION §2-3 の「set 失敗で transaction を failed 状態に
 * し、commit は拒否して rollback」は入力検証の失敗も含む — そうしないと
 * 「A は通って B は弾かれた」半端な更新が commit されてしまう (往復 1 の ④)。*/
static int reject_write(CfgDb *db, int rc)
{
    if (db && db->in_use && db->txn == 1) db->txn = 2;
    return rc;
}

/* set / delete の共通前提。0 = 進んでよい。
 * **前提検査の失敗も** 実行中の txn を failed にする。get の途中で接続が
 * CFG_ERROR になると `writable_now` が false になり、そこで返した INVAL が
 * txn を素通しすると「A だけ commit される」(往復 2 の 2)。 */
static int can_write_gate(CfgDb *db)
{
    if (!db || !db->in_use) return OS32_ERR_INVAL;
    /* 列挙の callback からの書き込みも拒否するが、実行中の txn は failed に
     * する (往復 3 の B2)。ここでは SQL を流さない — 巻き戻しは callback が
     * 戻った後、外側の commit / close が行う。 */
    if (db->in_enum) return reject_write(db, OS32_ERR_INVAL);
    if (db->txn == 2) return OS32_ERR_INVAL;         /* 既に failed */
    if (!writable_now(db)) return reject_write(db, OS32_ERR_INVAL);
    if (db->txn != 1) return OS32_ERR_INVAL;         /* txn 外は拒否 */
    return 0;
}

static int can_write(CfgDb *db, const char *scope, const char *key)
{
    int rc = can_write_gate(db);
    if (rc != 0) return rc;
    if (!cfg_i_valid_scope(scope) || !cfg_i_valid_key(key))
        return reject_write(db, OS32_ERR_INVAL);
    if (cfg_i_utf8_check(scope, cfg_strlen(scope)) != 0)
        return reject_write(db, OS32_ERR_INVAL);
    if (cfg_i_utf8_check(key, cfg_strlen(key)) != 0)
        return reject_write(db, OS32_ERR_INVAL);
    return 0;
}

static void set_failed(CfgDb *db)
{
    cfg_i_note(db);
    db->txn = 2;
}

static int set_value(CfgDb *db, const char *scope, const char *key, int type,
                  int iv, const char *tv, int tlen,
                  const void *bv, int blen)
{
    const CfgBackend *b = cfg_backend();
    int rc;
    /* 空の text / blob も NULL ではなく「空値」として入れる (票 §1-2)。
     * 長さ 0 でも非 NULL のポインタを渡す。 */
    static const char empty[1] = { 0 };

    if (cfg_i_prepare(db, SQL_SET) != 0) { db->txn = 2; return OS32_ERR_IO; }
    if (b->db_bind_text(db->handle, 1, scope, cfg_strlen(scope)) != 0 ||
        b->db_bind_text(db->handle, 2, key, cfg_strlen(key)) != 0 ||
        b->db_bind_int(db->handle, 3, type) != 0) {
        set_failed(db);
        b->db_finalize(db->handle);
        return OS32_ERR_IO;
    }
    rc = (type == CFG_TYPE_INT) ? b->db_bind_int(db->handle, 4, iv)
                                : b->db_bind_null(db->handle, 4);
    if (rc == 0) {
        rc = (type == CFG_TYPE_TEXT)
             ? b->db_bind_text(db->handle, 5, tv ? tv : empty, tlen)
             : b->db_bind_null(db->handle, 5);
    }
    if (rc == 0) {
        rc = (type == CFG_TYPE_BLOB)
             ? b->db_bind_blob(db->handle, 6, bv ? bv : (const void *)empty, blen)
             : b->db_bind_null(db->handle, 6);
    }
    if (rc != 0) {
        set_failed(db);
        b->db_finalize(db->handle);
        return OS32_ERR_IO;
    }
    if (b->db_step(db->handle) != DB_STATUS_DONE) {
        set_failed(db);
        b->db_finalize(db->handle);
        return OS32_ERR_IO;
    }
    b->db_finalize(db->handle);
    return 0;
}

int cfg_set_int(CfgDb *db, const char *scope, const char *key, int v)
{
    int rc = can_write(db, scope, key);
    if (rc != 0) return rc;
    return set_value(db, scope, key, CFG_TYPE_INT, v, (const char *)0, 0,
                  (const void *)0, 0);
}

int cfg_set_text(CfgDb *db, const char *scope, const char *key, const char *s)
{
    int rc = can_write(db, scope, key);
    int n;
    if (rc != 0) return rc;
    if (!s) return reject_write(db, OS32_ERR_INVAL);
    /* 上限を超えるかどうかを数える段で打ち切る (長い文字列を走り切らない)。*/
    for (n = 0; s[n]; n++) {
        if (n >= CFG_TEXT_MAX) return reject_write(db, OS32_ERR_INVAL);
    }
    if (cfg_i_utf8_check(s, n) != 0) return reject_write(db, OS32_ERR_INVAL);
    return set_value(db, scope, key, CFG_TYPE_TEXT, 0, s, n, (const void *)0, 0);
}

int cfg_set_blob(CfgDb *db, const char *scope, const char *key,
                 const void *p, int n)
{
    int rc = can_write(db, scope, key);
    if (rc != 0) return rc;
    if (n < 0 || n > CFG_BLOB_MAX) return reject_write(db, OS32_ERR_INVAL);
    if (n > 0 && !p) return reject_write(db, OS32_ERR_INVAL);
    return set_value(db, scope, key, CFG_TYPE_BLOB, 0, (const char *)0, 0, p, n);
}

int cfg_delete(CfgDb *db, const char *scope, const char *key)
{
    const CfgBackend *b = cfg_backend();
    int rc = can_write(db, scope, key);
    if (rc != 0) return rc;
    if (cfg_i_prepare(db, SQL_DEL) != 0) { db->txn = 2; return OS32_ERR_IO; }
    if (b->db_bind_text(db->handle, 1, scope, cfg_strlen(scope)) != 0 ||
        b->db_bind_text(db->handle, 2, key, cfg_strlen(key)) != 0) {
        set_failed(db);
        b->db_finalize(db->handle);
        return OS32_ERR_IO;
    }
    if (b->db_step(db->handle) != DB_STATUS_DONE) {
        set_failed(db);
        b->db_finalize(db->handle);
        return OS32_ERR_IO;
    }
    b->db_finalize(db->handle);
    return 0;
}

/* ======================================================================== */
/*  S3: scope 単位の削除と「宣言型つき NULL 行」                             */
/*                                                                          */
/*  `cfg import` の置換 (対象 scope を消してから入れ直す) と、export の      */
/*  `"v":null` を往復させるためだけの 2 本。契約は set / delete と同じ:      */
/*  txn の中でだけ、失敗で txn を failed、名前は bind、列挙中は拒否。        */
/* ======================================================================== */

int cfg_delete_scope(CfgDb *db, const char *scope)
{
    const CfgBackend *b = cfg_backend();
    int rc = can_write_gate(db);

    if (rc != 0) return rc;
    if (scope) {
        if (!cfg_i_valid_scope(scope)) return reject_write(db, OS32_ERR_INVAL);
        if (cfg_i_utf8_check(scope, cfg_strlen(scope)) != 0)
            return reject_write(db, OS32_ERR_INVAL);
    }
    if (cfg_i_prepare(db, scope ? SQL_DEL_SCOPE : SQL_DEL_ALL) != 0) {
        db->txn = 2;
        return OS32_ERR_IO;
    }
    if (scope && b->db_bind_text(db->handle, 1, scope, cfg_strlen(scope)) != 0) {
        set_failed(db);
        b->db_finalize(db->handle);
        return OS32_ERR_IO;
    }
    if (b->db_step(db->handle) != DB_STATUS_DONE) {
        set_failed(db);
        b->db_finalize(db->handle);
        return OS32_ERR_IO;
    }
    b->db_finalize(db->handle);
    return 0;
}

int cfg_set_null(CfgDb *db, const char *scope, const char *key, int type)
{
    const CfgBackend *b = cfg_backend();
    int rc = can_write(db, scope, key);

    if (rc != 0) return rc;
    if (type != CFG_TYPE_INT && type != CFG_TYPE_TEXT && type != CFG_TYPE_BLOB)
        return reject_write(db, OS32_ERR_INVAL);
    if (cfg_i_prepare(db, SQL_SET_NULL) != 0) { db->txn = 2; return OS32_ERR_IO; }
    if (b->db_bind_text(db->handle, 1, scope, cfg_strlen(scope)) != 0 ||
        b->db_bind_text(db->handle, 2, key, cfg_strlen(key)) != 0 ||
        b->db_bind_int(db->handle, 3, type) != 0) {
        set_failed(db);
        b->db_finalize(db->handle);
        return OS32_ERR_IO;
    }
    if (b->db_step(db->handle) != DB_STATUS_DONE) {
        set_failed(db);
        b->db_finalize(db->handle);
        return OS32_ERR_IO;
    }
    b->db_finalize(db->handle);
    return 0;
}
