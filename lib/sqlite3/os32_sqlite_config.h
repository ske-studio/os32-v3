/* ======================================================================== */
/*  OS32_SQLITE_CONFIG.H — OS32 用 SQLite コンパイル設定                       */
/*                                                                          */
/*  sqlite3.c をコンパイルする際に、このヘッダを -include で指定する。         */
/*  すべての SQLITE_OMIT / SQLITE_MAX / SQLITE_DEFAULT マクロをここに集約。   */
/*                                                                          */
/*  ビルドオプション:                                                        */
/*    - 最適化: -Os (サイズ最適化必須。-O2比で30%削減)                        */
/*    - amalgamation: OMITマクロ付きでソースから再生成したもの                */
/*    - 配置先: カーネル拡張域 0x200000-0x2FFFFF (1MB)                       */
/* ======================================================================== */
#ifndef OS32_SQLITE_CONFIG_H
#define OS32_SQLITE_CONFIG_H

/* ======== 必須設定 ======== */
#define SQLITE_THREADSAFE          0     /* シングルタスク OS: ミューテックス不要 */
#define SQLITE_OS_OTHER            1     /* カスタム VFS のみ使用 */
#define SQLITE_TEMP_STORE          3     /* 一時ファイルは常にメモリ上 */
#define SQLITE_DISABLE_LFS         1     /* 64bit ファイルオフセット無効 */
#define SQLITE_DQS                 0     /* ダブルクォート文字列禁止 */

/* ======== 機能削減 (OMIT) ======== */
#define SQLITE_OMIT_LOAD_EXTENSION 1     /* 動的ライブラリロード */
#define SQLITE_OMIT_WAL            1     /* WAL ジャーナルモード */
#define SQLITE_OMIT_SHARED_CACHE   1     /* 共有キャッシュ */
#define SQLITE_OMIT_AUTOINIT       1     /* 自動初期化 (手動 sqlite3_initialize 必要) */
#define SQLITE_OMIT_DEPRECATED     1     /* 非推奨 API */
#define SQLITE_OMIT_VIRTUALTABLE   1     /* 仮想テーブル */
#define SQLITE_OMIT_TRIGGER        1     /* トリガー */
#define SQLITE_OMIT_AUTHORIZATION  1     /* 認可コールバック */
#define SQLITE_OMIT_PROGRESS_CALLBACK 1  /* 実行中断コールバック */
#define SQLITE_OMIT_TRACE          1     /* SQL トレース */
#define SQLITE_OMIT_EXPLAIN        1     /* EXPLAIN 文 */
#define SQLITE_OMIT_TCL_VARIABLE   1     /* Tcl 変数参照 */
#define SQLITE_OMIT_COMPLETE       1     /* sqlite3_complete() */
#define SQLITE_OMIT_DECLTYPE       1     /* sqlite3_column_decltype() */
#define SQLITE_OMIT_JSON           1     /* JSON 関数群 */

/* ======== OMIT 付き amalgamation 再生成で対応済み ======== */
#define SQLITE_OMIT_CTE            1     /* WITH 句 (再帰 CTE) */
#define SQLITE_OMIT_WINDOWFUNC     1     /* ウィンドウ関数 */

/* ======== 追加 OMIT (機能影響が小さいもの) ======== */
#define SQLITE_OMIT_ALTERTABLE     1     /* ALTER TABLE (スキーマ変更は DROP+CREATE で代替) */
#define SQLITE_OMIT_ANALYZE        1     /* ANALYZE (統計情報収集 — 組み込み環境では不要) */
#define SQLITE_OMIT_ATTACH         1     /* ATTACH DATABASE (MAX_ATTACHED=0 と連動) */
#define SQLITE_OMIT_AUTOINCREMENT  1     /* AUTOINCREMENT (通常の rowid 自動採番で十分) */
#define SQLITE_OMIT_CAST           1     /* CAST 式 */
#define SQLITE_OMIT_FOREIGN_KEY    1     /* 外部キー制約 */
#define SQLITE_OMIT_GET_TABLE      1     /* sqlite3_get_table (非推奨 API) */
#define SQLITE_OMIT_INTEGRITY_CHECK 1    /* PRAGMA integrity_check */
#define SQLITE_OMIT_LOCALTIME      1     /* localtime 変換 (RTC はローカル時刻) */
#define SQLITE_OMIT_LOOKASIDE      1     /* Lookaside メモリアロケータ */
#define SQLITE_OMIT_REINDEX        1     /* REINDEX */
#define SQLITE_OMIT_SCHEMA_PRAGMAS 1     /* PRAGMA table_info 等 */
#define SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS 1  /* PRAGMA schema_version */
#define SQLITE_OMIT_UTF16          1     /* UTF-16 サポート (OS32 は UTF-8) */
#define SQLITE_OMIT_XFER_OPT      1     /* INSERT INTO ... SELECT 最適化 */
/* SQLITE_OMIT_FLOATING_POINT は sqlite3AtoF のバグにより使用不可。
 * 代わりに -msoft-float でソフトウェア浮動小数点を使用する */

/* ======== メモリ管理 ======== */
#define SQLITE_ENABLE_MEMSYS5      1     /* 固定プールメモリアロケータ */

/* ======== リソース制限 ======== */
#define SQLITE_DEFAULT_CACHE_SIZE  10    /* ページキャッシュ 10ページ (~10KB) */
#define SQLITE_DEFAULT_PAGE_SIZE   1024  /* ページサイズ 1KB */
#define SQLITE_MAX_COLUMN          100
#define SQLITE_MAX_SQL_LENGTH      10000
#define SQLITE_MAX_EXPR_DEPTH      100
#define SQLITE_MAX_COMPOUND_SELECT 5
#define SQLITE_MAX_VARIABLE_NUMBER 100
#define SQLITE_MAX_ATTACHED        0     /* ATTACH DATABASE 無効 */

#endif /* OS32_SQLITE_CONFIG_H */
