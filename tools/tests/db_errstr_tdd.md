# `db_last_error()` / `db_column_text()` の返り先 (ホスト試験の記録)

- 票: `docs/archive/kernel_v21/TASK_DB_ERRSTR.md` §4 / §5
- 対象: [`kapi/kapi_db.c`](../../kapi/kapi_db.c) の `kapi_db_last_error` /
  `kapi_db_column_text` と、結果データの上限を見ている 5 か所
- 領域の管理元: [`sdk/include/os32/os32_kapi_shared.h`](../../sdk/include/os32/os32_kapi_shared.h)
  の `DB_SHM_DIAG_*` / `DB_SHM_RESULT_LIMIT` ([C4])
- 実行: `python3 -B tools/tests/test_db_errstr.py [--target] [--mutate] [case ...]`
  (`make check-db-errstr-host` が `--target` 付きで回す)
- 試験: [`db_errstr_host.c`](db_errstr_host.c) — 実物の `kapi/kapi_db.c` +
  実 SQLite + 実 `os32_sqlite_vfs.c` + 実 `fs/vfs_fd.c` を 1 行も写さずに
  `#include` し、模型は exec 側のポインタ検証と SHM の置き場だけ。
  DB は `:memory:` で開くのでホストの FS には触らない
- KAPI の型・スロット・版数は動かしていない (v55 のまま)。変えたのは
  「返り値がどこを指すか」だけ — `tools/check_kapi_version.py` も一致のまま

## 1. 何を確かめる試験か

CPL=3 のアプリが `db_last_error()` の戻り値を読むと **#PF で死んでいた**。
戻り値の 4 経路すべてがカーネル番地 (`.rodata` の定数 / slot の `.bss` /
SQLite の帯 `0x2xxxxx`) を指していて、共有メモリと違ってアプリの PD からは
見えないため。`db_column_text()` の 3 つの `return "";` も同じ形。

正しい形は「返す前に共有メモリへ写す」。置き場はブロック 0 の末尾に切り出す。

    [0]                                                        [16KB]
    | DB_ResultHeader | 列情報 | 結果データ | 診断文 | 空文字列 |
    0                                       ^DIAG_OFFSET       ^EMPTY_OFFSET

**いちばん危ないのは結果データ側**。上限を見ている場所が `kapi_db.c` に
**5 か所** (`shm_write_error_text` の `max_len`、`shm_row_fits_n` の 2 本、
`shm_row_check` の溢れ検査、`shm_write_row` の `remaining`) あり、1 か所でも
`DB_SHM_BLOCK_SIZE` から直接引いたままだと結果データが診断文を踏み潰す。

## 2. 見る 2 つのこと

| ID | 見るもの | どう書いたか |
|---|---|---|
| E6 | 返り先が共有メモリの中か | ホストでは番地の区別が付かないので、`test_shm` の**範囲の検査** (`IN_SHM`) として書く。`db_last_error` の全 4 経路と `db_column_text` の全経路を通す |
| E5 | 上限で切り、必ず NUL で終える | 診断領域を 0x5A で汚してから呼ぶ。**空の SHM では NUL の置き忘れが見えない** |
| E7 | 結果を上限まで書いても診断文が壊れない | 先に診断文を置いて写しを取り、上限ちょうどの行を書いてから写しと突き合わせる |

ケースは 5 本。

| ケース | 中身 |
|---|---|
| `errstr_range` | `db_last_error` の 4 経路 (範囲外 handle ×2 / 空きスロット / `cleanup_message` / `sqlite3_errmsg`)。写した後に SQLite 側で別の SQL を流しても文字列が動かないことも見る |
| `coltext_range` | `db_column_text` の正常系 + エラー 3 経路 (col 範囲外 / `data_offset == 0` の NULL 列 / slot・stmt 無し)。空文字列を返しても結果データが踏まれないことも見る |
| `errstr_truncate` | 上限を超える診断文 (ASCII / UTF-8)、短い文、手前と奥の番兵 |
| `result_bound` | 上限ちょうどの BLOB / TEXT は書ける、+1 は `SQLITE_TOOBIG` で断る、`shm_write_error_text` に 16KB 超を直に渡しても診断領域が無事 |
| `diag_layout` | 領域の取り決めと、ブート自己診断 `db_v50_selftest()` の bit 5 |

## 3. RED → GREEN

### RED (領域の定数だけ足して、`kapi_db.c` は手つかずの状態)

```
$ python3 -B tools/tests/test_db_errstr.py
HOST GNU89 -Werror compile PASS (real kapi_db.c + bundled SQLite)
FAIL errstr_range:169: IN_SHM(m)
FAIL coltext_range:237: IN_SHM(m)
FAIL errstr_truncate:300: IN_SHM(m)
FAIL result_bound:360: IN_SHM(m)
FAIL diag_layout:445: db_v50_selftest() == 0
SUMMARY -1/5 PASS
FAIL SOURCE: kapi/kapi_db.c は DB_SHM_BLOCK_SIZE から直接引いている
             (行 218, 252, 255, 316, 366, 1194, 1195, 1196)
```

上限を見ている箇所は **実数 8 か所** だった。票の「少なくとも 5 か所」は
結果データの上限そのもの (218 / 252 / 255 / 316 / 366) で、残る 3 か所は
ブート自己診断 `db_v50_selftest()` が同じ境界を再計算している行
(1194 / 1195 / 1196)。**自己診断を直さないと、直した実装の方が落ちる。**

### GREEN

```
$ python3 -B tools/tests/test_db_errstr.py --target
HOST GNU89 -Werror compile PASS (real kapi_db.c + bundled SQLite)
TARGET i386-elf GNU89 compile PASS
PASS errstr_range / coltext_range / errstr_truncate / result_bound / diag_layout
SUMMARY 5/5 PASS
SOURCE: kapi_db.c は上限を DB_SHM_RESULT_LIMIT からだけ引く (10 箇所) PASS
```

既存の回帰も通した (`test_db_errstr.py` 24/24、`test_cfg.py` 53/53、
`test_install_recover.py` 14/14、`test_vfs_fd_sqlite.py` (2026-10-24 に削除 (票 TASK_TEST_TIDY の TT5)) 15/15、
`test_sqlite_groups.py` (2026-10-24 に削除 (票 TASK_TEST_TIDY の TT5)) 32/32、`test_db_errstr.py` 1/1)。
`kapi_db_v50_host.c` の `shm_bound` / `shm_exact` / `materialize_fail` は
結果側の上限を `DB_SHM_RESULT_LIMIT` から引き直した (中身は変えていない)。

## 4. 否定側 (`--mutate`) — 8 本、すべて RED

| # | 変異 | 落ち方 |
|---|---|---|
| 1 | `shm_row_fits_n` の payload 側の上限を `DB_SHM_BLOCK_SIZE` に戻す | 実行時 (3 件) |
| 2 | `shm_write_error_text` の `max_len` を戻す | 実行時 (2 件) |
| 3 | `shm_write_row` の `remaining` を戻す | **静的な番人だけ** (1 件) |
| 4 | `db_last_error` の「範囲外の handle」をカーネル番地のまま返す | 実行時 (1 件) |
| 5 | `db_last_error` の `sqlite3_errmsg` 経路を帯のまま返す | 実行時 (3 件) |
| 6 | `db_column_text` のエラー経路を `return "";` に戻す | 実行時 (1 件) |
| 7 | 切り詰めで NUL を置き忘れる | 実行時 (1 件) |
| 8 | 切り詰めの上限を診断領域いっぱいに広げる | 実行時 (2 件) |

### 実行時には見えない変異がある — だから静的な番人を置いた

変異 3 (`shm_write_row` の `remaining`) と、同じ形の `shm_row_check` の中間の
溢れ検査は、**事前検査が先に断つので実行時には観測できない**。書き手の上限が
広がっても、そこへ届く行はその前に `SQLITE_TOOBIG` で落ちているため。

そこで票 §4 の「`DB_SHM_BLOCK_SIZE` を直接引き算している箇所を残さないこと」を
そのまま規則にした。`check_no_block_size()` が `kapi/kapi_db.c` にこの名前が
**1 回も出ない**ことを見る。5 か所のどれを戻しても、その時点で RED になる。

## 5. 実装で決めたこと

- `DB_SHM_DIAG_SIZE` = 256B。内訳は診断文 `DB_SHM_ERRSTR_MAX` (255B、NUL 込み) と
  「常に NUL の 1 バイト」(`DB_SHM_EMPTY_OFFSET`)。結果側は 16384 - 256 = 16128B。
- 空文字列は返すたびに NUL を置く。SHM の初期化順に依らせない。
- 切り詰めは (a) 印 `...` の分を空けて、(b) UTF-8 の継続バイトの上で止まらない
  よう戻してから写す。印を付けるのは、**写せなかったぶんを「写せた」ことに
  しない**ため (日本語は 3 バイト 1 文字なので §4-27 の切り口の規則も要る)。
- 既存の `DB_ResultHeader.error_offset` と userland の `db_errmsg()` は動かして
  いない。あれは 1 回の `db_exec` の結果に結びついた欄で、今回足したのは別物。

## 6. この試験が見ていないこと

- **ゲストでは 1 度も動かしていない。** 票 §5 の E1〜E4 (実機で `db_test` /
  `dbq` が落ちないこと、`$?` = 0) は未実施。ホストでは「番地が読めるか」は
  原理的に確かめられないので、範囲の検査までが限界。
- SQLite 本体・プールの大きさ・接続管理は触っていない。

## TT2: DB 検査の引継ぎ (2026-10-24)

`check-db-v50-host` / `check-db-owned-host` の登録と Python runner を削除し、
`check-db-errstr-host` が旧ケース・変異も実行する。C fixture は実 SQLite と
障害注入用 SQLite 模型の境界が違うため保持。実 SQLite のコンパイルは共有する。
従来の診断 6 ケース・変異 9 本は保持し、合計 39 ケース・変異 18 本
(ほかに回収順の静的な番人) とする。ここでのケースは C のシナリオ関数の単位。
既存の記録にある unittest の件数とは数え方が違う。

| 移管元 → 移管先 | ケース (前→後) | 変異 (前→後) | 対応 |
|---|---|---|---|
| v50 → errstr API fixture | 25→25 | 3→3 | `prepare_replaces`: copy 拒否前 finalize、SQL 切詰め。`group_callers`: journal を現在 owner で開く |
| owned → errstr 障害注入 fixture | 8→8 | 6→6 | `test_public_owner`: 2 ガードそれぞれの削除・反転・TRUSTED 拒否。ほか寿命、回収順、prepare 置換 finalize 失敗、finalize 失敗、rollback 失敗、BUSY 隔離、open 失敗を保持 |

v50 の残りは `API_CASES` に全登録し、回収順の本文検査も移管した。
db-caller (2026-10-24 から群 `check-access-host` の中の test_db_caller.py) と `check-kapi-ranges-host` の内容・ケース・変異は変更しない。

## E08: db_v50 の起動時判定をホストへ移管 (2026-10-30)

`kdebug/kselftest/db_v50.inc` は 7→0 判定。同一構成の `kselftest_pass` 期待値は移管前から 7 減らし、`kselftest_fail=0` を維持する。過去の実測値は変更しない。
起動ログから外れる名前は `KAPI v50: 7 new db slots appended at 201..207`、`db row: 16KB block bound counts descriptors`、`db ptr: NULL and length overflow refused`、`db path: journal name fits the VFS capacity`、`db diag: one open-failure slot per owner ID`、`db diag: error string area is outside the result area`、`db diag: DB_OWNER_SLOTS covers the whole app ID pool`。
先頭 6 判定は実 `kapi_db.c` を組む既存の `v50_selftest` が担い、末尾の容量比較を同ケースへ追加した。`slot_layout_append` / `shm_bound` / `user_range` / `path_len` / `diag_layout` / `errstr_truncate` も継続する。
ホストのシナリオ数は 39→39。変異は 18→19: 実 `appslot.h` の `APP_MAX_APPS` だけを 4→5 にし、旧固定値検査が通っても追加した容量比較が失敗することを見る。
起動側の削除前に追加済みホスト試験で GREEN を確認。ログは `/home/hight/os32-tmp/run/e-group/k-dbv50-before2.log`、最終結果は同ディレクトリの `k-dbv50.md`。
ゲスト受入は新しい kernel/kdebug の対を照合し、同一構成で自己試験数が 7 減り失敗 0、上記ログ 7 本が消えること、および `db_test` 9/9・`db_v50_test` 42/42 を確認する。今回の実装者はゲストを操作しない。
