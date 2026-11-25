# sys/kern/ime/ime_store.h — 辞書の接続の内部契約 (core と FEP 部品の共有)

> 状態: **現行 (2026-11-18)** — 出所: DICT_META_DESIGN・TASK_DICT_META (要約して移した)
ソース: `sys/kern/ime/ime_store.h`・`sys/kern/ime/ime_conn.c`

## 何をするか

かな漢字変換 (FEP) が使う 2 つの DB 接続 — システム辞書 (読み取り専用) と学習 DB (読み書き) — の型・定数・誤りの意味を 1 か所に決める。
カーネル本体 (`sys/kern/sqlite_engine.c`、以下 core) と FEP 部品 (`fep.mod`) の両方がこのヘッダを読む。
KAPI には出さない。SQLite のポインタも接続も、アプリへは渡らない。
GNU11・libc なしで組める形にしてある (host 試験も libc なしで組む)。

## 定数

| 名前 | 値 | 意味 |
|---|---|---|
| `FEP_DICT_READER_VERSION` | 1 | FEP の辞書を読む機能の版。辞書の `min_fep_version` と比べる。KAPI の版とは別 |
| `FEP_DICT_FORMAT_VERSION` | 1 | 今の辞書の形式の版 (システム辞書のヘッダの user_version、両 DB の meta の `format_version`) |
| `FEP_DICT_POS_VERSION` | 1 | 品詞の対応表の版。学習 DB と辞書で一致が要る |
| `FEP_DICT_PAGE_SIZE` / `FEP_DICT_HEADER_SIZE` | 1024 / 100 | 両 DB の page_size。ヘッダを SQL より先に読む byte 数 |
| `FEP_DICT_CACHE_PAGES` / `FEP_USER_CACHE_PAGES` | 16 / 8 | 旧形式のシステム辞書の既定の cache / 学習接続の cache |
| `FEP_DICT_RESERVE_KB` / `FEP_DICT_MIN_RESERVE_KB` | 192 / 99 | `OS32_SQLITE_FEP_RESERVE`・`OS32_SQLITE_FEP_RESERVE_MIN` (`lib/sqlite3/os32_sqlite_vfs.h`) を KiB にした物 |
| `FEP_DICT_ID` / `FEP_DICT_COST_SCALE` | `ipadic-2.7.0` / `os32-log2047-boost-v1` | 旧形式の既定値。形式 1 の meta の照合にも使う |
| `FEP_DICT_PATH_S` / `_M` / `_L` | `/db/fep_s.db` / `IME_DICT_PATH` (`/db/fep.db`) / `/db/fep_l.db` | 固定の 3 つの辞書のパス |
| `FEP_USER_PATH` / `FEP_USER_DIR` / `FEP_USER_JOURNAL_SUFFIX` | `/etc/fep_user.db` / `/etc/` / `-journal` | 学習 DB の既定と、置いてよいディレクトリ |
| `FEP_SETTING_RESERVE` / `FEP_SETTING_USER_PATH` | `fep_mem_reserve_kb` / `fep_user_dict_path` | settings 表 (scope=`system`) のキー |

## 型と不変条件

- `FepMeta` — 辞書のメタ情報のうち、常駐して使う数値と短い文字列だけの写し (形式・`min_reader`・品詞の版・予約 KiB・cache・語数・`dict_id`・variant)。
  license などの長い原文は写さない。読むたびに SQL で引く (`ime_dict_info.c`)。
- `FepConn` — 接続 1 本。`db`、準備済みの文 `exact`・`prefix`・`learn`、常駐の group、パス、`isolated`、`io_retried`。
- `FepStore` — `system`・`user` の 2 接続、使用中の辞書の `meta`、起動時に決まった予約 `boot_reserve_kb`、学習の可否 `user_enabled`。
  `g_ime.store` が値で 1 つだけ持つ (`ime_current_store()`)。
- 初期化は `{0}` だけ。`isolated` になった接続は 0 に戻さず、group も再利用しない (再起動まで隔離)。
- 閉じる順は `group_begin_close` → 全部の文を finalize → `sqlite3_close` → `group_finish` (`fep_conn_close`)。
  どこかで失敗したら `isolated=1` で `OS32_ERR_BUSY`。finalize の戻り値は最後の評価の結果なので、破棄の成否に使わない。
- `fep_conn_ready` は SQL を一切流さずに、`INVAL` (NULL)・`BUSY` (隔離)・`AGAIN` (未接続)・`IO` (エンジンや FD が使えない)・`STALE` (FD が失効) の順に判定する。開き直しは呼び手 (`ime_dict.c`) の仕事。
- この口のどれも、途中で yield せず、IME へコールバックしない (シングルタスク・協調切替の前提)。

## 誤りの番号

新しい store の口は 0 か負の `OS32_ERR_*` を返し、SQLite の rc を外へ出さない。

| 値 | 意味 |
|---|---|
| `INVAL` | 引数・ヘッダ・meta の不一致 (版・ID・品詞・パスの範囲) |
| `IO` | SQL・読み取りの失敗、作りかけの学習 DB |
| `NOTFOUND` | ファイルやキーが無い |
| `STALE` | FD が失効した |
| `BUSY` | 隔離された接続、閉じる途中の失敗、常駐の group の空きが無い |
| `ROFS` | 学習 DB に書けない |
| `AGAIN` | 切替先の辞書が起動時の予約より大きい (再起動が要る)。core の起動の値がまだ無い |

`ime_user_*` の KAPI が返す、操作ごとの既存の負値 (`IME_DICT_ERR_*`、`-1`) はそのまま。

## 口の持ち主

| 口 | 実体 | 境目 |
|---|---|---|
| `fep_meta_probe`・`fep_boot_config_get`・`fep_boot_user_path_get` | `sys/kern/sqlite_engine.c` (core) | FEP は `tools/prototypes/fep_imports.def` の import で呼ぶ。生成した表は手で直さない |
| `fep_store_open`・`fep_store_switch`・`fep_store_close`・`fep_store_live_connections`・`fep_store_search`・`fep_store_learn`・`fep_store_user_*` | `sys/kern/ime/ime_dict.c` | 検索の合成と、接続ごとの回復 |
| `fep_user_open` | `sys/kern/ime/ime_user_store.c` | 学習 DB の検査と作成 |
| `fep_conn_close`・`fep_conn_ready` | `sys/kern/ime/ime_conn.c` | 両接続が共有する寿命 |
| `fep_store_info` | `sys/kern/ime/ime_dict_info.c` | KAPI `ime_dict_info` の本体 |
| `hsp_set_fep_user_path` | `userland/system/hsync_protect.inc` | 宣言だけここに置く (契約の試験用)。hsync はこのヘッダを読まない |

## 検査

- `fep-store-contracts` (`tools/tests/test_fep_store_contracts.py`): 宣言が libc なしで組めること、試験の土台 (`tools/tests/fep_store_fixture.h`)。
- `fep-conn` (`tools/tests/test_fep_conn.py`): 閉じる順、失敗で隔離すること、`fep_conn_ready` の判定順。
