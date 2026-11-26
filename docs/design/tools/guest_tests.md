# tools/guest_tests.py — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_TEST_RUNNER の契約を今のソースと照らして要約
ソース: `tools/guest_tests.py`・`tools/guest_tests.txt`

所定の試験プログラムを NP21/W のゲストで一括実行し、ホストで合否を集計するランナー。`make check-guest` が本番、
`make check-guest-host` がホスト試験 (`test_guest_tests.py (旧版 0211b4e5744a)`、NP21/W・HTTP・実時間に触らない)。
試験プログラム側の約束事 (終了コード 0/1/2・集計行) は `userland/lib/rt/testresult.h`。

## 1. 割り方

上半分は純関数 (`parse_list` / `build_script` / `parse_output` / `parse_summary` / `classify` / `aggregate` /
`running_test` / `Watch` / `hostdrv_verdict` / `format_report` / `report_exit_code`)。下半分 (`Np21Guest` の HTTP、
ファイルの見張り、rshell の開き直し) は薄く保ち、`run_suite(guest, host_dir, entries, …, now=, sleep=, log=)` に
ゲスト・時計・待ちを注入できる。

## 2. 一覧 (`tools/guest_tests.txt`、`parse_list`)

1 行 1 件。`#` から行末はコメント、空行は無視。行の最初の語が名前 (`[A-Za-z_][A-Za-z0-9_]*`)、行全体がゲストに
打たせる命令 (`restest all` → 名前 `restest`)。名前の重複・使えない字・空の一覧は誤り。
画面デモ・対話・末尾でキー待ちするものは載せない。

## 3. 生成するスクリプト (`build_script`)

シェルにループと `;` の連結が無いので、試験ごとに**ちょうど 3 行**を一覧の順に無条件に並べる
(前の試験が落ちても次の行は走る):

    echo RUNTESTS BEGIN <合い言葉> > /host/test/out.txt
    echo RUN <名前> >> /host/test/out.txt
    <命令> >> /host/test/out.txt
    echo RC <名前> $? >> /host/test/out.txt
    ...
    echo RUNTESTS DONE <合い言葉> >> /host/test/out.txt

先頭だけ `>` で出力を切り詰める (前回の出力を混ぜない)。合い言葉は回ごとに作る (`make_token`)。

## 4. 出力の読み方と判定

`parse_output` は `RUN` 〜 `RC` を 1 試験のまとまりとし、CRLF も読む。集計行は行頭が `<自分の名前>: ` の行だけを採り
(他人の行を拾わない)、複数あれば最後のもの。`RC` の値が数でなければ `None` (勝手に 0 にしない)。

`classify(rc, summary)` は**終了コードと集計行の両方**で決め、片方を選ばない:

| `$?` | 集計行 | 判定 |
|---|---|---|
| 0 | `PASS n/n` (n > 0) | pass |
| 1 | `FAIL m/n` (m ≠ n) | fail |
| 2 | `SKIP 理由` | skip |
| 126 / 127 / 130 / 139 | **無い** | nostart / notfound / intr / crash |
| 126 / 127 / 130 / 139 | ある | mismatch |
| 0・1・2 | 無い・種別が違う・数が釣り合わない | mismatch |
| 3〜125 | — | local (不合格に数える) |
| それ以外 | — | mismatch |
| 値が無い | — | incomplete |

合格に数えるのは pass と skip だけ。`aggregate` は一覧の全件を 1 行ずつ出す (出力に無ければ missing)。
BEGIN が無い・合い言葉が違う (前回の出力)・同じ `RUN` が 2 回・一覧に無い結果・DONE が無い、は問題として報告する。

## 5. 通しの順序 (`run_suite`)

1. `/host` が使えるか確かめる: ホスト側の置き場が在ること、ゲストに `/host/test/probe.txt` へ合い言葉を書かせて
   ホストで読めること (`hostdrv_verdict`)。使えなければ**走らせずに**断る。前回の `out.txt`・`probe.txt` は先に消す。
2. rshell を開き直す (`reopen_shell`、ESC → `rshell` → `ver`)。
3. スクリプトを `/host/test/runtests.sh` に置き、`source` を投げる。応答の時間切れは失敗ではない。
4. 出力ファイルを見張る (`Watch`)。DONE が出たら終わり。大きさが `stall_secs` 伸びなければ stall、
   `total_secs` に当たれば timeout。どちらも**そこまでの結果を集計してから**終わり、`running_test` で
   始まって終わっていない試験を名指しする。
   `stall_secs` は 60 秒未満を断る ([V3]、既定 180)。`total_secs` は `stall_secs` より長くなければ断る (既定 3600)。

## 6. ランナーの終了コード (`report_exit_code`)

| 値 | 意味 |
|---|---|
| 0 | 全件合格 (SKIP は不合格に数えない) |
| 1 | 使い方・一覧の誤り |
| 2 | 不合格・食い違い・問題があった、または DONE が無い |
| 3 | 固まった / 全体の上限に当たった (固まったが名前が分からない場合も 3) |
| 4 | 前提が無い (`/host` が使えない、ゲストに届かない、rshell を開き直せない) — 合格にしない |
