/* ======================================================================== */
/*  CFG_INIT.C — `cfg init` の実体 (票 §1-7)                                */
/*                                                                          */
/*  「欠損のときだけ作る」。DB の場所は固定 (/etc/settings.db) で、任意の     */
/*  パスは取らない。残骸 (`-journal` / `.new*`) は**消さない** — 隔離された   */
/*  接続が掴んだままかどうかは別プロセスからは判定できないので、片付けは     */
/*  S3 の再起動後リカバリの領分 (往復 3 の B1)。                             */
/*                                                                          */
/*  tsv は 2 巡する: 1 巡目で全行を検証し、通ったときだけ `.new` を作って     */
/*  2 巡目で流し込む (「全行検証してから作る」)。                            */
/* ======================================================================== */

#include "cfg_internal.h"

static int g_init_reason;

int cfg_last_init_reason(void)
{
    return g_init_reason;
}

/* ---- tsv のストリーム ---------------------------------------------- */

#define TSV_BUF 512

typedef struct {
    int  fd;
    int  n;
    int  pos;
    int  eof;
    unsigned char buf[TSV_BUF];
} TsvFile;

static int tsv_getc(void *ctx)
{
    TsvFile *f = (TsvFile *)ctx;
    int rc;
    if (f->pos >= f->n) {
        if (f->eof) return -1;
        rc = cfg_backend()->sys_read(f->fd, f->buf, (u32)TSV_BUF);
        if (rc < 0) return -2;
        if (rc == 0) { f->eof = 1; return -1; }
        f->n = rc;
        f->pos = 0;
    }
    return (int)f->buf[f->pos++];
}

static int tsv_open(TsvFile *f, const char *path)
{
    f->n = 0;
    f->pos = 0;
    f->eof = 0;
    f->fd = cfg_backend()->sys_open(path, KAPI_O_RDONLY);
    return f->fd;
}

static void tsv_shut(TsvFile *f)
{
    if (f->fd >= 0) cfg_backend()->sys_close(f->fd);
    f->fd = -1;
}

/* ---- 生成先 -------------------------------------------------------- */

static const char DDL_META[] =
    "CREATE TABLE meta (schema_version INTEGER NOT NULL, created TEXT)";
static const char DDL_SETTINGS[] =
    "CREATE TABLE settings (scope TEXT NOT NULL, key TEXT NOT NULL,"
    " type INTEGER NOT NULL, ival INTEGER, tval TEXT, bval BLOB,"
    " PRIMARY KEY (scope, key)) WITHOUT ROWID";
static const char SQL_META_INS[] =
    "INSERT INTO meta (schema_version, created) VALUES (?, ?)";
static const char SQL_ROW_INS[] =
    "INSERT INTO settings (scope, key, type, ival, tval, bval)"
    " VALUES (?, ?, ?, ?, ?, ?)";

static CfgTsvRow g_row;
static CfgDb g_new;                  /* `.new` 用の使い捨て接続記述 */

static int put_u32(char *buf, u32 v)
{
    char tmp[12];
    int n = 0, i = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (int)(v % 10UL)); v /= 10UL; }
    while (n) buf[i++] = tmp[--n];
    buf[i] = '\0';
    return i;
}

static int strlen_i(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int insert_row(const CfgTsvRow *r, void *ctx)
{
    const CfgBackend *b = cfg_backend();
    CfgDb *db = (CfgDb *)ctx;
    static const char empty[1] = { 0 };
    int rc;

    if (cfg_i_prepare(db, SQL_ROW_INS) != 0) return -1;
    if (b->db_bind_text(db->handle, 1, r->scope, strlen_i(r->scope)) != 0 ||
        b->db_bind_text(db->handle, 2, r->key, strlen_i(r->key)) != 0 ||
        b->db_bind_int(db->handle, 3, r->type) != 0) {
        cfg_i_note(db);
        b->db_finalize(db->handle);
        return -1;
    }
    rc = (r->type == CFG_TYPE_INT) ? b->db_bind_int(db->handle, 4, r->ival)
                                   : b->db_bind_null(db->handle, 4);
    if (rc == 0) {
        rc = (r->type == CFG_TYPE_TEXT)
             ? b->db_bind_text(db->handle, 5, r->tval, r->tlen)
             : b->db_bind_null(db->handle, 5);
    }
    if (rc == 0) {
        rc = (r->type == CFG_TYPE_BLOB)
             ? b->db_bind_blob(db->handle, 6,
                               r->blen ? (const void *)r->bval
                                       : (const void *)empty, r->blen)
             : b->db_bind_null(db->handle, 6);
    }
    if (rc != 0 || b->db_step(db->handle) != DB_STATUS_DONE) {
        cfg_i_note(db);
        b->db_finalize(db->handle);
        return -1;
    }
    b->db_finalize(db->handle);
    return 0;
}

/* stat は三値で持つ: 1 = ある / 0 = **明示的に** 無い / -1 = 判定不能。
 * I/O 障害を「無い」に丸めると、既存の `.new` を自分の生成物と思って消したり、
 * rename が両名を残したのに本体を無いと判断して `.new` を unlink したりする
 * (往復 1 の ②)。**変更・削除へ進むのは 0 のときだけ**。 */
#define CFG_PATH_ABSENT   0
#define CFG_PATH_PRESENT  1
#define CFG_PATH_UNKNOWN (-1)

static int path_state(const char *path)
{
    OS32_Stat st;
    int rc = cfg_backend()->sys_stat(path, &st);
    if (rc == 0) return CFG_PATH_PRESENT;
    if (rc == OS32_ERR_NOTFOUND) return CFG_PATH_ABSENT;
    return CFG_PATH_UNKNOWN;
}

/* `.new` を作って書き切る。0 / -1。close は呼び手が行う。 */
static int build_new(const char *tsv_path, CfgDb *db)
{
    const CfgBackend *b = cfg_backend();
    TsvFile f;
    CfgTsvErr err;
    char created[16];
    int len;

    if (cfg_i_exec(db, "PRAGMA page_size=1024") != 0) return -1;
    if (cfg_i_exec(db, "PRAGMA journal_mode=DELETE") != 0) return -1;
    if (cfg_i_exec(db, "PRAGMA user_version=1") != 0) return -1;
    if (cfg_i_exec(db, DDL_META) != 0) return -1;
    if (cfg_i_exec(db, DDL_SETTINGS) != 0) return -1;
    if (cfg_i_exec(db, "BEGIN") != 0) return -1;

    len = put_u32(created, b->get_tick ? b->get_tick() : 0UL);
    if (cfg_i_prepare(db, SQL_META_INS) != 0) return -1;
    if (b->db_bind_int(db->handle, 1, CFG_SCHEMA_VERSION) != 0 ||
        b->db_bind_text(db->handle, 2, created, len) != 0 ||
        b->db_step(db->handle) != DB_STATUS_DONE) {
        cfg_i_note(db);
        b->db_finalize(db->handle);
        return -1;
    }
    b->db_finalize(db->handle);

    if (tsv_open(&f, tsv_path) < 0) return -1;
    if (cfg_tsv_parse(tsv_getc, &f, &g_row, insert_row, db, &err) != 0) {
        tsv_shut(&f);
        return -1;
    }
    tsv_shut(&f);
    if (cfg_i_exec(db, "COMMIT") != 0) return -1;
    return 0;
}

static int verify_installed(void)
{
    CfgDb *db;
    int ok;
    if (cfg_open(&db, 0) != 0) return 0;
    ok = (cfg_status(db) == CFG_OK &&
          cfg_schema_version(db) == CFG_SCHEMA_VERSION);
    if (cfg_close(db) != 0) ok = 0;
    return ok;
}

static int init_done(int reason, int rc)
{
    g_init_reason = reason;
    return rc;
}

int cfg_init(const char *tsv_path)
{
    const CfgBackend *b = cfg_backend();
    TsvFile f;
    CfgTsvErr err;
    int rc, code, st_db, st_new;

    g_init_reason = CFG_INIT_NONE;
    if (!tsv_path) tsv_path = CFG_TSV_PATH;

    /* (a) 本体が無いことを確かめる。0 バイトでも「ある」= 拒否。
     * 判定不能 (I/O 障害) も拒否 — 「無い」に丸めない。 */
    st_db = path_state(CFG_DB_PATH);
    if (st_db == CFG_PATH_PRESENT) return init_done(CFG_INIT_EXISTS, OS32_ERR_EXIST);
    if (st_db != CFG_PATH_ABSENT) return init_done(CFG_INIT_STAT, OS32_ERR_IO);

    /* (b)(c) 残骸があれば拒否する。**消さない**。判定不能も同じく進まない。 */
    if (path_state(CFG_DB_JOURNAL_PATH) != CFG_PATH_ABSENT)
        return init_done(CFG_INIT_JOURNAL, OS32_ERR_NOTEMPTY);
    if (path_state(CFG_DB_NEW_PATH) != CFG_PATH_ABSENT ||
        path_state(CFG_DB_NEW_JOURNAL_PATH) != CFG_PATH_ABSENT)
        return init_done(CFG_INIT_STALE_NEW, OS32_ERR_NOTEMPTY);

    /* (d-1) 先に tsv を全行検証する。ここで落ちたら何も作らない。
     * 重複 (scope, key) だけは 2 巡目の PRIMARY KEY が弾く (控えを持たない)。*/
    if (tsv_open(&f, tsv_path) < 0) return init_done(CFG_INIT_TSV, OS32_ERR_NOTFOUND);
    rc = cfg_tsv_parse(tsv_getc, &f, &g_row,
                       (int (*)(const CfgTsvRow *, void *))0, (void *)0, &err);
    tsv_shut(&f);
    if (rc != 0) return init_done(CFG_INIT_TSV, OS32_ERR_INVAL);

    /* (d-2) `.new` を作って 2 巡目を流し込む。 */
    g_new.in_use = 1;
    g_new.txn = 0;
    g_new.in_enum = 0;
    g_new.status = CFG_OK;
    g_new.rw = 1;
    g_new.want_write = 1;
    g_new.schema_version = CFG_SCHEMA_VERSION;
    g_new.last_sqlite = 0;
    g_new.handle = b->db_open(CFG_DB_NEW_PATH);
    if (g_new.handle < 0) {
        g_new.in_use = 0;
        return init_done(CFG_INIT_BUILD, OS32_ERR_IO);
    }
    rc = build_new(tsv_path, &g_new);
    /* (e) close が失敗したら `.new*` は**消さない** (接続が隔離されている)。 */
    code = b->db_close(g_new.handle);
    g_new.in_use = 0;
    if (code != 0) {
        cfg_i_set_close_error(b->db_error_code(g_new.handle));
        g_new.handle = -1;
        return init_done(CFG_INIT_CLOSE, OS32_ERR_IO);
    }
    g_new.handle = -1;
    if (rc != 0) {
        /* close は成功していて、入口 (c) で `.new*` が **明示的に無かった**
         * ことも確かめてある = 消してよいのは自分が作ったものだけ。 */
        int why = (CFG_SQLITE_PRIMARY(g_new.last_sqlite) == CFG_SQLITE_CONSTRAINT)
                  ? CFG_INIT_TSV : CFG_INIT_BUILD;   /* 重複 key は tsv の違反 */
        b->sys_unlink(CFG_DB_NEW_PATH);
        b->sys_unlink(CFG_DB_NEW_JOURNAL_PATH);
        return init_done(why, why == CFG_INIT_TSV ? OS32_ERR_INVAL : OS32_ERR_IO);
    }

    /* (f) rename は原子的と仮定しない。失敗したら両方を stat する。 */
    if (b->sys_rename(CFG_DB_NEW_PATH, CFG_DB_PATH) != 0) {
        st_db = path_state(CFG_DB_PATH);
        st_new = path_state(CFG_DB_NEW_PATH);
        if (st_db == CFG_PATH_PRESENT && st_new == CFG_PATH_PRESENT) {
            /* 新名追加は成功し旧名削除と巻き戻しが失敗 = 同じ inode を 2 名が
             * 指している。`.new` を消すと本体まで壊れる (往復 2 の 1)。 */
            return init_done(CFG_INIT_AMBIGUOUS, OS32_ERR_NOTEMPTY);
        }
        if (st_db == CFG_PATH_PRESENT) {
            if (st_new == CFG_PATH_ABSENT && verify_installed())
                return init_done(CFG_INIT_NONE, 0);
            return init_done(CFG_INIT_AMBIGUOUS, OS32_ERR_NOTEMPTY);
        }
        /* 本体が **明示的に無い** ときだけ `.new*` を片付ける。判定不能なら
         * どちらも触らず recovery へ回す (往復 1 の ②)。 */
        if (st_db != CFG_PATH_ABSENT || st_new == CFG_PATH_UNKNOWN)
            return init_done(CFG_INIT_AMBIGUOUS, OS32_ERR_NOTEMPTY);
        b->sys_unlink(CFG_DB_NEW_PATH);
        b->sys_unlink(CFG_DB_NEW_JOURNAL_PATH);
        return init_done(CFG_INIT_RENAME, OS32_ERR_IO);
    }

    /* (g) 自己確認。 */
    if (!verify_installed()) return init_done(CFG_INIT_VERIFY, OS32_ERR_IO);
    return init_done(CFG_INIT_NONE, 0);
}
