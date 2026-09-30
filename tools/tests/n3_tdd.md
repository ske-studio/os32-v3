# N3 ホスト TDD の記録 (libos32host + wget / lpr / hclip / hdate)

票 [docs/archive/network/TASK_N3.md](../../docs/archive/network/TASK_N3.md) §4。C 側だけ
(Agent = `host_agent.py` は別コーダー・別票)。実行:

```
python3 -B tools/tests/test_host_lib.py [--target]
make check-host-lib-host        # 同じもの (--target 付き)
```

- `tools/tests/host_lib_host.c` … 実物の `userland/lib/host/libos32host.c` を
  `#include`、KAPI (`host_open`/`host_status`/`host_read`/`host_write`/`host_close`/
  `get_tick`/`sys_yield`) だけを台本式の贋物に。`sys_yield` が贋クロック `fk_now` を
  進める。`HOST_LIB_TEST_SEAMS` で無進捗期限を絶対期限へ落とす変異点 (`host__np_absolute`)
  を開く。**ライブラリは同時に 1 ハンドルしか開かない**ので、贋物は open ごとに 1 つの
  `HScript` を消費する単純な形。
- `tools/tests/host_cmd_host.c` … 実物の各コマンド (`-DHOST_TEST`、`main` を改名) を
  取り込み、`libos32host` の関数だけを贋物に。ローカル I/O は POSIX (実ファイル)。
  4 コマンドは同名 static を持つので `-DCMD_WGET`/`-DCMD_LPR`/`-DCMD_HCLIP`/`-DCMD_HDATE`
  で 4 回ビルドする。

## GREEN (現状)

```
host_lib_host: PASS 82/82        # N3-fix で 70 → 82 (§ N3-fix)
host_cmd_host (wget):  PASS 16/16
host_cmd_host (lpr):   PASS 10/10
host_cmd_host (hclip): PASS 9/9
host_cmd_host (hdate): PASS 5/5
TARGET i386-elf -Werror compile PASS (libos32host.c, wget, lpr, hclip, hdate)
SUMMARY 5/5 PASS

python3 -B tools/tests/test_host_agent.py   → SUMMARY 81/81 PASS  # N3-fix で 79 → 81
```

## ライブラリ (host_lib_host.c) が踏むもの — 票 §4

| ケース | 見るもの |
|---|---|
| `time` | TIME の 19B 本文・NUL 終端、open==close |
| `get_stream` | >64KB (200000B) を sink でストリーミング (メモリ一定)、パターン一致、nbytes |
| `get_404_status_order` | 業務 404 を out で返す (エラーにしない)、**http_status は read ループの前に確定** |
| `get_url_too_long` | url 1400B 超 → `HOST_EINVAL`、host_open は呼ばれない |
| `get_sink_abort` | sink が非 0 → `HOST_EABORT`、ハンドルは閉じる |
| `open_stale_up` | 最初の open が STALE を 200 tick (<3 秒) 返してから成功 |
| `open_stale_timeout` | 最初の open が STALE を返し続け 3 秒で `HOST_ELINK` |
| `second_stage_stale` | 1 段目成功 (link 確立) 後の STALE は 3 秒待たず即 `HOST_ELINK`、PRINT CLOSE を出さない |
| `status_timeout` | status が AGAIN のみ → 30 秒で `HOST_ETIMEOUT` |
| `noprogress_positive` | 20 秒間隔で進捗する 60 秒転送が成功 (read>0 で基準取り直し) |
| `noprogress_absolute_mutant` | **変異 (絶対期限)** にすると同じ 60 秒転送が 30 秒で `HOST_ETIMEOUT` (回帰の否定側) |
| `nosys` | NOSYS → `HOST_ENODEV` |
| `open_again_yield` | 両スロット rel_pending 相当: open が AGAIN → `sys_yield` → 成功 |
| `open_full_yield` | **N3-fix(b)** open が FULL → `sys_yield` 待ち → 成功 (`open_full` 欄を初めて使う) |
| `write_again_yield` | **N3-fix(a)** send_decl の write が AGAIN → `sys_yield` → 送り切る (REQUEST 転送 ACK まで必ず write AGAIN) |
| `write_noprogress_positive` | **N3-fix(c)** write>0 が 20 秒間隔で進む 60 秒転送が成功 (write 経路の基準取り直し) |
| `write_noprogress_absolute_mutant` | **N3-fix(c) 否定側** 絶対期限 (`host__np_absolute`) で同じ write 転送が 30 秒で `HOST_ETIMEOUT` |
| `clip_put_ok` | `CLIP PUT 5` の宣言長、書き込み、status 200 |
| `clip_put_empty` | 空 (0) と 4096 超は open せず `HOST_EINVAL` |
| `clip_get_503` | 503 → `HOST_ESERVICE` + svc_status、閉じる |
| `write_inval_business` | write INVAL → status 業務 500 → `HOST_ESERVICE` (+500) |
| `write_inval_again` | write INVAL → status AGAIN → 真の `HOST_EINVAL` |
| `write_inval_stale` | write INVAL → status STALE → `HOST_ELINK` |
| `print_text_70k` | 70KB を 65536 + 4464 に分割 (DATA 2 本)、pages、open==close==4 |
| `print_stream_onebyte` | 1 バイトずつ src + 途中 EOF: 宣言長 == write 合計 == 5、DATA 1 本 |
| `print_stream_empty` | 詰め 0 (即 EOF) → DATA を出さず CLOSE、pages 0 |
| `print_stream_abort` | src<0 → `HOST_EABORT`、CLOSE を出さない (後始末) |
| `print_409_cleanup` | DATA 段の 409 → `HOST_ESERVICE`、**PRINT CLOSE を出さない**、close==open==2 |
| `print_stream_bufsrc` | 40KB を 16KB バッファで DATA 3 本 (16384+16384+7232)、write 合計 40000 |

## コマンド (host_cmd_host.c) が踏むもの — 票 §2 の終了コード表

- **wget**: usage→3、200+file→ファイルに本文+"N bytes"+0、404→**空ファイルを残さず** "wget: 404"+1、
  200 かつ本文 0→空ファイルは作る+0、ELINK/ETIMEOUT/ENODEV→2、EINVAL(url 長)→3。
- **lpr**: usage→3、成功→"printed, N pages"+0、**basename 正規化** (`a b\tc.txt`→`a_b_c.txt`)、
  `-`→名前 `stdin`、ESERVICE→1 (svc 併記)、ELINK→2、開けない→4。
- **hclip**: usage→3、put→0、put 空→3、put 4096 超→3、get→0、get 503→1、get link→2。
- **hdate**: 成功→時刻 1 行+0、ELINK/ENODEV→2。

## RED→GREEN

1. **http_status の確定位置** (`get_404_status_order`): `libos32host.c` の
   `if (http_status) *http_status = (int)st;` を `drain_sink` の**後ろ**へ動かす変異で
   `host_lib_host: FAIL 69/70`（"http_status set before read loop" が落ちる)。正しい位置に
   戻すと 70/70。→ wget が 404 で空ファイルを作らない性質はこの確定順に依存する。
2. **無進捗期限の意味** (`noprogress_*`): 基準を取り直さない絶対期限 (変異点 `host__np_absolute`)
   にすると、20 秒間隔で 60 秒進む転送が 30 秒で `HOST_ETIMEOUT` に落ちる。正しい実装
   (進捗ごとに基準取り直し) では成功。両側を回帰として常時実行している。
3. 実装は先に書いたが、上記 2 つの変異を実際に注入して RED を確認 (1 は一時コピーで、
   2 は常時のテストケースで)。以降は GREEN。

## N3-fix (2026-09-14) — 試験硬化と Agent `/file/` NUL (票 §8)

実装 (`libos32host.c` / `host_agent.py` のサービス経路) は正しい。**試験だけ**を足して
贋 KAPI が実カーネルの経路を踏むようにし、Agent は 1 関数の例外処理を直した。触ったのは
`tools/tests/host_lib_host.c` (贋 `host_write` に AGAIN 欄・gap/chunk 欄、4 ケース追加)、
`tools/tests/test_host_agent.py` (実子 GET と NUL の 2 ケース追加)、
`tools/host_agent.py` (`_service_get_file` の 1 関数のみ)、この記録。

各変異を `libos32host.c` へ**一時的に**入れて FAIL を実測 → 戻して GREEN。実測:

1. **(a) write AGAIN を EIO 扱い**: `send_decl` の `if (n == OS32_ERR_AGAIN) { … sys_yield; continue; }`
   を `return HOST_EIO;` に変異 → `host_lib_host: FAIL 75/82`。落ちたのは
   `wagain: CLIP PUT succeeds after write AGAIN` / `… yielded` / `… 5B delivered`
   (+ 巻き添えで `wnoprog+` 系。gap も write AGAIN 経由のため)。戻して 82/82。
   → 実カーネルは REQUEST の転送 ACK まで必ず write AGAIN なので、この欠陥は
   CLIP PUT / PRINT DATA が本番で全滅するのに従来 70/70 を通していた。
2. **(b) open FULL を EIO 扱い**: `wait_open` の `case OS32_ERR_FULL:` を削除 (default→EIO) に変異
   → `host_lib_host: FAIL 79/82`。落ちたのは `openfull:` の 3 チェック。戻して 82/82。
   → `open_full` 欄は従来一度も使われず、FULL を EIO にする変異が素通りしていた。
3. **(c) 無進捗を絶対期限へ**: `np_reset` を `{ (void)d; }` (基準取り直しをしない) に変異
   → `host_lib_host: FAIL 65/82`。write 経路では `wnoprog+: 60s write with 20s gaps succeeds`
   / `… full body` / `… ~60s` が落ちる (read 経路の `noprog+` も同様)。戻して 82/82。
   常設の否定側ケース `write_noprogress_absolute_mutant` (`host__np_absolute` 版) も
   write 経路の絶対期限化を常時捕える。

Agent (`test_host_agent.py`、`python3 -B tools/tests/test_host_agent.py` → 81/81):

- **(2) 実子 GET** (`n3fix_get_child_real_http`): `http.server` を 127.0.0.1 に立て、
  `sys.executable -c GET_CHILD <url>` を**実子プロセス**で起動。`/ok`=200+本文、
  `/slow`=0.5 秒遅延でも本文完走、`/nope`=HTTPError → **rc 0 + status 404 + 本文**、
  `/drop`=応答を書かず切断 → URLError 未捕捉で **rc≠0** (Agent 側で 502)。実ネットワークは叩かない。
  従来 GET 試験は全て FakeProc で、この `GET_CHILD` 文字列は一度も実行されていなかった。
- **(3) `/file/` の NUL** (`n3fix_file_nul_byte_403`): `GET /file/a\0b` を Agent に送り、
  落ちずに 403 を返すこと。修正前 (`realpath` が try の外) は
  `ERROR n3fix_file_nul_byte_403: ValueError('lstat: embedded null character in path')`
  で例外が主ループへ抜ける (主ループは `ConnectionError` しか受けず Agent が落ちる)。
  修正: `realpath`/`commonpath` を try に入れ `(ValueError, OSError)` → 403。戻して ok。

## 範囲外 (別票 / 別コーダー)

- Agent (`tools/host_agent.py`) の非同期 GET・N2 残 non-blocker (票 §7) 本体は**別コーダー**。
  N3-fix では `_service_get_file` の NUL 例外処理 1 か所と、Agent 側試験の硬化のみ触った。
- ゲスト受入 (kernel-lgy98-link + host_agent v2、F6 の 64KB 超 wget 実測、票 §6) は PM / テスター。
