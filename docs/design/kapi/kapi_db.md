# sys/kapi/kapi_db.c — SQLite の KAPI と接続管理

> 状態: **現行 (2026-10-31)**

SQLite 接続とステートメントをハンドルとして公開する。
結果と診断文字列を共有領域に置き、所有者ごとに回収する。

## 落とし穴

- DB の失敗は戻り値だけではプール枯渇と他の失敗を区別しにくい。
  `db_last_error` の診断を確認し、不要になった接続やステートメントを閉じる
  (`sys/kapi/kapi_db.c` の `shm_write_row()`, `sys/kapi/kapi_db.c`)。ユーザーから読めるよう、
  エラー文字列は共有領域へ写される (`sys/kapi/kapi_db.c`)。
  呼出し元によるハンドルの所有検査は `sys/kapi/kapi_db.c` の `slot_get()` にある。
  `test_db_errstr.py (旧版 4f1b10929afc)` と `test_db_caller.py (旧版 0e5e43d0f720)` が検査する。

## 内部設計と契約 (D1)

拒否時は「何も変えない」
「受け付けた入力のときだけ」
「拒否の戻り値を見ずに step すると前の文の続きが返る」

## v50 の口 — 既存 DB を開く・prepare だけ・bind・診断

> 出所: S0_FOUNDATION・TASK_S0 (S0-K) の契約を今のソースと照らして要約 (2026-11-18)

ソース: `sys/kapi/kapi_db_v50.inc` (`kapi_db_open_existing`・`kapi_db_prepare_only`・`kapi_db_bind_*`・`kapi_db_error_code`・`db_resolve_fits`・`db_path_depth`・`db_journal_mode_check`)・`sys/kapi/kapi_db_helpers.inc` (`db_copy_input`・`sql_tail_check`・`shm_row_fits_n`・`shm_row_check`・`slot_note`・`slot_note_teardown`)。
上限の定数は `sdk/include/os32/os32_kapi_shared.h` (`DB_SQL_MAX_BYTES` 1024・`DB_BIND_TEXT_MAX` 255・`DB_BIND_BLOB_MAX` 4096・`DB_SHM_RESULT_LIMIT`)。設定レジストリ (`userland/lib/cfg/`) はこの口だけで DB に触る。

### `db_open_existing(path, writable)` — 作らずに開く

- `writable` は 0 (READONLY) か 1 (READWRITE) だけ。ほかは -1 + `SQLITE_MISUSE`。CREATE・URI は付けない。
- 拒否 (どれも SQLite を呼ばない = 副作用なし): 空・`:memory:`・`file:` で始まる path は `MISUSE`。呼び手のポインタを写せない (上限内に NUL が無いなど) も `MISUSE` で、SHM の診断文を「range check」と「引数そのもの」で分ける。
- path は解決 **前** に長さと深さを数える (`db_resolve_fits`)。`vfs_resolve_path` は `VFS_MAX_PATH` で切り詰めてから正規化し、`VFS_MAX_PATH_DEPTH` (32) を超えた成分を黙って捨てるので、溢れた入力が **別の DB の絶対名** に化ける。そこで `cwd + "/" + path + NUL` (絶対名は cwd 抜き) が `VFS_MAX_PATH` を超える・成分数 (空と `.` は数えず `..` は 1、相対名は cwd の成分も足す) が 32 を超える入力は `CANTOPEN` で断る。正規化で短くなる入力も巻き添えで断る。
- 解決後の絶対名で、SQLite を呼ぶ前に次を見る。
  - 本体の `vfs_stat`: NOTFOUND は `CANTOPEN`、それ以外の失敗は `IOERR` (I/O 障害を「無い」にしない)。0 バイトは `NOTADB`。
  - `<path>-journal` が下位層の名前の上限に収まらなければ `CANTOPEN` (本体を journal と取り違えない)。journal が在れば RO・RW とも `BUSY_RECOVERY` で、journal は消さない (回復は明示の操作だけ)。journal の stat が NOTFOUND 以外で失敗したら `IOERR`。
- 接続の枠が無ければ `FULL`。SQLite に渡すのは解決後の絶対名だけ。
- RO は変更 PRAGMA も照会もしない。RW は `journal_mode` が DELETE であることを確かめ (`db_journal_mode_check`)、照会の失敗 (NOTADB・IOERR・NOMEM) と「DELETE でない」(`CANTOPEN`) を分けて残し、close して -1。
- 成功で handle (0 以上)。失敗の拡張コードは owner 別の「直前の open 失敗」欄に置き、`db_error_code(-1)` で読む。成功で 0 に戻る。

### `db_prepare_only(handle, sql)` — step しない prepare

- SQL は NUL 込み 1024 byte 以内。超過は切り捨てず拒否。
- **入力の拒否** (NULL・空・上限超過・写せないポインタ) は旧 stmt・bind 済みの値・SHM・診断を何も変えない。拒否の戻り値を見ずに `db_step` すると、前の文が続きとして実行される。
- 入力が通ったら旧 stmt を finalize してから prepare する。SQL の誤り・2 本目の statement の拒否では旧 stmt はもう無い。
- 単一 statement の判定は SQLite に任せる (`sql_tail_check`): `pzTail` をもう一度 `sqlite3_prepare_v2` に渡し、「rc が OK で stmt が NULL」だけを末尾が空白・コメントと認める。`; \v`・UTF-8 BOM・`-- c`・`/* c */`・`;;` は通り、閉じていない `/*`・`\v` で始まる末尾・2 本目の文は拒否。拒否のあと stmt は残らない。
- SELECT の先頭行も DML も進めない。

### `db_bind_*` — prepare の後・最初の step の前だけ

- index は 1-based。範囲外は `SQLITE_RANGE`、prepare 前・step 後は `MISUSE`。
- text は 0〜255 byte、blob は 0〜4096 byte。負の長さ・超過は拒否。カーネルのスクラッチに写してから `SQLITE_TRANSIENT` で bind する (SQLite に呼び手のポインタを持たせない。bind の後に呼び手が値を書き換えても、同じスクラッチを次の bind が上書きしても、bind 済みの値は変わらない)。
- 0 byte の text・blob も非 NULL の空値 (`typeof` が `text`・`blob`)。NULL は `db_bind_null` だけ。

### `db_error_code(handle)` — 最後の失敗の診断

- データ操作 (open_existing・prepare_only・bind・step・exec) は成功で 0、失敗でその拡張コードに更新する。finalize・close は **失敗したときだけ** 更新する (後片付けで原因の診断を消さない)。stmt が無い `db_step` が DONE を返したときも 0 に戻す。
- 取得しても消えない。close 後も slot が再利用されるまでその値を返す (handle に世代は無い)。範囲外・一度も使っていない slot は `MISUSE`。
- `-1` は呼び手の owner の直前の open 失敗。owner ごとに別の欄で混ざらない。

### SHM の行の境界 (`shm_write_row`)

- 書く前に header + 全列の descriptor + payload の合計が `DB_SHM_RESULT_LIMIT` に収まるかを数える (`shm_row_fits_n`・`shm_row_check`)。超えたら範囲外に書かず、部分 ROW も返さず、`db_step` が -1 + `SQLITE_TOOBIG`。ちょうど収まる TEXT・BLOB は書ける。
- 列値の実体化に失敗した (accessor が NULL・`NOMEM`) 行も -1 で、欠けた ROW を成功にしない。stmt は生きたまま。
- SHM の配置と型の番号は変えない。

### 所有者の回収と exec の順序

- DB の slot は open した owner を持ち、`db_cleanup_owned(owner)` がその owner の接続だけを finalize → rollback → close する。子の回収で親の接続と実行中の stmt は無事。
- close が成功したときだけ slot を空ける。rollback・close に失敗した slot は隔離し、再操作・再利用・再 close をしない。
- exec の回収 (`sys/kern/exec/exec_finish.inc` の `exec_reclaim_resources`) は `exec_db_cleanup_owned(id)` を `vfs_close_owned(id)` より **先** に呼ぶ。SQLite の rollback・close が journal を読み書きできる間に FD を生かしておくため。

### 検査

`test_db_errstr.py (旧版 4f1b10929afc)` (本体 `kapi_db_v50_host.c (旧版 efcd32cfb5e9)`、実 `sys/kapi/kapi_db.c` + 実 SQLite + 実 `sys/fs/vfs_fd.c` + RAM のバックエンド)。上の各規則がケース名 (`open_existing`・`prepare_only`・`binds`・`error_code`・`shm_bound`・`shm_exact`・`stat_faults`・`journal_mode`・`step_no_stmt`・`path_len`・`transient`・`sql_tail_sqlite`・`prepare_replaces`・`materialize_fail`・`resolve_len`・`resolve_truncate`・`resolve_depth`・`cpl3_paths`・`owner_isolation`・`order_new`・`order_old`) に対応する。回収の順序はソースの本文でも確かめる。CPL=3 の受入はゲストの `userland/diag/db_v50_test.c`。

## 診断文と空文字列の置き場 (`db_last_error`・`db_column_text`)

> 出所: TASK_DB_ERRSTR の契約を今のソースと照らして要約 (2026-11-18)

ソース: `sys/kapi/kapi_db.c` (`kapi_db_last_error`・`kapi_db_column_text`)・`sys/kapi/kapi_db_helpers.inc` (`db_shm_diag`・`db_shm_empty`・`shm_write_error_text`・`shm_row_fits_n`・`shm_row_check`・`shm_write_row`)・`sys/kapi/kapi_db_selftest.inc`。領域の定数は `sdk/include/os32/os32_kapi_shared.h` (`DB_SHM_DIAG_SIZE`・`DB_SHM_DIAG_OFFSET`・`DB_SHM_ERRSTR_MAX`・`DB_SHM_EMPTY_OFFSET`・`DB_SHM_RESULT_LIMIT`)。

CPL=3 に返す文字列は、カーネルの番地 (`.rodata` の定数・slot の `.bss`・SQLite の帯) を指してはならない (アプリの PD から読めず #PF で落ちる)。**返す前に共有メモリのブロック 0 の末尾へ写し、その番地を返す。**

    [0]                                                          [16KB]
    | DB_ResultHeader | 列情報 | 結果データ |   診断文   | NUL |
                                            ^DIAG_OFFSET   ^EMPTY_OFFSET

- `DB_SHM_DIAG_SIZE` = 256。`DB_SHM_DIAG_OFFSET` = ブロック長 − 256 で、`DB_SHM_RESULT_LIMIT` (結果データが使える上限) と同じ値。診断文の器 `DB_SHM_ERRSTR_MAX` = 255 (NUL 込み)、最後の 1 バイト `DB_SHM_EMPTY_OFFSET` は常に NUL。
- **結果データの上限は `DB_SHM_RESULT_LIMIT` からだけ引く。** 結果側の上限を見ている箇所 (`shm_write_error_text` の `max_len`、`shm_row_fits_n` の列数と payload の 2 本、`shm_row_check` の溢れ検査、`shm_write_row` の `remaining`) のどれか 1 つでもブロック長から引くと、結果データが診断文を踏み潰す。事前検査が先に断つため実行時には見えない箇所があるので、`sys/kapi/kapi_db.c` (取り込む `.inc` を含む) に `DB_SHM_BLOCK_SIZE` の名前を出さないことを規則にしている。
- `db_last_error` の全経路 (範囲外・空きの handle、`cleanup_message`、`sqlite3_errmsg`、モジュール・エンジンが無い) は `db_shm_diag` で写した先頭を返す。写した後に SQLite 側で別の SQL を流しても文字列は動かない。
- 切り詰め: NUL を除いて 254 バイトを超えたら、印 `...` の 3 バイトを空け、UTF-8 の継続バイトの上で止まらないところまで戻してから写し、印と NUL を置く (写せなかった分を「写せた」ことにしない)。どの経路でも必ず NUL で終わる。
- 空文字列は `db_shm_empty` が返すたびに NUL を置いた 1 バイトを指す (SHM の初期化順に依らない)。`db_column_text` のエラー経路も `""` ではなくここを返す。
- `DB_ResultHeader.error_offset` と userland の `db_errmsg()` は 1 回の `db_exec` の結果に結びついた別の欄で、この領域とは別物。
- 自己診断 `db_v50_selftest()` (ホスト試験が呼ぶ) は領域の取り決め (診断領域 = 結果の上限、診断文と空文字列が重ならない、空文字列が NUL、長い文が上限で切れる) を bit 5 で見る。

検査: `test_db_errstr.py (旧版 4f1b10929afc)` (`make check-db-errstr-host`、本体 `db_errstr_host.c (旧版 423d3e5600dd)`、実 `sys/kapi/kapi_db.c` + 実 SQLite)。範囲 (`IN_SHM`) は `errstr_range`・`coltext_range`、切り詰めと NUL は `errstr_truncate`、上限ちょうどと +1 は `result_bound`、取り決めは `diag_layout`。`DB_SHM_BLOCK_SIZE` を名指ししないことは静的な番人 `check_no_block_size`。

## `kapi_db_column_text` の範囲検査

> 出所: TASK_T2D_T2H (T2e の KAPI の範囲検査の欠落の修正) の契約を今のソースと照らして要約 (2026-11-18)

結果の領域 (SHM) は呼び手が書き換えられるので、その中の `column_count`・`data_offset` を容量の正典にしない。

- col は `0 <= col`、かつ結果領域に収まる列情報の数 (`(DB_SHM_RESULT_LIMIT - sizeof(DB_ResultHeader)) / sizeof(DB_ColumnInfo)`) 未満、かつ `column_count` 未満。
- 列情報の `data_offset` は `0 < data_offset < DB_SHM_RESULT_LIMIT`。
- 外れたら SHM 上の空文字列 (`db_shm_empty`) を返す (カーネルの .rodata は CPL=3 から読めないので `""` を返さない)。slot が無い・文が無いときも同じ。

検査は `test_kapi_bounds.py (旧版 9543608bb83e)` (実 `slot_get` と実 `kapi_db_column_text` を、改竄した column_count・data_offset で通す。SQLite は実行しない)。
