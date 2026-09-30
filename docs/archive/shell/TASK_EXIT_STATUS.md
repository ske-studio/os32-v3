# TASK_EXIT_STATUS — 終了コードの配線と `$?` (ゲスト試験ランナーの 1 段目)

> 発行: PM (Claude Code `claude-opus-5`、2026-09-16) / 状態: **受入完了 (2026-09-16)**

基点: `feat/gui` = `15c5edf`。
引き継ぎ: [`../agents/HANDOVER_2026-09-16.md`](../agents/HANDOVER_2026-09-16.md) §7。
後続: 結果チャネル (TASK_TEST_RESULT、未起票)、ランナー (TASK_TEST_RUNNER、未起票)。
決裁済み: **E1** = `exec_last_result` を KAPI に足す / **E2** = 組み込み handler を `int` にする (ユーザー、2026-09-16)。
**分割 (ユーザー決裁 2026-09-16)**: 設計レビュー 4 往復で「入力を黙って切り詰めて進む」欠陥が出続けたため、
その系統は [`TASK_SH_TRUNCATION.md`](TASK_SH_TRUNCATION.md) に切り出した。**本票はその票が緑になってから着手する**
(切り詰めを断つ前に `$?` を配線すると、捨てられた入力が「成功」として `$?` に載る)。
本票に残る切り詰めの話は「断ったときに何を `$?` に入れるか」だけ。

## 0. 目的

ゲストで試験を一括実行し、合否を機械が読める形で返すための 1 段目。
**外部プログラムと組み込みコマンドの終了コードをシェルまで正しく届け、`$?` とスクリプトの失敗停止で使えるようにする。**

## 1. 確認した事実 (2026-09-16、`15c5edf`。行番号は確認時のもの)

1. **終了コードと起動エラーが同じ値の空間に混ざっている**。`sh_launch(cmdline)` = `g_api->exec_run(cmdline)`
   (`userland/shell/shell.h:114`)。`exec_run` は正常終了で `exec_exit_status` をそのまま返し (`exec/exec.c:1552`)、
   起動失敗では `EXEC_ERR_*` (`-1`〜`-5`) を返す。
2. **実害 1 — 1 コピーでも 2 回実行される** (レビュー往復 1 所見 1、経路を確認済み)。
   カーネルの `exec_launch` はパスに `/` が無いと `/bin/` `/sbin/` `/usr/bin/` を自分で順に探す
   (`exec/exec.c:1249-1265`。`SYS_DEFAULT_PATH` = `include/config.h:36` と同じ並び)。
   シェルも 2c で同じ PATH を走査する (`userland/shell/main.c:334`)。
   子が `return -1` (または `-3`) で終わると `try_exec` の戻り値が `EXEC_ERR_GENERAL` / `NOT_FOUND` と同じ値になり、
   `main.c:323` が「このディレクトリには無い」と読んで 2c へ進み、**同じ `/usr/bin/xxx.bin` をもう一度実行**して
   最後に `command not found` と表示する。同名を 2 か所に置く必要は無い。
3. **`exit(-2)` と fault を区別できない**。`exec_fault_recover()` は `exec_exit(EXEC_ERR_FAULT)` を呼ぶだけで
   (`exec/exec.c:1001-1003`)、`kapi_sys_exit(status)` も同じ `exec_exit(status)` に入る (`:1005-1013`)。
   書き手は `exec_exit` ただ 1 つ (`:950` で `exec_exit_status = status`)。
   **値からは種別を作れない**。CTRL+STOP も同じ経路 (`:1056`)。
4. **起動失敗は `exec_exit` を通らない**。`exec_launch` には早期 return が 13 か所あり (`:1191` `:1269` `:1277`
   `:1286` `:1295` `:1365` …)、`exec_exit_status` は前回の値のまま。`exec_run` は `exec_launch` を呼ぶだけ (`:1779`)。
5. **`try_exec` は常駐シェルと `sh.bin` の共通コード** (`main.c:140-188`)。`sh_launch` は常駐ではマクロで `exec_run`、
   `sh.bin` では要求表経路 (`sh_launch.inc`)。`sh.bin` 側でカーネルの静的な記録を読むと、**自分の子とは限らない**。
6. **`exit` は既に 2 か所にある**。`sh.bin` の組み込み (`cmd_base.c:168`、端末を閉じる)、スクリプト内の打ち切り
   (`cmd_script.c:173-181`)、`rshell` は行がちょうど `exit` なら自分の経路を閉じる (`rshell.c:143`、`execute_command` に渡さない)。
   組み込み表は**先勝ち** (`main.c:290-294`、`cmd_base` の初期化が `cmd_script` より先)。
7. **`set -e` は既存の `set` に食われる**。`cmd_set` は `argc == 2` で `=` が無ければ「値の表示」に落ち、
   `-e: not set` と出す (`cmd_env.c:211-219`)。`set` の登録も先勝ちで `cmd_env` が取る。
8. **組み込みコマンドの結果は捨てられている**。`execute_command` は `void` (`main.c:637`)、handler も戻り値を持たない。
9. **`$?` が無い**。`env_expand` (`cmd_env.c:97`) は環境変数の展開だけ。
10. **park は `exec_run` の子には起こらない**。`appslot_park_check` は `a->gui`
    (= `exec_start` で立てたアプリ) 以外を弾く (`exec/appslot.c:295-302`)。`exec.c:1550` の app_id 返却は
    `exec_start` 経路だけ。**実害 2 (`exit(2)`〜`exit(5)` と park の混同) は `exec_run` では起きない**。
11. **handler に届かない行がある**: リダイレクト失敗 (`main.c:361-463` → `:583` で実行しない)、行が長すぎる
    (`:536`、`:647-650`)、引数・glob 過多 (`:544-563`)、パイプの確保失敗 (`:675-678`)、`command not found` (`:345`)。
12. GUI 端末の要求表は DONE に終了コードを載せない (`exec/launch.c:266`)。`launch_report(token, rc)` は
    `rc > 0` を app_id と読む (`:233-247`)。
13. `sdk/kapi.json` は v52。**H2 が v53 を取る**ので、本票は着地順に応じて v53 か v54。
14. **入力を黙って捨てる経路が 4 つある** (往復 2 で確認):
    - `script_load` は 255 文字を超える行を切り詰め (`cmd_script.c` の `li >= SCRIPT_MAX_LINE`)、
      128 行を超えたら残りを捨てて `break` する (`SCRIPT_MAX_LINES`、`shell.h:25-26`)。どちらも読み込みは成功扱い。
    - `try_exec` はコマンド行を `TRY_EXEC_BUF_SIZE` に再構築し、**溢れた引数を落としたまま起動する**
      (`main.c:140-170`)。`exec` 組み込みと `time` も同じ形。
    - `split_pipeline` は `count < max_stages` で走査を打ち切るので、**9 段目以降は実行されない**
      (`main.c:606` の `MAX_PIPE_STAGES` = 8)。空の段も捨てられる。
    - 常駐シェルのパイプ段ループには打ち切りが無い (`main.c:724-733` の `sh_exit_flag` 検査は `SHELL_AS_APP` だけ)。
    - `run_cmd_internal` は**コマンド名を `PATH_MAX_LEN - 5` で切って** `.bin` を付ける (`main.c:299-305`)。
      251 バイトの接頭辞 `P` が実在すると、`Pextra` と打っても `P.bin` が起動する。
      `try_exec_from_path` のパス連結も同じ (`main.c:222`)。**`try_exec` の溢れ検査より前**に起きる。
    - `glob_cb` は `mem_alloc` の失敗で**印を立てずに戻る** (`sh_args.inc:84`)。`rm /tmp/item*` が 2 件に
      一致して 1 件目だけ確保できると、**1 件だけ消して成功**になる。
    - `rshell` は 126 文字で読み取りを止め、その接頭辞を実行する (`rshell.c:120-124`)。
    - `sh.bin` の要求表は NUL 込み **256 バイト** (`exec/launch.c:138-146` の `LAUNCH_CMDLINE_MAX`)。
      256〜511 バイトの行は再構築に成功して `launch_req` が `OS32_ERR_INVAL` を返し、
      今の `sh_launch.inc` はこれを「GUI 外」と読む。
15. `exec_exit` の直接の呼び手は 3 つ (`exec_fault_recover` / `kapi_sys_exit` / CPL=0 実行から戻った後の
    `exec_exit(EXEC_SUCCESS)`)。`exec_kill_one` は `exec_exit` を通らず自分で回収する。
    CTRL+STOP は `ring3_abort_check` → `ring3_fault_kill` → `exec_fault_recover` と流れるので、
    **`ABORTED` を作るにはこの途中で種別を渡す配線が要る**。

## 2. 設計

### 2-1. カーネル: 結果を「種別 + 値」で記録する

新しい KAPI `int exec_last_result(int *kind, int *code)` (決裁 E1)。`exec_run` の戻り値の意味は変えない ([ABI2])。

- **種別は呼び手が決める** (事実 3)。`exec_exit` は内部関数なので引数を 1 つ増やし、
  `kapi_sys_exit` → `EXEC_KIND_EXITED`、`exec_fault_recover` → `EXEC_KIND_FAULT`、
  CTRL+STOP の経路 → `EXEC_KIND_ABORTED` を渡す。**値から種別を推測しない**。
  (ランナーは「時間切れで畳んだ」と「落ちた」を分けたい。定数は `os32_kapi_shared.h` に置く [C4]。)
- **`exec_run` のすべての return 点で書く** (事実 4)。形は
  `{ g_last_kind = EXEC_KIND_NONE; rc = exec_launch(cmdline, 0); if (g_last_kind == EXEC_KIND_NONE) { g_last_kind = map(rc); g_last_code = 0; } return rc; }`。
  `map()` は `NOT_FOUND` / `INVALID` / `NOMEM` / `GENERAL` と、`appslot_start_admit` が返す
  `OS32_ERR_FULL` / `OS32_ERR_INVAL` (`exec.c:1191`) を **`EXEC_KIND_NOMEM` 相当 (= 次の候補へ進まない)** に写す。
- `exec_start` / `exec_resume` (GUI 経路) は**この記録を書かない**。`sh.bin` がカーネルの記録を読まないので
  (§2-2)、書くと紛れるだけ。
- `exec_last_result` は記録が無い (`NONE`) なら `OS32_ERR_INVAL`。

### 2-2. 起動の口を 1 つにする

事実 5 の穴を塞ぐため、`try_exec` からは**結果つきの起動口**だけを呼ぶ:

```c
/* shell.h。常駐と sh.bin で実装が違う */
int sh_exec_result(const char *cmdline, int *kind, int *code);
```

- 常駐 (`#ifndef SHELL_AS_APP`): `exec_run` → **直後に** `exec_last_result`。
- `sh.bin` (`SHELL_AS_APP`): `sh_launch.inc` の中で写像する。`LAUNCH_ST_DONE` → `(EXITED, 0)`、
  `LAUNCH_ST_FAILED` → `(起動失敗の種別, 0)`。**カーネルの記録は読まない**。
  要求表に終了コードを載せるのは別票 (§5)。
- `try_exec` / `try_exec_from_path` は `kind` だけで分岐する。
- 常駐シェルの SHA-256 一致 (`shell.h:89` の注記) はこの票で変わる。**注記を書き換える**。

### 2-3. シェル: 終了コードを持ち回る

- `execute_command` を `int` にし、`g_last_status` に入れる。呼び出し元 6 か所
  (`cmd_base.c:135`、`cmd_script.c:200,429`、`ui.c:545,706`、`rshell.c:149`) を更新する。
- 外部の写像 (定数で 1 か所にまとめる [C4]):

  | kind | `$?` |
  |---|---|
  | `EXITED` | 子の値 (0〜255 に丸める。負値もそのまま識別できるよう下位 8 ビットを使う) |
  | `FAULT` | `SH_STATUS_FAULT` (139 = 128+11) |
  | `ABORTED` (CTRL+STOP) | `SH_STATUS_ABORTED` (130) |
  | `NOT_FOUND` | 127 |
  | `NOMEM` / `FULL` / その他の起動失敗 | 126 |
  | `INVALID` (OS32X ヘッダが不正) | 126。**走査は次の候補へ進む** (事実 11 と別。今は止まる = 意図した変更、試験を足す) |

- **PATH 走査は `kind` で止める**: `NOT_FOUND` と `INVALID` のときだけ次の候補へ。
  `EXITED` / `FAULT` / `ABORTED` / `NOMEM` では**どんな値でも止まる** (事実 2 の修正)。
- **走査を尽くしたときの値** (往復 2 所見 1): 途中で `INVALID` を 1 度でも見たかを cwd と PATH をまたいで覚え、
  見たなら **126**、全部 `NOT_FOUND` なら **127**。`INVALID` を見たのに 127 (見つからない) と言わない。
- **入力を捨てたら失敗にする** (事実 14、往復 2 所見 3〜5)。「切り詰めたが動いた」を成功にしない:

  | 経路 | 変更 |
  |---|---|
  | `script_load` の行が長すぎる / 行数超過 / ファイルが読み込み上限を超える | **スクリプトを実行しない**。`source` は 2 |
  | `try_exec` / `exec` 組み込み / `time` の再構築が溢れた | **起動する前に** 2。クォートの再付与で伸びる場合も検査する |
  | パイプの段数超過 (9 段以上)、空の段 (`echo ok |`、先頭の `|`、`||`) | 行全体を実行せず 2 |
  | `stdin/stdout buffer lost` で段ループを抜けた | 2 |
  | `run_cmd_internal` のコマンド名の切り詰め、`try_exec_from_path` のパス連結の切り詰め (往復 3 B2) | **別のファイルを起動せず** 2 |
  | `glob_cb` の `mem_alloc` 失敗 (往復 3 B3) | 行全体を 2 で拒否し、handler を呼ばない。確保済みの文字列は解放する |
  | `rshell` の 126 文字超の入力 (往復 3 B4) | 行末まで読み捨て、**その行を実行せず** 2。次の行から正常に戻る |
  | `sh.bin` の要求表の上限 (256 バイト) 超過 (往復 3 B1) | **送信する前に** 2。`launch_req` を呼ばない (「GUI 外」と読まない) |

  `sh_exec_result` とは別に、**シェル側の組み立て失敗**を呼び手へ返せるようにする
  (起動の失敗と混ぜない)。

  **どこで断るかは [`TASK_SH_TRUNCATION.md`](TASK_SH_TRUNCATION.md) の範囲**。本票は「断った行の `$?` は 2」
  という値の割り当てだけを担う。

- **handler に届かない行の `$?`** (事実 11、レビュー所見 7):

  | 事象 | `$?` |
  |---|---|
  | 構文・リダイレクト失敗・展開で溢れた・引数/glob 過多・パイプの確保失敗 | `SH_STATUS_USAGE` (2) |
  | `command not found` (`main.c:345`) | 127 |
  | `sh.bin` が起動を断った (GUI 外など) | 126 |
  | 空行・コメントだけの行 | **変えない** |

- 組み込み: 表の handler を `int (*)(int, char **)` に (決裁 E2)。「赤字のエラーを出す分岐 = 非 0」。
  - 戻らないもの (`reboot`、引数なしの `os32gui` = `sys_exit(0)`) はそのままでよい。
  - `rshell` / `filer` はループを抜けた後に 0。
  - `source` は最後に実行した行の値 (`script_source_file` の戻り値)。`time` と `if` は内側の値
    (`if` は条件が偽なら 0)。`goto` のラベル無し (`cmd_script.c:476`) と ESC 中断 (`:193-195`) は非 0。
  - **パイプラインは最後の段の値**。ただし**明示の `exit` が優先**する (往復 2 所見 2)。
    `exit 3 | echo tail` のように途中の段で `exit` が立ったら、**常駐側でも**段ループを抜ける
    (今の打ち切りは `SHELL_AS_APP` だけ、事実 14)。「終了要求が立ったか」と「終了値」は**別の変数**にする
    — 真偽値に値を入れる作りだと `exit 0` で終われない。
  - `exec` 組み込み (`cmd_mnt.c:60-64`) は戻り値で印字を分けているので、`kind` 分岐に直す。
  - 取りこぼしを捕まえるのは**表の初期化子の型不一致** (関数ポインタ)。シェルのビルドに `-Werror` は無い
    (`build/programs.mk`) ので `-Wreturn-type` には頼らない。必要なら `-Werror=return-type` を足す。

### 2-4. `$?` の展開

- `env_expand` の名前走査の**手前**、`$` の直後・`{` 判定の前で `?` を特別扱いする (`cmd_env.c:123-135`)。
  `$?x` は `0x` のように展開する。**`${?}` は対応しない** (文書に書く)。
- `$?` は環境変数表に書かない (子に継承させない)。`set ?=5` で作った変数があっても、特別扱いが先なので隠れる。
- パイプ行の `$?` は段を回す前に 1 回だけ展開される (`main.c:647`)。文書に明記する。
- 最大 11 文字。既存の溢れ検査 (`:158`) に乗る。

### 2-5. スクリプト

- **`exit [N]`**:
  - 常駐シェル (`#ifndef SHELL_AS_APP`) では `cmd_script.c` に登録する。スクリプトの中なら打ち切り、
    `g_script_depth == 0` (対話) では**シェルを終わらせず** `$?` を `N` にするだけ。
  - `sh.bin` では `cmd_base.c` の既存の `exit` を残し (端末を閉じる = 決裁 D2(d))、`N` を受け取ったら
    `sh_exit_flag` と `main` の戻り値に載せる。**表は先勝ちなので、登録を `#ifdef` で分ける**。
  - `rshell` 経由では裸の `exit` を送らない (送ると `rshell.c:143` が自分の経路を閉じる)。ランナーの規約に書く。
- **`set -e` / `set +e`**: `cmd_set` (`cmd_env.c`) の**先頭**で `-e` / `+e` を拾い、`cmd_script.c` の
  `script_errexit` を関数経由で立てる (事実 7)。`set` を二重登録しない。
  入れ子の `source` を抜けるときは `script_abort_flag` と同じく save / restore する。
- 止めたときは `script: line N: status S` を 1 行。**N は「保持された行の位置」** — コメントと空行は
  `cmd_script.c:132-143` で詰められ、ラベル行 (`:label`) は保持されるので、ファイルの行番号とも
  実行したコマンドの本数とも一致しない (往復 2 所見 7)。文書にそう書く。

### 2-5-1. 実装前に固める契約 (往復 3 の非 blocker)

- `script_source_file` の戻り値は今「0 / -1」。open / read / 確保 / 深度超過 / 切り詰めの値を決める。
  呼び手は `source` のほかに**暗黙の `.sh` / `.bat` 実行**と**起動時の profile 読み込み** (`ui.c:505`) がある。
  profile の失敗が `$?` に残らないよう、profile 実行の後に `$?` を 0 に戻す。
- `$?` の初期値は 0。引数なしの `exit` は直前の値。`exit` の引数が非数値・範囲外・2 つ以上なら 2 (実行はしない)。
- 空のスクリプト (`sys_read()` が 0) は今「失敗」。S13 で 0 を期待するなら、この扱いも変える。
- パイプを 2 で断ったときの副作用: `echo x > /tmp/out |` が**ファイルを作らない・切り詰めない**こと、
  拒否の後にパイプ深度とリダイレクトが残らないこと。
- `rshell` 経由の観測: `;` を区切りとして解釈しないので、ゲスト受入は「試験コマンド → 完了待ち → `echo $?` を別送信」
  かスクリプトで行う。`rshell` を抜けるとその handler の 0 で上書きされる。
- `exec_last_result` / `sh_exec_result` の戻り値 (成功 0 / 記録なし `OS32_ERR_INVAL`)、失敗時の出力引数の値、
  シェル側の組み立て失敗の返し方を固定する。

### 2-6. GUI 端末 (`SHELL_AS_APP`)

要求表に終了コードを載せるのは**別票**。この票では `sh.bin` の `$?` は「起動できたか」までしか分からない
(DONE → 0)。そう文書に書く。

## 3. 範囲

| 層 | ファイル |
|---|---|
| KAPI | `sdk/kapi.json` (末尾に 1 本)、`sdk/include/os32/os32_kapi_shared.h` (`EXEC_KIND_*`)、`docs/KAPI_SPEC.md` |
| カーネル | `exec/exec.c` (`exec_exit` の引数、`exec_fault_recover`、`kapi_sys_exit`、CTRL+STOP、`exec_run` の記録)、`exec/exec.h`、`kapi/` の `__cdecl` ラッパ [C3] |
| シェル | `userland/shell/shell.h` (`sh_exec_result`、SHA-256 の注記)、`main.c` (`try_exec*`、`execute_command`、届かない行の状態)、`sh_launch.inc` (写像)、`cmd_env.c` (`$?`、`set -e`)、`cmd_script.c` (`exit`、`errexit`、行番号)、`cmd_base.c` (`sh.bin` の `exit`)、`cmd_mnt.c` (`exec` 組み込み)、`cmd_*.c` 全部 (handler の型)、`rshell.c` / `ui.c` (呼び出し元) |
| 試験 | `tools/tests/sh_status_host.c` + `sh_status_tdd.md`。**`try_exec` / `try_exec_from_path` は `main.c` の static なので `sh_exec.inc` に切り出す** (`sh_shell_host.c` は `main.c` を include していない)。`sh_shell_host.c:442,463` のスタブを型に合わせる |
| 文書 | `docs/manpages/` のシェルの頁 (`$?`、`exit`、`set -e`、`${?}` 非対応、行番号の意味)、`docs/POLICY_DEBUG.md` §4 (事実 2 の記録) |

## 4. 受入

### 4-1. ホスト試験

| ID | 反例 | 期待 |
|---|---|---|
| S1 | 子が `exit(0)` / `exit(1)` / `exit(-1)` / `exit(-2)` / `exit(-3)` / `exit(-4)` / `exit(-5)` / `exit(2)` / `exit(255)` | `$?` は表のとおり。**`-2` が `FAULT` にならない** (fault は別経路で起こす) |
| S1b | fault で落ちる子、CTRL+STOP で畳んだ子 | `SH_STATUS_FAULT` / `SH_STATUS_ABORTED`。`[Process crashed]` の表示は今どおり |
| S2 | 候補 1 が `NOT_FOUND`、候補 2 が存在 | 候補 2 を実行。**成功の直後に未知コマンドを打っても前回の記録を読まない** (事実 4) |
| S2b | `exit(-1)` の子を `/usr/bin` に**1 本だけ**置く | 実行回数は **1 回**。`command not found` を出さない (事実 2) |
| S2c | OS32X ヘッダが不正な `foo.bin` が cwd にあり、PATH にも同名の正しいものがある | PATH の方を実行する (意図した変更) |
| S2d | `appslot_start_admit` が `OS32_ERR_FULL` | 126 で**止まる** (PATH の数だけ繰り返さない) |
| S3 | 組み込みの成功 / 失敗 (`cd /nonexistent`、無いファイルの `cat`、既存への `mkdir`) | 0 / 非 0 |
| S3b | `source` / `time` / `if` (真・偽) / パイプライン / `goto` のラベル無し / ESC 中断 | §2-3 の規則どおり |
| S4 | リダイレクト失敗、行が長すぎる、引数過多、パイプの確保失敗、`command not found`、空行 | 2 / 2 / 2 / 2 / 127 / 変わらない |
| S5 | `set -e` のスクリプトで 2 行目が失敗 (外部・組み込み・リダイレクト失敗・未知コマンドの 4 通り) | 3 行目を実行しない。`source` の終了コードは非 0 |
| S5b | `set +e` で戻す、入れ子の `source` を抜けたとき | 旗が正しく戻る |
| S6 | 常駐で `exit 3` (対話) / スクリプト内の `exit 3` | シェルは終わらず `$?`=3 / 打ち切って `source` が 3 |
| S6b | `sh.bin` の `exit` / `exit 3` | 端末が閉じる (既存の挙動、`test_sh_shell.py` が緑のまま) |
| S7 | `$?`、`$?x`、`$` 単独、`${?}`、`$VAR` との併用、パイプ行の `$?` | §2-4 の規則。既存の展開を壊さない |
| S8 | cwd に不正な `foo.bin`、PATH には同名が**無い** | `$?` = 126 (127 ではない、往復 2 所見 1) |
| S9 | `exit 3 | echo tail` (常駐 / `sh.bin`、`if` / `time` 経由、入れ子 `source`)、`exit 0` | 後段を実行せず 3 / 0。`exit 0` でも終了要求が立つ |
| S10 | 255 文字を超える行、129 行以上、読み込み上限を超えるファイル | スクリプトを実行せず `source` は 2 |
| S11 | 再構築が溢れる長さの外部コマンド / `exec` / `time` | 起動する前に 2 (子を起こさない) |
| S12 | 9 段のパイプ、`echo ok |`、`| echo`、`a || b` | 行全体を実行せず 2 |
| S13 | リダイレクトだけの行 (`argc == 0`)、help の短絡、実行行が 1 つも無い `source` | 票の表どおりの値 (0 / 0 / 0) を決めて固定する |
| S14 | 入れ子 `exec` (子が非 0 → 親が別の値 → その後に起動失敗) | それぞれ正しい値。前の記録を読まない |
| S15 | GUI の `exec_start` / `exec_resume` / kill が走っている間に常駐の同期起動 | 記録が混ざらない |
| R1 | `test_sh_shell.py` / `test_sh_launch.py` / `test_fs_kind_callers.py` | 退行なし |
| R1b | `test_sh_launch.py` の `case_req_nogui` は `EXEC_ERR_NOT_FOUND` と次候補への継続を期待する (`sh_launch_host.c:338`)。新設計の「GUI 外 = 126・走査停止」と食い違う | 試験を新しい結果 API へ移す (旧 `sh_launch` の契約は残さない)。**票のこの変更を明記** |
| R1c | `test_fs_kind_callers.py` の `run` は `void` handler を受ける (`fs_kind_callers_host.c:37`、`-Werror`) | E2 の型変更に合わせる |
| R2 | 新しい試験は**実物の登録表**を通す (`sh_shell_host.c:442,463` のスタブは `exit` を直接認識していて、本物の登録・伝播が壊れていても緑になる、往復 2 所見 6) | `execute_command` / `execute_single` / ルーターを実物で通す |

park (事実 10) は `exec_run` では起こらないので**表の項目としてだけ**置き、実行試験は作らない。

### 4-2. ゲスト受入

終了コードを返す小さな試験バイナリを `userland/tests/` に置き `deploy.yaml` に登録 ([V2])。
`/api/cmd` (POST の生ボディ) で `t_exit1; echo $?` 相当を送り、`/api/tvram` の `lines` で読む。
**S2b は 1 コピーで実行回数 1 を確かめる** (実行のたびにファイルへ 1 行追記して数える)。

## 5. 決裁と、この票でしないこと

| ID | 問い | 決裁 |
|---|---|---|
| E1 | 起動結果の取り方 | **(a) `exec_last_result` を KAPI に足す** (ユーザー、2026-09-16) |
| E2 | 組み込みの戻り値 | **(a) handler を `int` に変える** (ユーザー、2026-09-16) |

しないこと: GUI 端末の要求表に終了コードを載せる (別票)、`if` / `&&` / `||` の**構文**の追加、
`${?}` の対応、`$!` `$$` などの他の特殊変数。

## 6. 往復記録

### 往復 1 — 設計レビュー (Fable 5.1 サブエージェント、`c8364e1` 対象、2026-09-16) — Request changes

blocker 7 件・非blocker 8 件。**7 件とも PM がコードで到達可能性を確認**した (行番号は §1 に転記)。

| # | 所見 | 確認 | 対応 |
|---|---|---|---|
| 1 | 実害 1 は同名 2 つを必要としない。カーネルが `/bin` `/sbin` `/usr/bin` を自分で探すので、1 コピーでも 2 回走る | `exec/exec.c:1249-1265` と `include/config.h:36` で確認 | §1 事実 2 に書き直し、受入 S2b を「1 コピーで回数 1」に |
| 2 | `exit(-2)` と fault は `exec_exit` の中で区別できない | `exec/exec.c:1001-1013` で確認 | 種別は**呼び手**が渡す (§2-1)。`ABORTED` も分ける。S1 / S1b |
| 3 | 起動失敗の早期 return は `exec_exit` を通らず、前回の記録が残る | `exec_launch` の早期 return 13 か所を確認 | `exec_run` の全 return 点で書く (§2-1)。S2 |
| 4 | `try_exec` は両ビルド共通。`sh.bin` がカーネルの記録を読むと他人の子の結果を拾う | `shell.h:114` と `sh_launch.inc` で確認 | 起動口を `sh_exec_result` に統一 (§2-2)。SHA-256 の注記も書き換え |
| 5 | `exit [N]` が既存の `exit` 2 つ (と rshell) と衝突する。表は先勝ち | `cmd_base.c:168`、`cmd_script.c:173`、`rshell.c:143`、`main.c:290` で確認 | 登録を `#ifdef` で分ける (§2-5)。S6 / S6b |
| 6 | `set -e` は `cmd_set` の「値の表示」分岐に食われる | `cmd_env.c:211-219` で確認 | `cmd_set` の先頭で拾う (§2-5)。S5 |
| 7 | handler に届かない行の `$?` が未定義で `set -e` がすり抜ける | `main.c` の 6 経路を確認 | 表を追加 (§2-3)。S4 / S5 |
| 8 | 実害 2 (`exit(2..5)` と park) は `exec_run` では到達不能 | `exec/appslot.c:295-302` で確認 | §1 事実 10 に訂正。S4 (旧) を削除し表の項目だけに |
| 9〜15 | `OS32_ERR_FULL` の写像、`INVALID` の扱いの変更、`exec` 組み込み、`time`/`if`/パイプの戻り値、`$?` の展開位置、行番号の意味、試験基盤 (`sh_exec.inc` への切り出し、スタブ、`-Werror=return-type`) | 妥当 | §2-1 / §2-3 / §2-4 / §2-5 / §3 / §4 に反映 |

### 往復 2 — 設計レビュー (Codex、`7a47486` 対象、2026-09-16) — Request changes

往復 1 の 7 件は「1 件を除いて閉じた」と確認された (残りは所見 2 のパイプ内 `exit`)。
新しい blocker 5 件はすべて**入力を黙って捨てる経路**で、PM がコードで確認した (§1 事実 14):

| # | 所見 | 対応 |
|---|---|---|
| 1 | `INVALID` を見た後に候補が尽きると 127 になる (126 であるべき) | 走査中に `INVALID` を見たかを覚える (§2-3)。S8 |
| 2 | 常駐側のパイプ段ループには `exit` の打ち切りが無い (`sh_exit_flag` は `SHELL_AS_APP` だけ) | 終了要求と終了値を別変数にし、常駐でも段の間で見る。明示の `exit` が「最後の段」より優先 (§2-3)。S9 |
| 3 | `script_load` が 255 文字超の行を切り詰め、128 行超を捨てて成功を返す | 捨てたらスクリプトを実行しない (§2-3)。S10 |
| 4 | `try_exec` / `exec` / `time` がコマンド行を切り詰めたまま起動する | 起動する前に 2 (§2-3)。S11 |
| 5 | `split_pipeline` が 9 段目以降と空の段を捨てる | 行全体を 2 で拒否 (§2-3)。S12 |
| 6 | 既存のホスト試験はスタブが `exit` を直接認識していて、本物の登録・伝播が壊れていても緑 | 実物の登録表を通す (§4 R2) |
| 7 | 行番号の定義がラベル・`goto` と合わない | 「保持された行の位置」と定義 (§2-5) |

観点の確認で分かった配線: `exec_exit` の直接の呼び手は 3 つで CPL=0 復帰も `EXITED`、`exec_kill_one` は
`exec_exit` を通らない、CTRL+STOP は `ring3_fault_kill` 経由なので `ABORTED` の配線が要る (§1 事実 15)。

### 往復 3 — 設計レビュー (Codex、`6f3ea0c` 対象、2026-09-16) — Request changes

往復 2 の 7 件は「設計上閉じた」。新しい blocker 4 件はすべて**同じ系統 (入力の欠落)** で、
PM がコードで確認した (§1 事実 14 に追記):

| # | 所見 | 対応 |
|---|---|---|
| B1 | `sh.bin` の要求表は 256 バイト。256〜511 バイトの行は `launch_req` が `INVAL` を返し、今は「GUI 外」と読む | 送信前に上限を検査して 2 (§2-3) |
| B2 | `run_cmd_internal` がコマンド名を 251 バイトで切るので、**別のファイルが起動し得る**。`try_exec` の検査より前 | 名前・拡張子付加・PATH 連結まで検査対象に (§2-3) |
| B3 | `glob_cb` の `mem_alloc` 失敗が印を残さず、**一致の一部だけ**を渡して成功する | 行全体を 2 で拒否 (§2-3) |
| B4 | `rshell` が 126 文字で切って接頭辞を実行する | 行末まで捨てて実行しない (§2-3) |

3 往復とも新しい経路が出続けたので (ROLES §5 の「往復数は指摘の集合で数える」)、**個別対応に加えて
入力経路の固定長バッファを 1 度全部洗う**要求を §2-3 に入れた。加えて、契約の未確定点を §2-5-1 に、
既存試験への影響を §4 の R1b / R1c に書いた。

### 往復 4 — 設計レビュー (Antigravity CLI `agy`、`0bb0f82` 対象、2026-09-16) — Request changes

blocker 4 件・非blocker 3 件。**また同じ系統** (入力の欠落) で、`if` の比較が 255 文字で切り詰められて
**条件が逆転し破壊的なコマンドが走る**という、`$?` とは独立の既存欠陥も出た。

**決着 (ユーザー決裁 2026-09-16)**: 票を 2 つに分ける。
切り詰めの修正は [`TASK_SH_TRUNCATION.md`](TASK_SH_TRUNCATION.md) (本票より先)。本票は終了コードの配線に絞る。
`agy` の所見 (T1〜T14 のうち T1 / T7 / T8 / T9 / T12 / T13 / T14) は新しい票の §1 に転記した。


### 往復 5 — 設計レビュー (ローカル `gemma4:31b`、`3e95ab1` 対象、2026-09-16) — **Approve**

切り詰め票 ([`TASK_SH_TRUNCATION.md`](TASK_SH_TRUNCATION.md)) の実装・実機受入が済み、**本票の前提が解けた**
状態で最終レビューを実施。

**前提の整合性を確認**: 断りの印 (`sh_refused_flag`) と `$?` は役割が分かれている。前者は
「実行そのものを拒否したか」の制御信号、後者はその結果としての状態値。**二重管理ではない**。

blocker 0。実装時の注意が 2 件:

| # | 所見 | 対応 |
|---|---|---|
| 1 | `exit 3 \| echo tail` で、終了要求を立てる前に `g_last_status` を書かないと `$?` が 0 になる | **実装の条件に入れる**: `exit` の handler は値を書いてから要求を立てる。段ループを抜けた後もその値を保持する |
| 2 | `$?x` が `0x` に展開される (将来 `eval` 等を入れると 16 進と誤認され得る) | 受入 S7 に「展開後が数字で始まる場合の形」を明記する。**現状のシェルでは無害** |

確認した範囲: 前提の整合性、`exec_last_result` の契約 (全 return 点での書き込み、記録なしの扱い)、
`sh_exec_result` による起動口の統一、組み込み handler の `int` 化、`$?` の展開、`exit` と `set -e` の
登録の分け方、PATH 走査の 126 / 127 の使い分け。

**この票は設計凍結。実装へ進む。**


## 8. ゲスト受入 (PM、2026-09-16、`e047eb8`)

NP21/W 停止 → `nhd-pull` → `os32-cycle deploy` → 起動。`ver` の `API: v55`、配備サイズ一致。

| 反例 | 結果 |
|---|---|
| `echo ok` の後の `$?` | `0` |
| `cd /nonexistent` の後の `$?` | `1` |
| 存在しないコマンドの後の `$?` | `127` |
| `set -e` 無しのスクリプト (2 行目が失敗) | 3 行とも走る |
| `set -e` 有りのスクリプト | `LINE1` の後で止まり **`LINE3` を実行しない**。`script: line 3: status 1` を表示。`source` は 1 |
| `exit 3` を含むスクリプト | `A` を出して打ち切り、`$?` は 3 |

未実施: S2b (1 コピーでの実行回数 1) は `t_exit` を配備してから。CTRL+STOP の 130、
GUI 端末 (`sh.bin`) 側の `$?`。

### 着地でのしくじり (記録)

最初の着地 (`44a685b`) は `git apply --3way` の失敗を見落とし、**新規ファイルだけが入った状態**で
コミットしていた。KAPI の追加も本体の実装も欠け、試験が `build/sdk.mk` に登録されていないので
`make check` が素通りした。実機で `ver` が旧版 (v54) を表示して発覚。`0eba40f` で戻し、
基点の上に成果を復元してから `git merge` で合流し直した。
**教訓: パッチ当ては「衝突が無い」ことと「当たった」ことが別。当てた後に中身の存在を確かめる**
(今回なら `sdk/kapi.json` の版数と関数名を見れば 1 秒で分かった)。
