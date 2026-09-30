/* ======================================================================== */
/*  CFG_INTERNAL.H — libos32cfg の内部境界 (SDK には配らない)                */
/*                                                                          */
/*  KAPI 呼び出しは 1 枚の関数ポインタ表 (CfgBackend) に集める。ホスト TDD は */
/*  ここを実 kapi_db.c + RAM backend に差し替えて、libos32cfg.c を**その      */
/*  まま**載せる (票 §4)。ゲストでは cfg_backend.c が kapi から作る。         */
/* ======================================================================== */

#ifndef CFG_INTERNAL_H
#define CFG_INTERNAL_H

#include "libos32cfg.h"

/* ---- SQLite のコード (libos32cfg が写像に使う分だけ、[C4]) -------------- */
#define CFG_SQLITE_OK             0
#define CFG_SQLITE_ERROR          1    /* no such table 等 */
#define CFG_SQLITE_BUSY           5
#define CFG_SQLITE_CORRUPT       11
#define CFG_SQLITE_CANTOPEN      14
#define CFG_SQLITE_CONSTRAINT    19    /* PRIMARY KEY 重複など */
#define CFG_SQLITE_NOTADB        26
#define CFG_SQLITE_BUSY_RECOVERY (5 | (1 << 8))   /* hot journal */
#define CFG_SQLITE_PRIMARY(c)    ((c) & 0xFF)

/* ---- KAPI 境界 --------------------------------------------------------- */
typedef struct CfgBackend {
    /* v50 */
    int (*db_open_existing)(const char *path, int writable);
    int (*db_prepare_only)(int handle, const char *sql);
    int (*db_bind_int)(int handle, int index, int value);
    int (*db_bind_text)(int handle, int index, const char *text, int length);
    int (*db_bind_blob)(int handle, int index, const void *data, int length);
    int (*db_bind_null)(int handle, int index);
    int (*db_error_code)(int handle);
    /* v42 */
    int (*db_open)(const char *path);          /* CREATE 付き (cfg_init) */
    int (*db_close)(int handle);
    int (*db_exec)(int handle, const char *sql);
    int (*db_step)(int handle);
    int (*db_finalize)(int handle);
    /* 結果ブロック (DB_ResultHeader + DB_ColumnInfo[] + データ) の先頭 */
    unsigned char *(*shm)(void);
    /* VFS */
    int (*sys_stat)(const char *path, OS32_Stat *st);
    int (*sys_rename)(const char *oldpath, const char *newpath);
    int (*sys_unlink)(const char *path);
    int (*sys_open)(const char *path, int mode);
    int (*sys_read)(int fd, void *buf, u32 size);
    /* import の重複検出で先行行を読み直す (票 S3 §2)。 */
    int (*sys_lseek)(int fd, int offset, int whence);
    void (*sys_close)(int fd);
    u32 (*get_tick)(void);
} CfgBackend;

/* ゲスト側の実体は cfg_backend.c。ホスト TDD は自前の定義で置き換える。 */
const CfgBackend *cfg_backend_platform(void);
/* 試験・特殊用途の差し替え (NULL で既定へ戻す)。 */
void cfg_set_backend(const CfgBackend *b);
const CfgBackend *cfg_backend(void);

/* ---- CfgDb の中身 ------------------------------------------------------ */
struct CfgDb {
    int in_use;
    int handle;          /* KAPI のスロット。-1 = 掴んでいない */
    int want_write;      /* cfg_open(writable) の要求 */
    int rw;              /* 実際に RW で開けたか */
    int status;          /* CFG_* */
    int schema_version;  /* 読めた版 (MISSING / CORRUPT は 0) */
    int last_sqlite;     /* 直前の SQLite 拡張コード */
    int txn;             /* 0 = 無し / 1 = 実行中 / 2 = failed */
    int in_enum;         /* 再入フラグ */
    int orphan_close;    /* 内部で捨てた接続の close 失敗 (0 = 無し) */
    int cleanup_sqlite;  /* 後片付け (ROLLBACK) の失敗。操作失敗とは別に持つ */
};

/* ---- 内部共有 (libos32cfg.c が実装) ------------------------------------ */
int  cfg_i_valid_scope(const char *s);        /* 1 = 規則どおり */
int  cfg_i_valid_key(const char *s);
int  cfg_i_len_ok(const char *s, int max);    /* 1..max バイトの C 文字列 */
int  cfg_i_utf8_check(const void *p, int n);  /* 0 = 妥当 / -1 */

/* 増分 UTF-8 検査 (tsv の長いコメントを溜めずに見るため) */
typedef struct { int need; u32 cp; u32 lo; } CfgU8;
void cfg_i_u8_reset(CfgU8 *s);
int  cfg_i_u8_byte(CfgU8 *s, int b);          /* 0 = ここまで妥当 / -1 */
int  cfg_i_u8_done(const CfgU8 *s);           /* 1 = 途中の列が無い */

/* SHM の列アクセサ (libos32db と同じ読み方) */
int          cfg_i_col_count(void);
int          cfg_i_col_type(int col);
int          cfg_i_col_len(int col);
i32          cfg_i_col_int(int col);
const void  *cfg_i_col_ptr(int col);

/* 単一 statement の実行 (BEGIN / COMMIT / ROLLBACK / DDL)。0 / -1。 */
int  cfg_i_exec(CfgDb *db, const char *sql);
/* prepare_only だけ (bind と step は呼び手)。0 / -1。 */
int  cfg_i_prepare(CfgDb *db, const char *sql);
void cfg_i_note(CfgDb *db);                   /* last_sqlite を取り込む */
/* RO / RW どちらの接続でも同じ schema 検査 (票 §1-1b)。CFG_* を返す。 */
int  cfg_i_schema_check(CfgDb *db, int *version_out);
/* cfg_init から close 失敗コードを共有欄に残す。 */
void cfg_i_set_close_error(int code);

/* ---- tsv reader (cfg_tsv.c、純関数) ------------------------------------ */
#define CFG_TSV_OK        0
#define CFG_TSV_E_CR      1
#define CFG_TSV_E_UTF8    2
#define CFG_TSV_E_COLS    3
#define CFG_TSV_E_SCOPE   4
#define CFG_TSV_E_KEY     5
#define CFG_TSV_E_TYPE    6
#define CFG_TSV_E_VALUE   7
#define CFG_TSV_E_RANGE   8
#define CFG_TSV_E_NUL     9
#define CFG_TSV_E_IO      12
#define CFG_TSV_E_EMIT    13

/* 重複 (scope, key) はここでは見ない。`cfg_init` が流し込む先の
 * `settings` 表が PRIMARY KEY (scope, key) なので、素の INSERT が
 * SQLITE_CONSTRAINT で弾く — 控えを持たないので行数にも名前の長さにも
 * 上限が要らない (往復 1 の ⑯)。 */

typedef struct {
    char          scope[CFG_SCOPE_MAX + 1];
    char          key[CFG_KEY_MAX + 1];
    int           type;                       /* CFG_TYPE_* */
    int           ival;
    char          tval[CFG_TEXT_MAX + 1];
    int           tlen;
    unsigned char bval[CFG_BLOB_MAX];
    int           blen;
    int           lineno;
} CfgTsvRow;

typedef struct { int code; int lineno; } CfgTsvErr;

/* get は 1 バイト (0..255) か EOF の -1、読み取り障害は -2 を返す。
 * row は呼び手が用意する作業領域。emit が非 0 を返したら中断。
 * 0 = 全行受理 / -1 = 規則違反 (err に理由と行番号)。 */
int cfg_tsv_parse(int (*get)(void *ctx), void *gctx, CfgTsvRow *row,
                  int (*emit)(const CfgTsvRow *r, void *ctx), void *ectx,
                  CfgTsvErr *err);


/* ---- JSON reader (cfg_json.c、純関数、票 S3 §2) ------------------------ */
/*  `cfg export` が書く形**だけ**を読む。汎用 JSON ではない (DESIGN §6b):
 *    1 行目  {"schema_version":N,"exported":"..."}
 *    以降    {"scope":"…","key":"…","type":0|1|2,"v":<int>|"<text>"|"<b64>"|null}
 *  キー順は固定、空白は許さない、エスケープは writer が出す 6 種だけ
 *  (`\"` `\\` `\n` `\r` `\t` `\uXXXX`。`\u` は BMP のみでサロゲートは拒否)。
 *  数値は int32 の正準形 (先頭ゼロ・`+`・`-0` は拒否)。                     */
#define CFG_JSON_OK        0
#define CFG_JSON_E_SYNTAX  1    /* 骨組みが違う / 余分な文字 */
#define CFG_JSON_E_SCOPE   2
#define CFG_JSON_E_KEY     3
#define CFG_JSON_E_TYPE    4
#define CFG_JSON_E_VALUE   5    /* 値の形 / 上限超過 / base64 */
#define CFG_JSON_E_RANGE   6    /* int32 の範囲外 */
#define CFG_JSON_E_UTF8    7    /* 不正 UTF-8 / `\u` のサロゲート */
#define CFG_JSON_E_NUL     8    /* 文字列に NUL (`\u0000`) */
#define CFG_JSON_E_VERSION 9    /* ヘッダの版が自分より新しい */

/* 行バッファ。writer の最長行は blob 4096B の 5,627B (+ LF + NUL = 5,629B)。*/
#define CFG_JSON_LINE_MAX  6144

typedef struct {
    char          scope[CFG_SCOPE_MAX + 1];
    int           slen;
    char          key[CFG_KEY_MAX + 1];
    int           klen;
    int           type;                       /* CFG_TYPE_* (宣言型) */
    int           is_null;                    /* 1 = "v":null */
    int           ival;
    char          tval[CFG_TEXT_MAX + 1];
    int           tlen;
    unsigned char bval[CFG_BLOB_MAX];
    int           blen;
    /* **意味**の傷 (構文解析は通ったが上限 / 値域を外れた)。cfg_json_check
     * が読む。対象外の scope の行はここを見ない = 構文検証だけになる。 */
    int           scope_over;                 /* 63B を超えた (中身は切れている) */
    int           scope_nul;
    int           key_over;
    int           key_nul;
    int           val_over;                   /* text 255B / blob 4096B 超過 */
    int           val_nul;
    int           val_range;                  /* int32 の範囲外 */
    int           val_b64;                    /* base64 の非正準形 (未使用ビット) */
} CfgJsonRow;

/* line は長さ付き (埋め込み NUL も行の一部として見る)。CFG_JSON_* を返す。
 *
 * `cfg_json_record` は **構文だけ** を見る (骨組み・キーの順・エスケープ・
 * base64 の形・数字の並び)。上限や値域は `row` の傷として控えるだけで、
 * 拒否はしない — `--scope` の対象外の行を「構文検証だけ」で通すため (票 §2)。
 * `cfg_json_check` が**対象行にだけ**かける意味の検証 (名前の規則と長さ、
 * UTF-8、NUL、text / blob の上限、int32 の範囲)。
 * `cfg_json_scope_usable` は scope を丸ごと復号できた (= 対象かどうかを
 * 文字列で判定してよい) かどうか。切り詰めた scope を比べると、63B の
 * 対象名に前半が一致する長い scope を取り違える。 */
int cfg_json_header(const char *line, int len, int *version_out);
int cfg_json_record(const char *line, int len, CfgJsonRow *row);
int cfg_json_check(const CfgJsonRow *row);
int cfg_json_scope_usable(const CfgJsonRow *row);

#endif /* CFG_INTERNAL_H */
