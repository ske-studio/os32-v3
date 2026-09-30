/* ======================================================================== */
/*  KAPI_DB.H — SQLite DB KAPI ラッパー ヘッダ                               */
/*                                                                          */
/*  外部プログラムが KAPI テーブル経由で呼ぶ DB 操作関数のプロトタイプ。       */
/*  カーネル内に DB 接続スロット (最大 DB_MAX_CONNECTIONS) を管理する。        */
/*                                                                          */
/*  制約:                                                                   */
/*    - SQL 文字列は最大 1024 バイト (SQL_COPY_BUF_SIZE)。超過分は切り捨て。 */
/* ======================================================================== */

#ifndef KAPI_DB_H
#define KAPI_DB_H

#include "os32_kapi_shared.h"

/* DB オープン — パスからファイル DB (メモリDB は ":memory:") を開く */
/* 戻り値: ハンドル (0以上) / -1=失敗 */
int __cdecl kapi_db_open(const char *path);

/* DB クローズ — finalize -> 必要なら rollback -> close。
 * 戻り値: 0=全段成功, -1=失敗。close 成功時だけ slot を解放する。
 * close 失敗接続は再操作/再回収/再利用せず隔離 (FD 保護は F2 の前提作業)。 */
int __cdecl kapi_db_close(int handle);

/* SQL 実行 (結果不要の DDL/DML) — 共有メモリにステータスを書き込む */
/* 戻り値: 0=成功, 負数=エラー */
int __cdecl kapi_db_exec(int handle, const char *sql);

/* ステートメント準備 — SELECT 等のクエリを解析 */
/* 共有メモリに最初の行またはステータスを書き込む */
/* 戻り値: 0=DONE, 1=ROW, 負数=エラー */
int __cdecl kapi_db_prepare(int handle, const char *sql);

/* 次の行を取得 — 共有メモリを更新 */
/* 戻り値: 0=DONE (完了), 1=ROW (行あり), 負数=エラー */
int __cdecl kapi_db_step(int handle);

/* カラム値取得 (整数) — 現在の行から col 番目の値を返す */
int __cdecl kapi_db_column_int(int handle, int col);

/* カラム値取得 (文字列) — 共有メモリのデータ領域へのポインタを返す */
const char * __cdecl kapi_db_column_text(int handle, int col);

/* ステートメント手動 finalize — 結果セット途中放棄時に使用 */
/* db_step(DONE) 時は自動 finalize されるため、通常は不要 */
/* 戻り値: 0=成功, -1=失敗 */
int __cdecl kapi_db_finalize(int handle);

/* 通常は sqlite3_errmsg()。open/close 回収中の最初の失敗はコピーを保持。
 * close 後も slot 再利用までは診断を取得できる (世代付き handle ではない)。
 * open 失敗の即時診断は SHM。owner 別 handle=-1 診断 ABI は F4 の範囲。 */
const char * __cdecl kapi_db_last_error(int handle);

/* MEMSYS5 メモリ使用量取得 (デバッグ・モニタリング用) */
/* 戻り値: sqlite3_memory_used() の値 (バイト単位) */
u32 __cdecl kapi_db_mem_used(void);

/* ======================================================================== */
/*  v50 (票 S0-K §1a) — 設定レジストリの基盤 7 本                            */
/*                                                                          */
/*  既存 10 本の順序・型・挙動は 1 つも動かさない ([ABI2])。新 handle にも     */
/*  db_step / db_finalize / db_close をそのまま使う (reset API は足さない)。  */
/* ======================================================================== */

/* 「直前の open 失敗」を呼び手 (owner) ごとに 1 欄だけ保持する。owner は
 * exec/appslot.h の ID の池 (0 = カーネル / 1 = シェル帯 / 2..APP_ID_MAX = 5)。
 * APP_SLOT_COUNT 以上であることは kernel/kselftest.c の v50 の項が見る。 */
#define DB_OWNER_SLOTS    6

/* 既存 DB を開く (CREATE / URI を付けない)。
 *   writable: 0 = SQLITE_OPEN_READONLY / 1 = SQLITE_OPEN_READWRITE。他は拒否。
 * open の **前**に本体の size > 0 と `<path>-journal` の不在を vfs_stat で
 * 検査し、反すれば SQLite を呼ばずに失敗する (RO でも hot journal の
 * 後始末を走らせないため)。失敗の拡張コードは owner 別の欄に残り
 * db_error_code(-1) で読める。戻り値: handle >= 0 / -1 = 失敗。 */
int __cdecl kapi_db_open_existing(const char *path, int writable);

/* step しない prepare。SQL は NUL 込み DB_SQL_MAX_BYTES 以内 (超過は
 * 切り捨てず拒否)、単一の非空 statement のみ (末尾の空白 / コメントは可)。
 * 同じ handle の旧 stmt は finalize して置換する。0 = 成功 / -1 = 失敗。 */
int __cdecl kapi_db_prepare_only(int handle, const char *sql);

/* bind は prepare_only の後・最初の step の前だけ。index は 1-based。
 * text / blob はカーネルへ検証付きコピーしてから SQLITE_TRANSIENT で渡す
 * (SQLite に呼び手のポインタを保持させない)。0 = 成功 / -1 = 失敗。 */
int __cdecl kapi_db_bind_int(int handle, int index, int value);
int __cdecl kapi_db_bind_text(int handle, int index, const char *text, int length);
int __cdecl kapi_db_bind_blob(int handle, int index, const void *data, int length);
int __cdecl kapi_db_bind_null(int handle, int index);

/* slot の「最後の失敗」コード (SQLite 拡張 result code) を返す診断専用の口。
 * データ操作 (open_existing / prepare_only / bind_* / step / exec) は成功で 0 に、
 * 失敗でそのコードに更新する。finalize / close は**失敗したときだけ**更新する
 * (後片付けが原因診断を消さない)。close 後も slot が再利用されるまで同じ値を
 * 返す。範囲外 handle / 一度も開かれていない slot は SQLITE_MISUSE。
 * handle = -1 は呼び手 owner の「直前の open 失敗」。取得しても消えない。 */
int __cdecl kapi_db_error_code(int handle);

/* KAPI 表の slot 配置が v50 / v51 の形を保っているか。
 *   slot_count … KAPI 表の要素数 (実運用では KAPI_SLOT_COUNT)
 *   戻り値 1 = 正しい / 0 = 崩れている
 *
 * **末尾に何本足されても真**であること ([ABI2] は末尾追記を正当と定めている)。
 * 引数で受けるのは、ホスト試験から「KAPI をもう 1 本足したら」を直に
 * 試せるようにするため — 以前ここが `KAPI_SLOT_COUNT != <末尾> + 1` の
 * 決め打ちで、KAPI を足すたびに必ず落ちていた (2026-09-15、票 H3)。 */
int db_slot_layout_ok(int slot_count);

/* v50 の自己診断 (kernel/kselftest.c から。ホスト試験と同じ判定を踏む)。
 * ビット 0..n が落ちた項目 (0 = 全部通った)。 */
u32 db_v50_selftest(void);

/* 既存 exec_exit 用の全 active slot 回収を F2 まで維持。隔離 slot は除外。 */
void db_cleanup_all(void);

/* Internal only: not a public KAPI entry; exec integration belongs to F2. */
void db_cleanup_owned(int owner);

#endif /* KAPI_DB_H */
