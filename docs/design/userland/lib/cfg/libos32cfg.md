# userland/lib/cfg/libos32cfg.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: S0_FOUNDATION・TASK_S0 の契約を今のソースと照らして要約
ソース: `userland/lib/cfg/libos32cfg.c`・`userland/lib/cfg/libos32cfg.h`

設定レジストリ (`/etc/settings.db`) のクライアント。C89 で静的リンクし、KAPI の `db_open_existing`・`db_prepare_only`・`db_bind_*`・`db_error_code` だけで DB に触る。
場所・版・上限の定数は `libos32cfg.h` が管理元 (`CFG_DB_PATH`・`CFG_DB_JOURNAL_PATH`・`CFG_SCHEMA_VERSION` など)。

## 設定レジストリの全体の約束

レジストリに関わる部品 (この library・`cfg` コマンド・初期値の生成・配備の保護・復旧) が共通に守る約束。各部品の設計書はこれを前提にする。

### DB を作るのは明示の操作だけ

- 実行時の初期化 (カーネルの SQLite の初期化、cfg の状態とメモリ上の既定値) は `/etc/settings.db` を作らない。create・DDL・seed・自動の版上げをしない。
- DB が無いときは空 DB を作らず、library の中の fallback で動く。読みは最小の既定値 (`cfg_get_int` の `def`)、書きは拒否。journal も作らない。
- DB を作るのは `cfg init` (`cfg_init`)、媒体の初期値 (ホストの `tools/mk_settings_db.py`)、インストーラの新規 seed と明示の復旧 (`install --recover-settings`) だけ。通常の配備は DB に触らない (`tools/deploy_protect.py`)。

### 開き方 (`cfg_open`)

1. 必ず RO・no-create (`db_open_existing(path, 0)`) で開き、schema を検査する。検査のための stat や `query_only` を RO の代わりにしない。
2. 結果は `cfg_status` に置き、`cfg_open` 自体は引数不正・二重 open 以外で 0 を返す (呼び手を分岐させない)。
   - `CFG_MISSING`: 無い (CANTOPEN かつ stat が NOTFOUND)。CANTOPEN だけでは「無い」と決めない。
   - `CFG_CORRUPT`: 0 バイト・NOTADB・CORRUPT・meta 表が無い・schema の形が違う・hot journal (`BUSY_RECOVERY`)。
   - `CFG_VERSION`: `schema_version` が自分より新しい。読みだけ。
   - `CFG_ERROR`: I/O・NOMEM・BUSY など。詳細は `cfg_last_sqlite`。
3. 壊れた DB・hot journal を自動で直さない。RW で開き直したり journal を消したりしない。元のファイルと journal は残す。
4. writable の要求でも、RW (no-create) へ切り替えるのは RO の検査が `CFG_OK` のときだけ。RW で開き直したら schema をもう一度検査し、版が RO の時と違えば失敗にする。
5. open は BEGIN しない。

### 書き (トランザクション)

- `cfg_begin` の明示の呼び出しだけが `BEGIN IMMEDIATE` を流す。BEGIN・set・COMMIT はそれぞれ単一 statement。
- set・delete・import はトランザクションの外では拒否 (`OS32_ERR_INVAL`)。
- トランザクションの中で set・delete が **どんな理由で** 断られても (入力の検証・前提の検査・SQL の失敗)、そのトランザクションは failed になる。failed の `cfg_commit` は拒否して rollback し `OS32_ERR_IO` を返す。半端な更新を commit しない。
- `cfg_close` は未 commit を rollback してから close する。rollback・close の失敗は成功にしない。最初の失敗を `cfg_last_close_error` に残し、後片付けの失敗で元の操作の診断 (`cfg_last_sqlite`) を上書きしない。

### 値の規則

| 項目 | 規則 |
|---|---|
| scope | `system`・`gshell`・`user`・`app:<名前>` (名前は `a-z 0-9 _`)、最大 63 byte |
| key | `a-z 0-9 _` の成分を `/` でつないだ小文字の階層名 (空の成分なし)、最大 63 byte |
| int | int32 |
| text | UTF-8、最大 255 byte |
| blob | 0〜4096 byte |

- 不正な UTF-8・埋め込み NUL・長さ超過は拒否する。規則は set・delete の側で強制し、読みは長さだけを見る (DB の中身を規則で切り捨てない)。
- NULL と空の text・blob を区別する。空値も非 NULL として bind する。「行はあるが値が NULL」は `cfg_get_info` の `CFG_INFO_NULL` で返す。
- 値は SQL に埋めず bind で渡す (引用や hex 化で回避しない)。
- 取り出しの cap が足りなければ `OS32_ERR_NOSPC` で、出力を書かない (部分更新しない)。
- `cfg_get_int` の `def` とは別に、障害は `cfg_status` (`CFG_ERROR`) に残す。障害を「無い」に丸めない。

### 列挙と再入

- `cfg_enum`・`cfg_enum_scopes` は行を private の配列に写し終えてから callback を呼ぶ。callback の中の `cfg_*` (読み・書き・close・入れ子の列挙) は `OS32_ERR_INVAL` で断り、書きを試みたトランザクションは failed になる。
- 上限 (`CFG_ENUM_MAX`・`CFG_SCOPES_MAX`) を超えたら callback を 1 度も呼ばず `OS32_ERR_NOSPC`。

### 呼び出してよい所と接続の寿命

- X4・IRQ・描画の callback から SQLite・VFS を呼ばない。SQLite は THREADSAFE=0 なので、呼び出しの途中に別の DB 操作を再入させない。
- open から close までを 1 回の実行・1 つのイベントの中で閉じ、間で yield しない。1 プロセス 1 接続 (`CfgDb` は静的 1 本)。
- KAPI の SHM に載った行・文字列は次の DB 操作で無効になる。すぐ写し、同時に持たない。

### 保証の上限

DELETE journal で rollback・I/O 失敗の伝播・通常の再 open の整合までを保証する。電源断を含む crash durability (全層の書き込み順序・装置の flush・切り替えの原子性) は保証しない。

## 組み立て

- KAPI の呼び出しは関数ポインタ表 `CfgBackend` (`cfg_internal.h`) に集める。ゲストの実体は `cfg_backend.c` (`kapi->db_*`)、ホスト試験は `cfg_backend_platform` を差し替えて実物の `kapi/kapi_db.c` へ向ける。library 本体に `#ifdef` の分岐は無い。
- `libos32cfg.a` は翻訳単位を分ける: `libos32cfg.c` (open・get・set・txn)・`cfg_enum.c`・`cfg_tsv.c`・`cfg_init.c`・`cfg_import.c`・`cfg_json.c`・`cfg_backend.c`。読むだけのアプリ (libos32gui の `cfgro.rs`、gshell の起動時の読み込み) が列挙・生成の作業領域 (静的配列) を引き込まないため。

## schema の検査

`cfg_i_schema_check` は `meta` 表を 1 本の SQL (`SQL_SCHEMA`) で見る: 行数・`typeof(schema_version)` の MIN と MAX・値の MIN と MAX・`schema_version BETWEEN 1 AND 2147483647` の MIN。

- 行がちょうど 1、型が全部 `integer`、MIN = MAX、範囲内のときだけ版を採る。0 行・2 行・`'abc'`・blob・実数・0・負・int32 を超える値・正常な行との混在は全部 `CFG_CORRUPT`。
- 版が `CFG_SCHEMA_VERSION` (1) なら `CFG_OK`、大きければ `CFG_VERSION`。
- 検査の SQL の失敗は `map_sql_failure` で写す: `SQLITE_ERROR` (meta 表が無い)・`NOTADB`・`CORRUPT`・`BUSY_RECOVERY` は `CFG_CORRUPT`、IOERR・NOMEM・通常の BUSY は `CFG_ERROR`。
- 版の正典は `meta.schema_version`。`PRAGMA user_version` は読まない (カーネルの SQLite は `SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS` で無効)。

## API の戻り値

### 接続

- `cfg_open`: 負は `OS32_ERR_INVAL` (引数不正・二重 open) だけ。`cfg_close` の後は再び開ける。状態は `cfg_status`・`cfg_schema_version`・`cfg_last_sqlite`。
- `CFG_VERSION` は RO 接続を持ったままにする (読める、書きは拒否)。`CFG_CORRUPT`・`CFG_ERROR` は接続を捨てる。
- `cfg_close`: 0 か `OS32_ERR_IO`。open の途中で捨てた接続の close 失敗もここで報告する。close 失敗のコードは `cfg_last_close_error` (close 後も読める)。

### 読み

- 使えるのは `CFG_OK` か `CFG_VERSION` のときだけ。`CFG_MISSING`・`CFG_CORRUPT` では `OS32_ERR_NOTFOUND`、`CFG_ERROR` では `OS32_ERR_IO`。
- `cfg_get_info`: 宣言型 (下位 4 bit) + 値が NULL なら `CFG_INFO_NULL`。行が無ければ `NOTFOUND`、契約外の行 (type が 0/1/2 でない・int が int32 の外・値の型が宣言と違う) は `OS32_ERR_NOSYS`。int32 の範囲と型は SHM で 32 bit に縮む前に SQL の側 (`SQL_GET`) で判定する。
- `cfg_read_int`: 0 で `*out` に値、負は上と同じ。`cfg_get_int` はその上の 1 行の包みで、失敗も未設定も `def` を返す (障害は `cfg_status` に残る)。
- `cfg_get_text`・`cfg_get_blob`: 長さを返す。求めた型と宣言型が違う・値が NULL は `NOTFOUND`、空値は長さ 0、cap 不足 (text は NUL の 1 byte を含む) は `NOSPC` で出力を書かない。保存値が上限を超える・text に埋め込み NUL や不正 UTF-8 があれば `NOSYS` (書く前に断る)。値は SHM から呼び手のバッファへすぐ写す (後で別の接続が SHM を上書きしても変わらない)。
- 読みの scope・key は長さ (1〜63 byte) だけを見る。

### 書き

- `cfg_begin` は writable で `CFG_OK`、トランザクションが無いときだけ。それ以外は `OS32_ERR_INVAL`、BEGIN の失敗は `OS32_ERR_IO`。
- `cfg_set_*`・`cfg_delete`・`cfg_delete_scope`・`cfg_set_null`: 書けない状態・トランザクション外・規則違反は `OS32_ERR_INVAL`、SQL の失敗は `OS32_ERR_IO`。どちらもトランザクションを failed にする (failed の後の set は `INVAL`)。
- `cfg_commit`: failed か接続の状態が `CFG_OK` でなくなっていれば rollback して `OS32_ERR_IO`。COMMIT の失敗も rollback して `OS32_ERR_IO`、COMMIT 自身のコードを `cfg_last_sqlite` に残す (成功した ROLLBACK で 0 に戻さない)。
- `cfg_rollback`: トランザクションが無ければ `INVAL`。

### 列挙

- `cfg_enum(db, scope, prefix, fn, ctx)`: prefix は `substr(key,1,len) = prefix` の前方一致 (LIKE ではないので `_` はワイルドカードにならない)。`ORDER BY key`。`cfg_enum_scopes` は `DISTINCT` で昇順。
- callback が非 0 を返したら中断し、それまでに呼んだ件数を返す。
- `CFG_MISSING`・`CFG_CORRUPT` では 0 件、`CFG_ERROR` は `OS32_ERR_IO` (0 件に丸めない)。type 列が契約外の行があれば `OS32_ERR_NOSYS` で列挙ごと断る。名前に埋め込み NUL・不正 UTF-8・長さ超過があれば障害 (`IO`)。

## 検査

`tools/tests/test_cfg.py` (本体 `tools/tests/cfg_host.c`): 実物の library・`userland/cmds/cfg.c`・`kapi/kapi_db.c`・SQLite・RAM の VFS を組み、`missing`・`corrupt`・`version`・`types`・`roundtrip`・`txn`・`limits`・`enum`・`meta`・`shm_copy`・`init`・`rename`・`tsv`・`args`・`cmd`・`list_big` などを見る。`--target` で i386-elf の `-Werror`、`--sanitize` で ASan と符号付き桁あふれ。
