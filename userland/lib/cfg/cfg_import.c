/* ======================================================================== */
/*  CFG_IMPORT.C — `cfg import <file> [--scope <s>] [--merge]` の実体        */
/*                                                                          */
/*  票 docs/archive/settings/TASK_S3.md §2。C89 [C1]。malloc しない。        */
/*                                                                          */
/*  **2 巡する**。1 巡目は検証だけ (DB を開かない): 対象行を全部読み、tsv    */
/*  reader と同じ規則 (scope / key / type / 値の上限・UTF-8・NUL) に加えて   */
/*  (scope, key) の重複と件数上限 (8192) とヘッダの版を見る。1 行でも不正    */
/*  なら**何も書かない**。2 巡目はファイルを先頭から読み直し、**同じ検証を   */
/*  全行に再適用**しながら単一トランザクションで書く。件数・版が 1 巡目と    */
/*  食い違えば commit せず rollback — 巡回の間にホスト側でファイルが差し     */
/*  替わる経路 (往復 2 の R7) を構造で塞ぐ。                                 */
/*                                                                          */
/*  重複検出は (scope, key) の FNV-1a 32bit を開番地法の表に入れ、**行の     */
/*  先頭オフセット**を控える。衝突したら `sys_lseek` で先行行を読み直し、    */
/*  復号した scope / key を文字列で比べる — 誤検出も見逃しも無い。           */
/*  スロットは件数上限 (8192) の 2 倍の 16384 (64KB)。票の 8192 個 (32KB)    */
/*  だと上限いっぱいの入力で表が**満杯**になり、線形探査の塊が育って         */
/*  読み直しが 561,944 回に膨らむ (実測)。半分の詰まり方なら 3,007 回で      */
/*  済む — 1 回が lseek + read + 行の再解析なので、ゲストでは桁違いの差。    */
/*                                                                          */
/*  `--scope` があるときは読み取り・検証・適用のすべてをその scope の行だけ  */
/*  に限る (他 scope の行は**構文検証だけ**して無視、往復 1 の B10)。        */
/* ======================================================================== */

#include "cfg_internal.h"

#define IMP_BUF      512                    /* 逐次読みの窓 */
#define IMP_HASH     16384                  /* 重複表 (件数上限の 2 倍) */
#define IMP_EMPTY    0xFFFFFFFFUL

/* 読み取りの戻り */
#define IMP_LINE     1
#define IMP_EOF      0
#define IMP_ERR_IO   (-1)
#define IMP_ERR_LONG (-2)

/* ------------------------------------------------------------------ */
/*  作業領域 (静的。cfg_import.o を引くのは cfg.bin だけなので、読むだけ */
/*  のアプリ / shlib はこの 50KB 弱を背負わない)                        */
/* ------------------------------------------------------------------ */

static u32        imp_tab[IMP_HASH];
static char       imp_line[CFG_JSON_LINE_MAX];
static char       imp_back[CFG_JSON_LINE_MAX];   /* 衝突時の読み直し */
static CfgJsonRow imp_row;
static CfgJsonRow imp_back_row;

typedef struct {
    int           fd;
    unsigned char buf[IMP_BUF];
    int           len;
    int           pos;
    u32           base;      /* buf[0] のファイル位置 */
    int           eof;
} ImpIn;

/* ------------------------------------------------------------------ */
/*  小物                                                               */
/* ------------------------------------------------------------------ */

static int imp_strlen(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int imp_streq(const char *a, const char *b)
{
    int i;
    for (i = 0; a[i] && b[i]; i++) {
        if (a[i] != b[i]) return 0;
    }
    return a[i] == b[i];
}

/* FNV-1a 32bit を scope + NUL + key にかける。 */
static u32 imp_hash(const char *scope, const char *key)
{
    u32 h = 2166136261UL;
    int i;
    for (i = 0; scope[i]; i++) {
        h ^= (u32)(unsigned char)scope[i];
        h *= 16777619UL;
    }
    h *= 16777619UL;                 /* 区切り (0 を xor しても値は変わらない) */
    for (i = 0; key[i]; i++) {
        h ^= (u32)(unsigned char)key[i];
        h *= 16777619UL;
    }
    return h;
}

/* ------------------------------------------------------------------ */
/*  逐次読み (位置を自分で持ち、lseek と噛み合わせる)                   */
/* ------------------------------------------------------------------ */

static u32 imp_pos(const ImpIn *in)
{
    return in->base + (u32)in->pos;
}

/* 1 バイト / -1 = EOF / -2 = I/O */
static int imp_get(ImpIn *in)
{
    int n;
    if (in->pos >= in->len) {
        if (in->eof) return -1;
        in->base += (u32)in->len;
        in->pos = 0;
        in->len = 0;
        n = cfg_backend()->sys_read(in->fd, in->buf, (u32)IMP_BUF);
        if (n < 0) return -2;
        if (n == 0) { in->eof = 1; return -1; }
        in->len = n;
    }
    return (int)in->buf[in->pos++];
}

static int imp_seek(ImpIn *in, u32 off)
{
    if (cfg_backend()->sys_lseek(in->fd, (int)off, SEEK_SET) < 0) return -1;
    in->base = off;
    in->pos = 0;
    in->len = 0;
    in->eof = 0;
    return 0;
}

/* 1 行を buf (CFG_JSON_LINE_MAX) に取る。LF は含めない。
 * 戻り: IMP_LINE / IMP_EOF / IMP_ERR_IO / IMP_ERR_LONG。 */
static int imp_read_line(ImpIn *in, char *buf, int *len, u32 *off_out)
{
    int n = 0, c;

    if (off_out) *off_out = imp_pos(in);
    for (;;) {
        c = imp_get(in);
        if (c == -2) return IMP_ERR_IO;
        if (c == -1) break;
        if (c == '\n') {
            buf[n] = '\0';
            *len = n;
            return IMP_LINE;
        }
        if (n >= CFG_JSON_LINE_MAX - 2) return IMP_ERR_LONG;
        buf[n] = (char)c;
        n++;
    }
    if (n == 0) return IMP_EOF;              /* 改行で終わった = 正常終端 */
    buf[n] = '\0';
    *len = n;
    return IMP_LINE;                         /* 改行の無い最終行 */
}

static int imp_open(ImpIn *in, const char *path)
{
    int fd = cfg_backend()->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return -1;
    in->fd = fd;
    in->len = 0;
    in->pos = 0;
    in->base = 0;
    in->eof = 0;
    return 0;
}

static void imp_finish(ImpIn *in)
{
    if (in->fd >= 0) cfg_backend()->sys_close(in->fd);
    in->fd = -1;
}

/* ------------------------------------------------------------------ */
/*  重複表                                                             */
/* ------------------------------------------------------------------ */

static void imp_tab_reset(void)
{
    int i;
    for (i = 0; i < IMP_HASH; i++) imp_tab[i] = IMP_EMPTY;
}

/* 0 = 新顔 (表に入れた) / 1 = 重複 / IMP_ERR_IO。
 * 衝突したスロットは先行行を読み直して**文字列で**比べる。 */
static int imp_tab_add(ImpIn *in, u32 off, const CfgJsonRow *r)
{
    u32 cur = imp_pos(in);
    int idx = (int)(imp_hash(r->scope, r->key) & (u32)(IMP_HASH - 1));
    int i, len, rc;

    for (i = 0; i < IMP_HASH; i++) {
        if (imp_tab[idx] == IMP_EMPTY) {
            imp_tab[idx] = off;
            return 0;
        }
        if (imp_seek(in, imp_tab[idx]) != 0) return IMP_ERR_IO;
        rc = imp_read_line(in, imp_back, &len, (u32 *)0);
        if (rc != IMP_LINE) return IMP_ERR_IO;
        if (cfg_json_record(imp_back, len, &imp_back_row) != CFG_JSON_OK ||
            !cfg_json_scope_usable(&imp_back_row))
            return IMP_ERR_IO;               /* 一度受理した行が読めない */
        if (imp_seek(in, cur) != 0) return IMP_ERR_IO;
        if (imp_streq(imp_back_row.scope, r->scope) &&
            imp_streq(imp_back_row.key, r->key)) return 1;
        idx = (idx + 1) & (IMP_HASH - 1);
    }
    return IMP_ERR_IO;                       /* 表が満杯 (件数上限で来ない) */
}

/* ------------------------------------------------------------------ */
/*  1 巡                                                               */
/* ------------------------------------------------------------------ */

static int imp_apply(CfgDb *db, const CfgJsonRow *r)
{
    if (r->is_null) return cfg_set_null(db, r->scope, r->key, r->type);
    if (r->type == CFG_TYPE_INT)
        return cfg_set_int(db, r->scope, r->key, r->ival);
    if (r->type == CFG_TYPE_TEXT)
        return cfg_set_text(db, r->scope, r->key, r->tval);
    return cfg_set_blob(db, r->scope, r->key, r->bval, r->blen);
}

/* db が NULL なら検証だけ。非 NULL なら (呼び手が begin 済みの txn の中で)
 * 対象行を書く。検証の順も内容も**両巡で同じ**。 */
static int imp_pass(const char *path, const char *scope, CfgDb *db,
                    int *rows_out, int *ver_out, CfgImportInfo *info)
{
    ImpIn in;
    int lineno = 0, len, rc, rows = 0, ver = 0;
    u32 off;

    *rows_out = 0;
    *ver_out = 0;
    imp_tab_reset();
    in.fd = -1;
    if (imp_open(&in, path) != 0) return CFG_IMPORT_E_OPEN;

    /* --- 1 行目: ヘッダ --- */
    rc = imp_read_line(&in, imp_line, &len, &off);
    if (rc != IMP_LINE) {
        imp_finish(&in);
        info->lineno = 1;
        if (rc == IMP_ERR_LONG) return CFG_IMPORT_E_LONG;
        if (rc == IMP_ERR_IO) return CFG_IMPORT_E_IO;
        return CFG_IMPORT_E_HEADER;                  /* 空ファイル */
    }
    lineno = 1;
    rc = cfg_json_header(imp_line, len, &ver);
    info->version = ver;
    if (rc == CFG_JSON_E_VERSION) {
        imp_finish(&in);
        info->lineno = 1;
        return CFG_IMPORT_E_VERSION;
    }
    if (rc != CFG_JSON_OK) {
        imp_finish(&in);
        info->lineno = 1;
        info->detail = rc;
        return CFG_IMPORT_E_HEADER;
    }

    /* --- 以降: 1 行 1 レコード --- */
    for (;;) {
        rc = imp_read_line(&in, imp_line, &len, &off);
        if (rc == IMP_EOF) break;
        if (rc != IMP_LINE) {
            imp_finish(&in);
            info->lineno = lineno + 1;
            return (rc == IMP_ERR_LONG) ? CFG_IMPORT_E_LONG : CFG_IMPORT_E_IO;
        }
        lineno++;
        /* 構文はすべての行に、意味 (上限 / 値域 / 名前の規則) は**対象行に
         * だけ**かける。混ぜると `--scope gshell` の import が、関係の無い
         * `user` 行の 256B text で落ちる (往復 1 の B3)。 */
        rc = cfg_json_record(imp_line, len, &imp_row);
        if (rc != CFG_JSON_OK) {
            imp_finish(&in);
            info->lineno = lineno;
            info->detail = rc;
            return CFG_IMPORT_E_LINE;
        }
        if (scope && (!cfg_json_scope_usable(&imp_row) ||
                      !imp_streq(imp_row.scope, scope))) continue;
        rc = cfg_json_check(&imp_row);
        if (rc != CFG_JSON_OK) {
            imp_finish(&in);
            info->lineno = lineno;
            info->detail = rc;
            return CFG_IMPORT_E_LINE;
        }
        if (rows >= CFG_IMPORT_MAX_ROWS) {
            imp_finish(&in);
            info->lineno = lineno;
            return CFG_IMPORT_E_MANY;
        }
        rc = imp_tab_add(&in, off, &imp_row);
        if (rc != 0) {
            imp_finish(&in);
            info->lineno = lineno;
            return (rc == 1) ? CFG_IMPORT_E_DUP : CFG_IMPORT_E_IO;
        }
        rows++;
        if (db && imp_apply(db, &imp_row) != 0) {
            imp_finish(&in);
            info->lineno = lineno;
            info->detail = cfg_last_sqlite(db);
            return CFG_IMPORT_E_WRITE;
        }
    }
    imp_finish(&in);
    *rows_out = rows;
    *ver_out = ver;
    return CFG_IMPORT_OK;
}

/* ------------------------------------------------------------------ */
/*  本体                                                               */
/* ------------------------------------------------------------------ */

/* 失敗の経路で使う close。**この呼び出しの中で**失敗したときだけ
 * `info->cleanup` に残す (前回の診断を拾わない)。rollback が成立して
 * いなければ「1 行も残らない」とは言えないので、必ず呼び手まで運ぶ。 */
static void imp_close(CfgDb *db, CfgImportInfo *info)
{
    int code;
    if (cfg_close(db) != 0) {
        code = cfg_last_close_error();
        info->cleanup = code ? code : -1;
    }
}

int cfg_import_file(const char *path, const char *scope, int merge,
                    CfgImportInfo *info)
{
    CfgImportInfo local;
    CfgDb *db;
    int rc, rows1 = 0, rows2 = 0, ver1 = 0, ver2 = 0;

    if (!info) info = &local;
    info->rows = 0;
    info->lineno = 0;
    info->detail = 0;
    info->status = CFG_OK;
    info->version = 0;
    info->cleanup = 0;

    if (!path || !path[0]) return CFG_IMPORT_E_ARG;
    if (scope && (!cfg_i_valid_scope(scope) ||
                  cfg_i_utf8_check(scope, imp_strlen(scope)) != 0))
        return CFG_IMPORT_E_ARG;

    /* --- 1 巡目: 検証だけ。ここで落ちれば DB は開いてすらいない --- */
    rc = imp_pass(path, scope, (CfgDb *)0, &rows1, &ver1, info);
    if (rc != CFG_IMPORT_OK) return rc;

    /* --- DB を開く --- */
    if (cfg_open(&db, 1) != 0) {
        info->detail = OS32_ERR_INVAL;
        return CFG_IMPORT_E_WRITE;
    }
    if (cfg_status(db) != CFG_OK) {
        info->status = cfg_status(db);
        imp_close(db, info);
        return CFG_IMPORT_E_STATUS;
    }
    if (cfg_begin(db) != 0) {
        info->detail = cfg_last_sqlite(db);
        imp_close(db, info);
        return CFG_IMPORT_E_WRITE;
    }
    /* 置換は対象 scope (NULL = 全部) を先に消す。--merge は消さない。 */
    if (!merge && cfg_delete_scope(db, scope) != 0) {
        info->detail = cfg_last_sqlite(db);
        imp_close(db, info);                 /* rollback して閉じる */
        return CFG_IMPORT_E_WRITE;
    }

    /* --- 2 巡目: 同じ検証をやり直しながら書く --- */
    rc = imp_pass(path, scope, db, &rows2, &ver2, info);
    if (rc == CFG_IMPORT_OK && (rows2 != rows1 || ver2 != ver1))
        rc = CFG_IMPORT_E_CHANGED;
    if (rc != CFG_IMPORT_OK) {
        imp_close(db, info);                 /* 未 commit は rollback */
        return rc;
    }
    if (cfg_commit(db) != 0) {
        info->detail = cfg_last_sqlite(db);
        imp_close(db, info);
        return CFG_IMPORT_E_WRITE;
    }
    info->rows = rows1;
    /* commit は通ったので **更新済み**。close の失敗は隠さず持ち上げる。 */
    if (cfg_close(db) != 0) {
        info->detail = cfg_last_close_error();
        return CFG_IMPORT_E_CLOSE;
    }
    return CFG_IMPORT_OK;
}

const char *cfg_import_detail_name(int detail)
{
    switch (detail) {
    case CFG_JSON_E_SYNTAX:  return "syntax";
    case CFG_JSON_E_SCOPE:   return "scope";
    case CFG_JSON_E_KEY:     return "key";
    case CFG_JSON_E_TYPE:    return "type";
    case CFG_JSON_E_VALUE:   return "value";
    case CFG_JSON_E_RANGE:   return "range";
    case CFG_JSON_E_UTF8:    return "utf8";
    case CFG_JSON_E_NUL:     return "nul";
    case CFG_JSON_E_VERSION: return "version";
    default:                 return "bad";
    }
}
