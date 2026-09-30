# T9 — shell script: 常駐シェルの内蔵コマンドとスクリプトを端末から (設計草案)

> 発行: PM (2026-09-12) / 状態: **受入完了 (2026-09-13)**

独立レビュー通過 (第 6 版、Codex 3 往復目で Approve、2026-09-12、§10)。non-blocker 4 件は実装要件に取り込み済み。K / ビルド系 → S / W / A の順で発注。
親: [PLAN.md](../../tasks/gui/v13/PLAN.md) §1 (決裁 B: … → T7 → T8 → **shell script** → 設定 S0〜)。
前提: K6C / K7 / T7 / T8 (端末、con_sink、kbd 待ちと poll の park、全画面)。すべて main `a9aa0e4`。

## 0. いま起きること (確認済み)

- `ls` `cat` `cd` `env` `run` (スクリプト) 等の内蔵コマンドは **常駐シェル `shell.bin` の中の C 関数** (`userland/shell/*.c`、
  `execute_command()` `main.c:658`、表 `ShellCmd`)。GUI 中は `os32gui` が常駐シェルを終わらせ、起動ループが gshell を同じ
  シェル帯 0x300000 に載せる (`cmd_sys.c:225`) ので、**GUI 中に常駐シェルは存在しない**。端末 (T7) が起動できるのは
  `/usr/bin` `/bin` の外部バイナリだけで、内蔵コマンドは打てない。
- `shell.bin` は他の外部プログラムと**同じソース・同じフラグ** (`PROGRAM_FLAGS`、crt0) で、違いはリンカスクリプト
  (`sdk/link/app_sys.ld` = 0x300000 帯) だけ (`build/programs.mk:36`)。65,680 B。
- 制約 (K5b D9-8、`exec/exec.c:1569,1652`): CPL=3 アプリからの `exec_run` は「子が終わるまで塞ぐ」入れ子で、
  **その子は park できない** (単一カーネルスタック、`ring3_park_reject_count`)。GUI 中に入れ子の子が `kbd_getchar` を
  呼ぶと `hlt` に落ちて協調型全体が止まる (T8-3 K の実装メモでも実測)。`exec_start` (塞がない起動) は owner 1 (WM) 専用。
- `exec_app_state(id)` (KAPI v44) は所有者制限なしで状態 (0 FREE / 1 RUNNING / 2 PARKED / 3 WAIT_KEY / 4 WAIT_POLL) を返す。

## 1. 設計 (PM 案、レビュー対象)

**方針: 常駐シェルのソースをそのまま CPL=3 の外部アプリ `sh.bin` としてもビルドし、端末から `sh` で起動する。**
内蔵コマンド・環境変数・スクリプト (`run`) は同じコードが動く。外部プログラムだけは入れ子 `exec_run` を使えないので、
**端末 (WM) に起動してもらい、終わるまで譲りながら待つ**。

| # | 決定 | 担当 |
|---|---|---|
| D1 | `build/programs.mk` に `userland/sh.bin` を足す: `SHELL_SRC` + crt0 を `sdk/link/app.ld` (0x500000) でリンク、`-DSHELL_AS_APP`、**`.o` は `userland/shell/sh_obj/` 等の専用出力先** (常駐の `SHELL_OBJ` と混ぜない)。`build/app.conf` に `userland/sh` (**KAPI 49**、heap は既定、4 列目 `launcher`)、端末 `t5a_display` も `launcher` + 49、gshell も 49。`userland/deploy.yaml` に `/bin/sh.bin`。`shell.bin` (常駐) は無変更で同じソース — **受入 S7 で `shell.bin` の SHA-256 が変更前後で一致**すること (サイズ一致では不十分) | ビルド系 |
| D2 | `SHELL_AS_APP` の条件分岐 (`userland/shell/*.c`): (a) `shell_rshell_init()` と **`shell_run()` 内の自動 `serial_init`** (`ui.c:485`) を呼ばない (シリアル / `rshell_active` のタイムアウトを触らない)、(b) `os32gui` `rshell` `filer` (TUI、カーソル位置依存) は `sh: cui only` で拒否、(c) 履歴ファイルは `~/.sh_history` 相当の別名 (常駐の履歴を壊さない)、(d) `exit` で `shell_run()` を抜けて 0 で終了、(e) プロンプトは `sh> ` (どちらで打っているか分かるように) | S (シェル) |
| D3 | **外部プログラムの起動はカーネル仲介の明示プロトコル** (KAPI v49、末尾追記 **8 本**、ABI は §1a)。**要求表は要求者 ID ごとに 1 本 (ID 2〜5 の 4 本)**。表の欄は **配送状態と子の所有を分ける**: `phase` (IDLE / PENDING / TAKEN / RUNNING / DONE / FAILED)、`kind` (LAUNCH / KILL)、`child` (所有する子 ID、0 = 無し。**取消や退場の途中でも消さない**)、`token` (32bit、全体で単調増加)、cmdline。(1) `launch_req(cmdline)`: 宣言 `LAUNCHER` を持つ CPL=3 だけ、要求者は `res_owner_get()` で記録、GUI 外や入れ子 `exec_run` の子からは `OS32_ERR_INVAL`、自分の表が IDLE でなければ `OS32_ERR_FULL`。(2) `launch_pending()`: 誰でも、`PENDING` の要求数。**gshell は `should_park` の (a) に `launch_pending() > 0` を足す** (K5b の `launch_pending` と同列)。(3) `launch_take(...)`: owner 1 専用、top-level でだけ。`kind` = LAUNCH (cmdline) / KILL (`arg` = 畳む ID)。`PENDING` → `TAKEN`。(4) `launch_report(token, rc)`: owner 1 専用。LAUNCH: `rc > 0` → `child = rc`、`RUNNING`; `rc == 0` → `DONE`; `rc < 0` → `FAILED(rc)`。KILL: WM が `exec_kill(arg)` を終えてから呼ぶ (`rc` は無視) — 表は **`child` の回収通知で** `DONE` になる (`launch_report` は取得済みの印を消すだけ)。(5) `launch_poll(token, &status)`: 要求者だけ。(6) `launch_cancel(token)`: 要求者だけ、`RUNNING` のとき `kind = KILL(child)`、`phase = PENDING` (child は保持)。(7) `launch_child(id) -> child / 0`: **誰でも**、その ID の表が所有する子 (連鎖の次)。WM が CTRL+STOP の宛先を末尾へ解決するのに使う (D8)。(8) `sys_yield` (D5)。**回収通知**: `exec_reclaim_owned(x)` の中で (a) `child == x` の表を `DONE` に (phase が PENDING / TAKEN の KILL 途中でも)、(b) `requester == x` の表 (要求者の退場) は **`child != 0` なら `kind = KILL(child)`、`phase = PENDING`、requester を「孤児回収」印に**して残す (WM の top-level が `launch_take` で受けて畳む。孤児の子は WM が畳むまで生きているが、要求者 ID は再利用されうるので **表の所有は requester ではなく token で照合**し、再利用 ID からの `launch_req` は「孤児回収が完了するまで `OS32_ERR_FULL`」)。解放済み AppSlot の欄は読まない (通知は ID だけ) | K + W |
| D3a | **`sh` 側** (`SHELL_AS_APP` 時): 外部プログラムへ至る経路は **`try_exec()` (`main.c:296`) だけでなく内蔵 `exec` (`cmd_mnt.c:59`) と `filer` の起動 (`cmd_filer.c:314`) も `exec_run` を直接呼ぶ**ので、`exec_run` の呼び出しを 1 つの関数 `sh_launch(cmdline)` に集約し、`SHELL_AS_APP` では **`exec_run` を一切呼ばない** (`grep exec_run userland/shell` を受入 S7 で 0 件にする — `SHELL_AS_APP` のビルドで)。`sh_launch` = 候補パス解決 → `launch_req` → `sys_yield()` + `launch_poll()` の繰り返し → `DONE` で戻る / `FAILED(rc)` は `sh: <名>: launch failed (rc)`。待ちの間 `kbd_getchar` / `kbd_trygetchar` は呼ばない。`filer` は D2(b) で拒否 | S |
| D4 | **端末 (T7-A) も同じ要求表で起動する**: T7 の E3 の後、`session_launch` の代わりに **`launch_req(絶対パス + 引数)`** を出し token を保持して接続モードへ。100ms タイマで `launch_poll(token)` を読み、`RUNNING(id)` で子 ID を知り、**`DONE` でプロンプトへ戻る** (短命な子は最初の poll で `DONE`)。`FAILED(rc)` は `launch failed (rc)` を出してプロンプトへ。**接続モードの ESC は `launch_cancel(token)`** (子と、その要求表の孫まで D8 の連鎖で畳まれる) → `DONE` を待ってプロンプトへ (第 4 版までの「ESC で子を残したままプロンプト」は廃止。孤児と `ERR_FULL` の固着を防ぐ)。`EXIT` レコードは表示用にとどめモード判定には使わない | A |
| D5 | **KAPI v49 `sys_yield(void)`**: GUI 中は **必ず** `WAIT_POLL` に park する (tick 制限なし — 明示的な譲りは呼び手の意思。印は専用の `parked_from_yield`、resume は注入リングを読まず EAX = 0)。park できない文脈 (CPL=0、フレーム無し、入れ子 `exec_run` の子) では `hlt` 1 回して 0。CUI 中は `hlt` 1 回。カウンタ `ring3_yield_count`。**WM 側の公平性 (gshell `pick_poll`)**: `WAIT_POLL` 群は **ID 昇順の固定ではなく巡回** (前回起こした ID の次から)、かつ **同じ tick に同じアプリを 2 回起こさない** (WM が `get_tick` を控える。起こす相手が全員「この tick で起こし済み」なら `sys_halt` で次の tick を待つ) — sh の `sys_yield` と子の `kbd_trygetchar` が同時に `WAIT_POLL` でも tick ごとに交互に走り、飢餓しない。`kbd_trygetchar` の kernel 側 tick 制限は従来どおり (子は 1 tick 走り続けてから譲る) | K + W |
| D6 | スクリプト (`run file`): 各行は `execute_command()` なので内蔵はそのまま、外部行は D3 で端末経由。`$VAR` 展開・`if`/`goto` 等の既存機能はそのまま | — |
| D7 | 8MB では `sh` (段 2、数百ページ) + 子は入らない (T7 の 8MB と同じ、仕様どおり)。15MB 以上が対象 | — |
| D8 | **kill の連鎖と CTRL+STOP の宛先**: `exec_kill(id)` は **「id とその子孫 (要求表の `child` を末尾まで辿る) を末尾から回収」に固定**する (K)。WM は kill 後に `exec_app_state` で FREE になった ID を全部 `forget` する (子孫は WM の表からも消す)。**CTRL+STOP は WM 側で末尾へ解決**: `abort_target()` = フォーカス窓のアプリ (全画面なら所有者) から `launch_child()` を末尾まで辿った ID。`abort_targets_current(cur)` は **`abort_target() == cur` のときだけ真** — 端末 (cur) が子を持つなら偽になり、端末自身の syscall 出口では畳まれず、`redirect_abort(末尾)` → top-level で `exec_kill(末尾)` (末尾は子孫を持たないので 1 本だけ畳まれる)。`sh` はその `DONE` を受けて `sh> ` に戻る (S6)。もう 1 回 CTRL+STOP で次の末尾 (sh)、さらにもう 1 回で端末 | K + W |
| D9 | **接続モードの ESC = 取消**: 端末は `launch_cancel(token)` → WM が `exec_kill(child)` (子孫ごと) → 表は `child` の回収通知で `DONE` → 端末は poll で `DONE` を見てプロンプトへ。`DONE` / `FAILED` に対する `launch_cancel` は `OS32_ERR_STALE`、`PENDING` / `TAKEN` (起動がまだ WM に取られていない / 取られて exec_start 中) に対しては `OS32_ERR_AGAIN` で端末は次のタイマで再試行 | A |

メモリ: `sh.bin` ≈ 66KB + 既定ヒープ、要求表 4 本 (requester / child / phase / kind / token 32bit / cmdline 256B ≈ 1.1KB)、印 1 語 × 5。con_sink のレコード型は増やさない。KAPI v49 は **8 本**。

### 1a. KAPI v49 の ABI (K が実装、W / S / A が従う)

共通: 出力ポインタは NULL 可 (書かない)。失敗時は出力を書かない。CPL=3 のポインタは既存のディスパッチャ検証。

| 名前 | 引数 | 戻り | 権限 | 規則 |
|---|---|---|---|---|
| `launch_req` | `const char *cmdline` | token (> 0) / 負 | 宣言 `LAUNCHER` を持つ CPL=3 | cmdline は NUL 終端 1〜255B (超過 / 空 → `OS32_ERR_INVAL`)。GUI 外 / 入れ子 `exec_run` の子から → `OS32_ERR_INVAL`。自分の表が IDLE でない (孤児回収中を含む) → `OS32_ERR_FULL`。**token = 32bit の全体単調増加カウンタ** (0 と負は使わない、上位 8bit に要求者 ID は入れない; `0x7FFFFFFF` に達したら `OS32_ERR_FULL` で拒否 = 事実上到達しない)。表は token で照合するので、周回や ID 再利用で取り違えない |
| `launch_pending` | — | `PENDING` の要求数 | 誰でも | 0 なら WM は何もしない |
| `launch_take` | `char *buf, u32 cap, i32 *requester, i32 *kind, i32 *arg` | token / 0 (無し) / 負 | owner 1 | `cap < 256` → `OS32_ERR_INVAL`。`kind` = 1 LAUNCH (buf に cmdline) / 2 KILL (`arg` = 畳む ID)。`PENDING` → `TAKEN`。要求者 ID 昇順 |
| `launch_report` | `i32 token, i32 rc` | 0 / 負 | owner 1 | `TAKEN` 以外 → `OS32_ERR_STALE`。LAUNCH: `rc > 0` は生きている非シェル ID でなければ `OS32_ERR_INVAL` → `child = rc`, `RUNNING`; `rc == 0` → `DONE`; `rc < 0` → `FAILED(rc)`。KILL: `rc` は無視、`TAKEN` の印だけ消す (`DONE` は `child` の回収通知で) |
| `launch_poll` | `i32 token, i32 *status` | 0 / 負 | 要求者 (表の requester と一致) | `status`: `0` PENDING / `1` TAKEN / `0x100 + child` RUNNING / `0x200` DONE / `0x300 + (-rc)` FAILED。`DONE` / `FAILED` を返した時点で表を IDLE に (再 poll は `OS32_ERR_STALE`) |
| `launch_cancel` | `i32 token` | 0 / 負 | 要求者 | `RUNNING` → `kind = KILL(child)`, `PENDING`; `PENDING` / `TAKEN` → `OS32_ERR_AGAIN`; `DONE` / `FAILED` / 不一致 → `OS32_ERR_STALE` |
| `launch_child` | `i32 id` | 子 ID / 0 | 誰でも | その ID の表の `child` (phase を問わず)。不正 ID → 0 |
| `sys_yield` | — | 0 | CPL=3 | D5。park できない文脈では `hlt` 1 回。resume 後に印 `parked_from_yield` を消す |

回収通知: `exec_reclaim_owned(x)` の中 (K6C の `con_sink_owner_exit` と同じ位置。**正常終了は AppSlot 解放の後、`exec_kill` は前**に通るので、通知は ID だけを使い AppSlot の欄を読まない) で、`child == x` の表を `DONE` に、`requester == x` の表を孤児回収 (`KILL(child)`, `PENDING`) に。

## 2. 受入 (ゲスト、PM / テスター)

| ID | 試験 | 合格条件 |
|---|---|---|
| S1 | 内蔵 | 端末で `sh` → `sh> ` → `ls /` `cat /etc/system.cfg` `cd /usr` `pwd` `env` の出力が端末に出る |
| S2 | 外部 | `sh> kbd_echo` → 子が端末経由で起動し打鍵が届き、`q` で戻ると **`sh> ` に戻る** (`sh` は待っている間 `WAIT_POLL` で譲り、子は `WAIT_KEY`) |
| S3 | スクリプト | `source /test/hello.sh` (`userland/tests/data/hello.sh`、内蔵 `echo` `pwd` `env` + 外部 `klibc_test` の混在) が最後まで流れ `script done` で `sh> ` に戻る |
| S4 | 拒否 | `sh> os32gui` / `filer` / `rshell` → `sh: cui only` |
| S5 | 終了 | `exit` で `sh` が終わり端末のプロンプト `> ` に戻る。接続モードの ESC は `launch_cancel` で sh (と孫) を畳んでからプロンプトへ (D9)。その後の起動要求が `ERR_FULL` にならない |
| S6 | CTRL+STOP | 端末にフォーカスがある状態で子が走っている最中に CTRL+STOP → 連鎖の末尾 (子) だけが畳まれ `sh> ` に戻る (D8)。もう 1 回で sh が畳まれ端末のプロンプトへ |
| S7 | 回帰 | regress 6 本、CUI の常駐 `shell.bin` は変更前後で**同一** — `cmd_ver` が `__DATE__` / `__TIME__` を埋めるため SHA-256 は再現しないので、同一フラグの `SHELL_OBJ` 12 本のうち `cmd_base.o` 以外がバイト一致し、`cmd_base.o` の差が `__TIME__` 文字列だけであること (S の実装メモ、着地時に PM が clean build で再確認)。`SHELL_AS_APP` ビルドの `exec_run` 参照 0 件、Start → CUI mode (sh と子が生きていても D8 で畳まれる) |

## 3. レビューで見てほしい点 (第 6 版)

1. **D8**: `exec_kill(id)` = id と子孫の回収に固定し、CTRL+STOP は WM が `launch_child` で末尾を解決して `exec_kill(末尾)` (1 本だけ)。`abort_targets_current` は末尾 == cur のときだけ真。
2. **D3 の表**: 配送状態 (`phase` / `kind`) と子の所有 (`child`) を分け、取消・退場の途中でも `child` を保持。要求者の退場は「孤児回収」として WM の top-level が畳む (カーネルは回収文脈から kill しない)。
3. **token**: 32bit 全体単調増加、表は token で照合、枯渇は拒否。
4. **D9**: `launch_cancel` の `AGAIN` / `STALE` の規則と端末の再試行。
5. §1a の残り (NULL 出力、失敗時の出力なし、回収通知の順序)。

## 5. 独立レビュー第 1 版 (2026-09-12) — Request changes

- blocker 1: 第 1 版 D3 の「`exec_app_state(2..5)` の走査で子を追う」は、`exec_start` が park 前に終了した子を `0` で返し ID を回収済みにする既存契約のため、**短命な子を取り逃がして 2 秒後に `launch failed` になる**。→ 第 2 版: WM が `launch_report(rc)` で結果を明示的に返し、`rc == 0` は即 `DONE` (D3)。
- blocker 2: 第 1 版 D3 の「PRINT に `\x1e LAUNCH`」は con_sink のレコードに送信元 ID が無く、**任意のアプリが端末経由の非同期起動を使えてしまう**。→ 第 2 版: カーネルの要求表が `res_owner_get()` で要求者を記録し、宣言 `LAUNCHER` を持つアプリだけ受け付ける (D3)。端末は起動要求を解釈しない (D4)。
- 二重ビルドと `sys_yield` の方向性に blocker なし。

## 6. 独立レビュー第 2 版 (2026-09-12) — Request changes

- blocker 1: 第 2 版 D5 の `sys_yield` が `WAIT_POLL` の印をそのまま使うと、`exec_resume` が `parked_from_poll` の再開で `kbd_inject_take()` を行うため、**sh が譲っている間に子向けの打鍵が入ると sh がその 1 バイトを吸って捨てる**。→ 第 3 版: 専用の印 `parked_from_yield` を足し、resume で注入リングを読まない (D5)。
- blocker 2: 第 2 版 D4 の「自分以外に生存アプリがあれば接続維持」は、端末 + sh + GUI アプリ A のとき **sh が exit しても A が生きていてプロンプトへ戻れない**。→ 第 3 版: カーネルが `START id` レコードを積み、端末は自分が起動した sh の ID を確定して、その ID の `EXIT` だけで戻る (D4)。
- 前回の 2 件 (短命な子、任意アプリの起動) は `launch_report(rc)` と `LAUNCHER` + 要求表で解消と判定。

## 4. 範囲外

- 設定レジストリ (S0〜)。端末の複数化。`filer` の端末化 (カーソル位置レコードの解釈)。
- 配備・コミット・push・エミュレータ・ローカル AI・ini・.env・`make` は禁止 (コーダー)。

## 7. 独立レビュー第 3 版 (2026-09-12) — Request changes

- blocker: 第 3 版 D4 の `START` レコードは con_sink の drop-oldest リングに載るため、**子が `exec_start` の戻り前に 8KB 超を出力すると `START` が捨てられ、端末は子 ID を永久に確定できない** (短命な大量出力なら `EXIT` だけ見える)。→ 第 4 版: `START` を廃止し、端末も要求表 (`launch_req` / `launch_poll`) で子 ID と `DONE` を問い合わせる (D3 の表を要求者 ID ごとに 4 本へ、D4)。制御情報はリングに載せない。
- `parked_from_yield` と「sh 自身の ID を追う」方向は妥当と判定。

## 8. Codex レビュー 第 4 版 (2026-09-12、`codex exec -s read-only`、gpt-6-astra) — Request changes (往復 1/3)

blocker 5 件はいずれも PM が実コードで到達可能と確認:
1. 要求を WM top-level へ渡す経路が無い (`should_park` に起動要求が無い、`launch_take` は owner 1 専用) → D3 に `launch_pending()` と `should_park` の理由を追加。
2. `WAIT_POLL` 群で `pick_poll` が ID 昇順固定のため sh が子を飢餓させる → D5: `sys_yield` は常に park、WM は巡回 + 同一 tick 1 回。
3. 内蔵 `exec` (`cmd_mnt.c:59`) と `filer` (`cmd_filer.c:314`) が `exec_run` を直接呼ぶ → D3a: `exec_run` 呼び出しを `sh_launch` に集約し `SHELL_AS_APP` では 0 件。
4. CTRL+STOP がフォーカス窓 (端末) を畳み sh と子が孤児になる → D8: カーネル側で要求表の連鎖の末尾へ転送。
5. 接続モードの ESC で sh が残り要求表が `RUNNING` のまま固着 → D9: ESC = `launch_cancel`、`DONE` を待ってプロンプトへ。

non-blocker 3 件も反映: sh 専用の `.o` 出力先とハッシュ比較 (D1、S7)、`shell_run()` の自動 `serial_init` も `SHELL_AS_APP` で外す (D2a)、ABI 表 (§1a)、sh / 端末 / gshell の要求版を v49 に (D1)。宣言 `LAUNCHER` は認証ではなく協調的な宣言であり、`session_launch` (GUI アプリの経路) はそのまま — と明記。

## 9. Codex レビュー 第 5 版 (2026-09-12) — Request changes (往復 2/3)

blocker 4 件、いずれも PM が妥当と判断して第 6 版に反映:
1. CTRL+STOP が端末の OP_WAIT 内で `abort_targets_current` 真 → 端末自身が畳まれ、カーネル側の転送 (第 5 版 D8) に到達しない → **WM 側で末尾を解決** (`launch_child`、`abort_targets_current` は末尾 == cur のときだけ)。
2. 「id と子孫を回収」と「末尾だけ停止」を同じ `exec_kill` に割り当てると WM の `forget` と CANCEL の `DONE` が壊れる → `exec_kill` は子孫ごとに固定、CTRL+STOP は末尾 1 本、kill 後は FREE を走査して forget。
3. 取消で `RUNNING(child)` から外れると要求者退場時に孤児 → `child` を配送状態と分けて保持、退場は孤児回収 (KILL) を WM に渡す。
4. 8bit 世代の token が 255 回で周回 → 32bit 全体単調増加、token で照合、枯渇は拒否。

non-blocker: KAPI は 8 本に統一、NULL 出力と失敗時の規則、回収通知は ID だけ (正常終了は AppSlot 解放後、kill は前)、clean build 双方で SHA-256 比較。

## 10. Codex レビュー 第 6 版 (2026-09-12) — **Approve** (往復 3/3)

blocker なし。non-blocker 4 件は実装要件として各票に入れる:
1. **要求表の回収完了処理** (K): 子の回収通知で `DONE` と同時に `child = 0`。孤児回収 (要求者退場) の完了時は poll を待たず `IDLE`。子を持たない要求者の退場も表を解放。再利用 ID への誤連鎖と `ERR_FULL` 固着を防ぐ。
2. **KILL 後の `launch_report`** (K / W): `launch_take` → `exec_kill(child)` → 回収通知で `DONE` → `launch_report` の順になると `TAKEN` でないので `STALE` を返す。WM はこれを再試行せず正常として扱い、カーネルは回収通知で取得済み情報も掃除する (ABI は変えない)。
3. **sh の行入力** (S): `shell_run` の `ime_getkey` (FEP を通る) ではなく、`SHELL_AS_APP` では **`kbd_getkey` / `kbd_getchar` の注入入力に統一** (FEP 確定は gshell 側で済んでいる。UTF-8 の後続バイトも同じ経路)。二重処理の疑いを消す。
4. **WM の新しい起動口** (W): `launch_take` の LAUNCH は `run_program` (`begin_start` / `end_start`、全画面判定・復帰) を通す。`exec_start` の直呼びはしない。

## 11. 実装メモ (ビルド系、D1 / D1a、2026-09-12)

- `build/programs.mk`: `SH_OBJDIR = userland/shell/sh_obj`、`SH_OBJ` を `-DSHELL_AS_APP` でそこへ吐き、`userland/sh.elf` は `PROGRAM_LDFLAGS` (= `app.ld` 0x500000) + `FILER_DRAW_OBJ` + `-los32save`。常駐の `SHELL_OBJ` / `userland/shell.elf` は 1 文字も変えていない (S7 の SHA-256 一致の根拠)。
- 4 列目 `launcher` → `--launcher` を `userland/%.bin` レシピに追加。`sdk/mkos32x.py` に `OS32X_FLAG_LAUNCHER = 0x0020` と `--launcher` (票 K と重複したら同内容なので片方を捨てる)。
- `build/app.conf`: `userland/sh 49 0 launcher` / `t5a_display 49 0 launcher` / `gshell 49`。`tools/check_manifests.py` は `launcher` を書式として許すだけ (呼び出しとの突き合わせは KAPI v49 着地後)。
- `programs:` に `sh`、`clean-programs` で `sh_obj/` と `userland/sh.{elf,raw,bin}` を掃除。`userland/deploy.yaml` に `/bin/sh.bin` (tags `programs`)。
- 未実施: 実ビルド・配備・実機 ([V4])。検証は `make -n sh` / `make -n programs` の dry-run と `check_gfx_flag()` / `check_constraints.py` の直接実行まで。`make check-manifests` の §2 は `userland/sh.bin` が実在してから通る。

## 12. 実装メモ (K、2026-09-12、着地 feat/gui)

- 要求表は `exec/launch.c` + `include/launch.h` (添字 = 要求者 ID、照合は token)。ワイヤ側の定数
  (`LAUNCH_KIND_*` / `LAUNCH_ST_*` / `LAUNCH_CMDLINE_MAX` / `LAUNCH_TOKEN_MAX`) と
  `OS32X_FLAG_LAUNCHER 0x0020` は `sdk/include/os32/os32_kapi_shared.h` が正典 ([C4])。
- KAPI v49 = 8 本、スロット 193〜200 (`0x30C`〜`0x328`)。`sys_yield` の実体は `exec_sys_yield`。
  データフィールドは `0x32C` / `0x330` へ移った (`docs/KAPI_SPEC.md` の v49 節が正典)。
- D8: `exec_kill` は `launch_chain()` で末尾まで辿り、**末尾から** `exec_kill_one` を回す。
  CTRL+STOP は WM が `launch_child()` で末尾を解決して渡す (末尾は 1 本だけ畳まれる)。
- D5: 第 4 の park 点。印 `parked_from_yield`、状態は `WAIT_POLL` のまま、間引きなし。
  「印 → EAX の出所」の対応表は `appslot_resume_source()` に閉じた (`exec_resume` は分岐するだけ)。
- §10 non-blocker 1 / 2 は実装済み: 回収通知で `DONE` + `child = 0`、孤児の完了は `IDLE`、
  子を持たない要求者の退場も解放、KILL 後の `launch_report` は `STALE` (WM は正常扱い)。
- ホスト TDD: `tools/tests/test_launch.py` (新規、`make check-launch-host`) と
  `test_multiapp_impl.py` ケース 23。記録は `tools/tests/t9_tdd.md`。kselftest に 3 項。
- **Codex 実装レビュー 往復 1/3 の blocker 3 件を修正** (2026-09-13、いずれも §1a からの逸脱):
  (1) `launch_take` は `buf == NULL` を断らない — 共通契約「出力ポインタは NULL 可」に従い
  cmdline のコピーだけ飛ばす (`cap` を見るのは `buf` が非 NULL のときだけ)。
  (2) `launch_cancel` の要求者不一致は `INVAL` ではなく **`OS32_ERR_STALE`** (§1a「不一致 → STALE」)。
  `AGAIN` だけが再試行の合図、という端末側 (D9) の読み分けを壊さないため。
  (3) `launch_report(KILL)` が `TAKEN` のまま来ても `child` を落とさず、印だけ消して `RUNNING` へ
  戻す。`DONE` を付けるのは常に回収通知 — 落とすと「生きている子の所有が表から消え、以後の
  退場でも回収されない」孤児ができた。試験は `2b2`/`2b3`・`4c`・`5h2` とケース 9 (16 検査) を追加。
- **Codex 網羅レビュー 往復 7 の blocker R1 を修正** (2026-09-13): `sys_getcwd` が返していた
  `fs/vfs.c` の static `cwd` は**カーネル帯 = USER ビット無し** (`kernel/paging.c` は
  `phys | PAGE_RW` で張る。RO+USER なのは KAPI トランポリンページの 1 枚だけ) なので、
  CPL=3 の `cd` / `pwd` / `apps/edit` が戻り値を読むと #PF → fault kill になっていた。
  → `sdk/kapi.json` の target を `vfs_cwd` → **`vfs_cwd_user`** (`exec/exec.c`) に差し替え、
  `ring3_in_syscall` なら**トランポリンページの空き** (表 + スタブの後ろ、`RING3_USTR_OFF`、
  `exec/ring3_str.h`) へ写してそのポインタを返す。CPL=0 の呼び手には従来どおり static `cwd`。
  **スロット・引数・戻り型は不変なので [ABI2] の範囲内、KAPI の版は上げていない** — 外から
  見える約束が 1 つも動かず、上げると既存バイナリの `min_api_ver` が一斉に足りなくなるため。
  写しは呼ばれるたびに上書き (`docs/KAPI_SPEC.md` の関数表の前に注記)。写し場がページに
  収まることは `STATIC_ASSERT` で固定 (KAPI が増えたらビルドが落ちる)。
  試験: `tools/tests/test_ring3_str.py` (新規、`make check-ring3-str-host`) 4 ケース 20 検査と、
  `kselftest_run_post_exec()` の 3 項 (`kselftest_run()` は `exec_init()` より前に走るため別口)。
- **Codex 網羅レビュー 往復 8 の blocker T1 を修正** (2026-09-13): 標準 FD のリダイレクト表
  (`fs/fd_redirect.c`) は FD 0/1/2 の 3 本しかなく**全アプリ共有**で、park/resume でも
  切り替わらなかった。park してある sh の `> /tmp/out` が生きたままなので、WM の Start → Run で
  起動した別アプリの `printf` がそこへ入り (反例 1)、パイプ中なら stdout が sh の .bss
  (sh の**仮想**番地) なので別アプリの `sys_write(1)` がその番地を**別 CR3 で解決して書く**
  (反例 2)。→ 表を **ID ごとの枠** (`exec/appslot.c` の `g_redir[APP_SLOT_COUNT]`、504 B) にし、
  park (4 か所) で走っていた ID の枠へ**移し**、resume で戻す。ID 1 (WM / 常駐シェル) の枠も
  同じ表に置く。持ち替えは**コピーではなく移動** — 枠と「いまの表」の両方に同じ欄が残ると、
  次のアプリが同じ FD を張ったときの `fd_redirect_reset` が**他人のファイルを閉じる**
  (`fd_redirect_save` / `_restore` / `_clear_state` を `fs/fd_redirect.c` に追加)。
  park したまま畳まれた ID のリダイレクトは「いまの表」に無いが、その `file_fd` は
  `fd_redirect_to_file` の `vfs_open` が**その ID の owner タグ**で取ったものなので、
  `exec_reclaim_owned` の (2) `vfs_close_owned(id)` が閉じる。`appslot_reclaim` は枠を
  **空にするだけ** (`fd_redirect_clear_state`)。試験は `test_multiapp_impl.py` ケース 24。
- **Codex 網羅レビュー 往復 9 の non-blocker を修正** (2026-09-13): 上の初版は
  `appslot_reclaim` が `fd_redirect_close_state()` で枠から閉じており、その後
  `vfs_close_owned(id)` が同じ `file_fd` にもう一度 `vfs_close` を掛けていた (いまの
  `vfs_close` と回収順では FD 再利用が挟まる反例は無いが、「二重 close なし」の説明が
  不正確だった)。→ 枠は `fd_redirect_clear_state()` で空にするだけにし、閉じるのは
  `vfs_close_owned` 1 か所に寄せた (`fd_redirect_close_state` は削除)。ホスト試験の偽 VFS に
  **owner タグと FD ごとの `vfs_close` 呼び出し回数**を持たせ、`vfs_close_owned` の偽物を
  `ma_reclaim_res` の (2) に置いて `24B2` / `24J2`「`vfs_close` は 1 回だけ」で押さえた。
- **受入 S6 (CTRL+STOP の連鎖) の差し戻しを修正** (2026-09-13): 2 回目の CTRL+STOP で sh と
  端末が両方消えたのは、IRQ1 の `ring3_abort_request()` → `appslot_abort_request()` が
  **そのとき走っていた slot** に `abort_req` を立てる K2 の経路。T9 で sh が `WAIT_POLL` で
  毎 tick 回り端末も 100ms タイマで回るようになったため、IRQ1 が落ちた先は D8 の宛先
  (フォーカス窓の連鎖の末尾、WM が `launch_child` で解決) と**無関係なアプリ**になる。
  → **GUI 中 (`con_sink_is_enabled()`) はカーネルが立てない**。要求は raw リング経由で WM に
  届き、WM が `exec_abort_clear` → `exec_kill(末尾)` を実行する (決裁 A1 / D8) — つまり
  D8 の「宛先は WM が決める」を、IRQ1 の側でも守らせる形にした。判定は純関数
  `appslot_abort_admit(gui_mode, now_tick)` (`exec/appslot.c`) に閉じ、`exec.c` は
  `con_sink_is_enabled()` と `tick_count` を渡すだけ。
  **暴走の逃げ道は残す**: `AppSlot.last_kernel_tick` (下の補正で syscall 入口も起点に)
  から `APP_RUNAWAY_TICKS` (200 = 2 秒) 以上 WM へ戻っていなければ GUI 中でも立てる —
  KAPI を呼ばない計算ループは協調型で WM が制御を取り戻せない唯一のケース。
  CUI (K2) は 1 バイトも変えていない。`ring3_abort_check()` と IRQ1 スタブは**触っていない** —
  gfx 拒否 (T8 D1a) と V86 の脱出は `appslot_abort_request()` の直呼びで、あちらは
  「WM / カーネルが宛先を決めた」kill なので GUI 中も従来どおり効く必要があるため
  (PM 案の「`ring3_abort_check` も GUI 中は何もしない」は D1a を壊すので採らなかった)。
  試験は `test_multiapp_impl.py` ケース 25 (25 検査) と kselftest 1 項 (4 検査)。
- **暴走判定の起点を syscall 入口へ** (2026-09-13、S6 合格後の補正): 上の初版は
  `last_resume_tick` を start / resume でしか更新しなかったので、GetMessage 型の GUI アプリ
  (端末) が WM の `op_wait` の**中**で待っている間は更新されず、**2 秒イベントを待っただけで
  「暴走」に見えた**。その状態で IRQ1 が端末の CPL=3 実行中 (100ms タイマの描画など) に落ちると、
  スタブの即 kill が **D8 の宛先 (連鎖の末尾) より先に端末を畳む** (`sh> ` で待っている状態の
  CTRL+STOP 1 回で到達しうる)。→ 欄を `last_kernel_tick` に改め、`ring3_syscall_dispatch` の
  入口でも更新する (`if (g_cur_app) g_cur_app->last_kernel_tick = tick_count;` の**代入 1 つ**、
  hot path なので関数呼び出しは足さない)。start / resume の更新は残す。これで
  「KAPI を呼ばない計算ループ」だけが `APP_RUNAWAY_TICKS` に掛かり、待っているアプリは
  常に最近カーネルへ入っているので対象外になる。`appslot_abort_admit` の意味は不変。
  試験は ケース 25 の (f) 6 検査 (`25z`〜`25F`)。
- **K5c の経路を戻す** (2026-09-13、S6 再試験の 3 回目が畳まれなかった件): 上の「GUI 中は
  IRQ1 由来を立てない」は広すぎた。`in_op_wait` (WM がそのアプリの `gui_call(OP_WAIT)` の
  **中**で回っている) のときは、割り込まれた文脈が CPL=0 (WM のコード) なので IRQ1 スタブの
  即 kill は起きず、要求は必ず WM のハンドラが見る — 本人宛なら break して syscall 出口の
  `ring3_abort_check` が畳み、別宛なら `exec_abort_clear` で降ろして `exec_kill(宛先)`
  (K5c / 決裁 A1)。塞ぐと連鎖の末尾が端末自身のとき誰も畳まず、素の GUI アプリを
  フォーカスして CTRL+STOP で閉じる K5b/K5c の挙動 (G テスト) も壊れる。
  → 許可条件を `!gui_mode || in_op_wait || 暴走` にした (`appslot_abort_admit` の 1 行)。
  塞いだままなのは「**アプリのコードが CPL=3 で実際に走っている最中**」だけで、そこは
  IRQ1 スタブが D8 の宛先より先に畳むので暴走のときしか立てない。試験は ケース 25 の (g)
  12 検査 (`25G`〜`25R`) と kselftest の 5 項目め。
- **未実施**: `make` (clean build / `check` 全体 / `external`)、配備、実機。`build/app.conf` は
  ビルド系レーンの担当なので触っていない (sh / 端末 / gshell の要求版 49 は未設定)。

### 12a. Codex 実装レビュー (K 側、2026-09-13、`codex exec -s read-only`) — 往復 1/3: Request changes → 修正済み

blocker 3 件 (すべて `exec/launch.c` の §1a 逸脱): (1) `launch_take` が `buf == NULL` を INVAL にしていた → NULL 可、(2) `launch_cancel` の要求者不一致が INVAL → STALE、(3) `launch_report(KILL)` が TAKEN のまま来ると `child = 0` + DONE にしていた → 印だけ消して RUNNING に戻す (child 保持、DONE は回収通知だけ)。
non-blocker: kill 連鎖の途中要素を飛ばす経路は正常系で到達しない、cmdline の NUL より先の未マップページは既存ディスパッチャと同じ扱い (呼び手の fault kill)、ホスト試験は `exec.c` の転記ハーネス。
`launch_poll` の不一致は INVAL のまま (§1a は cancel だけ STALE と規定)。

**往復 2/3 (`dc8405e`): Approve** — 3 件の修正を確認、新たな blocker なし。non-blocker は前回と同じ 3 点。
## 13. 実装メモ (S、2026-09-13)

- D2 は全て `#ifdef SHELL_AS_APP`: (a) `shell_rshell_init` と `shell_run` の `serial_init`+`rshell`、(b) `run_cmd_internal` の頭で `os32gui`/`rshell`/`filer` を `sh: cui only`、(c) `.sh_history` (`HIST_FILE_ROOM` も広げた — 旧 `max - 12` では 1 バイト溢れる)、(d) 内蔵 `exit` が `sh_exit_flag` を立て `shell_run` が抜ける、(e) `sh> `。§10-3 の行入力は `kbd_getkey` (`ime_getkey` と同じ u16 形式なので行編集の変換は不要)。
- D3a: `exec_run` は `shell.h` の `#define sh_launch(c) (g_api->exec_run(c))` **1 か所だけ**になり `grep exec_run userland/shell/*.c` は 0 件。`SHELL_AS_APP` の実体は `userland/shell/sh_launch.inc` (`.c` にすると wildcard の `SHELL_SRC` 経由で常駐にも空の `.o` が混ざる)。
- 票に無い判断 2 つ: ①`gfx_shutdown` を `sh_gfx_restore()` にして `SHELL_AS_APP` では**呼ばない** (所有者検査が無く、CPL=3 の sh が呼ぶと GUI の表示ごと止まる)。②`FAILED` が `NOT_FOUND`/`GENERAL` のときは印字しない (PATH 候補の数だけ同じ行が出る)。
- 常駐の回帰: `dee101b` の `userland/shell/` を同じフラグでコンパイルして `.o` を突合、12 本中 11 本バイト一致、`cmd_base.o` の差は `__TIME__` の 1〜2 バイトのみ。**`cmd_ver` が `__DATE__`/`__TIME__` を埋めるので `shell.bin` の SHA-256 はそもそも再現しない** — S7 はここを除いた比較に。
- 未実施: `make` 全般・リンク・配備・実機 ([V4])。通したのは両フラグの単体コンパイル、`check-sh-launch-host` (新規、`check`/`.PHONY` 登録、28 項目 ALL PASS、記録 `tools/tests/t9_tdd.md`)、`check_constraints.py`。
- 残る穴 (W/A へ): ①`try_exec` 2b は相対名 (`ls.bin`) を `launch_req` へ渡すので、`run_program` が要求者でなく WM の cwd で解決すると当たらない。②リダイレクト/パイプは sh 自身の FD に掛かるため `sh> ls > f` は外部コマンドに効かない。

### 13a. 実装レビュー (往復 1/3) の修正 (S、2026-09-13)

- blocker 1: `SHELL_AS_APP` の `redraw_line` を `userland/shell/sh_redraw.inc` に分け、**コンソール座標を一切引かない**形にした。いま出ている行の写し (`sh_drawn`) を持ち、純粋な延長なら差分バイトだけ、それ以外は `\n` + プロンプト + 行全体、行末より前は桁数ぶんの BS で戻す。`shell_run` が直接印字する 3 か所 (ASCII / UTF-8 追加、行末 BS) と候補一覧の後にも `sh_mark_drawn` / `sh_drop_drawn` で写しを合わせる (常駐では両マクロとも空)。
- blocker 2: `cmd_script.c` の `script_exec` が**各行の前に** `sh_exit_flag` を見て抜ける (`#ifdef SHELL_AS_APP`)。`goto` の巻き戻しでも毎行通り、ネストした `source` は内側から順に戻って `script_source_file` が各段で解放する。行ループ末尾の判定 (ui.c) はそのまま。→ §13 の「残る穴 ③」は解消。
- check-manifests: `docs/07_shell.md` の基本コマンド表に `exit` と「`sh.bin` のみ」の注記を追加 (§1c は「なし」)。non-blocker も対応 — GUI 外の `launch_req` INVAL は `sh: external programs need the GUI terminal` を**1 度だけ**出して次の PATH 候補へ回す (起動が通れば印を寝かせる)。UTF-8 の BS は既存問題として触っていない。
- 試験: `check-sh-shell-host` を新設 (`check` / `.PHONY` 登録、19 項目、RED → GREEN を `t9_tdd.md` に記録)。`check-sh-launch-host` は 36 項目へ増え、こちらも RED を採り直した。常駐の `.o` は 12 本中 11 本が `f753f2d` とバイト一致、`cmd_base.o` の差は `__TIME__` 1 バイトのみ。

### 13b. 実装レビュー (往復 2/3) の修正 (S、2026-09-13)

- blocker 1: `exit` の印を `shell_run` の行ループの**入口** (最初の `show_prompt` と `sh_getkey` の前) で見るようにし、ループ末尾の判定は外した。これで起動時の `/etc/profile` / `$HOME/.profile` 内の `exit` でも入力待ちに入らずそのまま終わる。
- (往復 9) **C-1 (必須、I4 が持ち込んだ回帰)**: 反復型 `wildcard_match` がパターン側の `*` を通常文字の比較より**後**に見ていたため、`*` と `*dest` で `*` 同士が通常文字として消費されて外れた (`cp SRC DIR/` + アスタリスクが `*dest` を展開から落とす)。`*` の判定を先頭に移した標準形へ。常駐にも入る。
- (往復 9) **I-1〜I-6 は継承の欠陥で常駐にも効くので別コミット (S7 の例外)**。I-1: `cmd_sys.c` の `IdeInfo` に `phys_sector_size` を足してカーネル定義 (96B) と一致させる (`ide 0` で `ide_identify` が 2 バイトはみ出していた)。I-2: `env_expand` が収まらなければ**負**を返し、呼び手は `sh: line too long after expansion` で行を捨てる。I-3: `fs_join_path` が溢れたら負を返し、`cp` / `mv` の 5 か所が `path too long` で中止する (黙って切り詰めて別の宛先を潰していた)。I-4: `cp -r` の収集表の名前幅を `OS32_MAX_PATH` に。1 段 16.6KB になるので**スタックではなくヒープ**から取る (常駐のスタックは 40KB、深さ 8 で溢れる)。上限超過は `cp -r: too many entries` で中止。I-5: `dd` のセクタ長を種別ごとに — `cd` 2048 / `hd` 512 (`SYS_HDD_SECTOR_SIZE` を追加) / `fd` ほか 1024。`dev_get_info` がセクタ長を返さないので**名前の先頭 2 文字**で分けている (KAPI が返すようになったらそちらへ寄せる)。I-6: `dd` が `sys_write` の戻り値を見て、短い / 失敗なら `dd: write failed at sector N (wrote M bytes)` で中止する。
- (往復 8) T2: 内蔵 `play` は `drivers/fm.c` の `io_wait` で鳴り終わるまで CPL=0 で同期に待つ (GUI 全体が止まり CTRL+STOP でも回収できない) ので `sh: cui only` の一覧に追加。`beep` は一瞬なので残す。SHELL_AS_APP のみ — 常駐 `.o` は不変 (I1〜I4 を戻した状態で `23a5d8f` と突合して確認)。
- (往復 8) **I1〜I4 は継承の欠陥で常駐にも効くので別コミット (S7 の例外)**。I1: 引数上限を超えたら `sh: too many arguments` を出して `parse_args_and_glob` が負を返し**行ごと捨てる** (黙って切り捨てると `cp … /wrong /intended` の宛先が消えた。複数 glob の合計超過も同じ)。I2: `sh_path_normalize()` (`cmd_fs_shared.c`) で `.` / `..` / 連続 `/` を畳んでから `fs_same_file` が比べる (`st_ino` が 0 の HostDrv で `cp /host/a /host/./a` が自己上書き判定を抜けていた。ino 比較は従来どおり併用)。I3: `dd cd0` は ATAPI の 1 セクタ 2048B (`SYS_CDROM_SECTOR_SIZE`、`include/config.h` に追加) で確保する。`dummy_total` / `dummy_bps` も初期化。I4: `wildcard_match` を反復型 (O(n×m)) に — 再帰版は `*a`×20 を `a`×40 に当てると事実上停止し、R4 以降それが `sys_ls` の CPL=0 コールバック内で回るので GUI ごと固まった。
- (往復 7) R2: 内蔵 `exec` / `if` / `time` / 外部行を含む `source` は事前判定を通った**後**にリダイレクトを張ってから外部へ行ける。`apply_redirects` で立て `reset_all_redirects` で下ろす印 `sh_redirect_active` を `sh_launch` の入口で見て断る (B4 のパイプ深度と同じ作法)。事前判定は残した。
- (往復 7) R3: `sh_ls.inc` の写しの名前幅を `OS32_MAX_PATH` (256B) に。128B では 127B で無言に切れて、一覧にも glob にも**存在しない短縮名**が出ていた。同じ幅なので切断そのものが起きない (UTF-8 の境界問題も消える)。`.bss` は 128 件 × 264B ≈ 33KB — 件数は据え置き (`ls` の一覧としてこの上限は `TAB_MAX_MATCHES` / `FL_MAX_ENTRIES` と同じ作法)。
- (往復 7) R4: 写し取りに**純関数のふるい** (`sh_ls_set_filter`) を足し、glob はコールバック内で `wildcard_match` を通ったものだけを写す。列挙順の先頭 N 件で切ると不一致が先に並ぶディレクトリで一致を取り逃がした。一致が上限を超えたら `sh: glob: too many matches` を出して**行ごと捨てる** (一部だけ展開して実行しない)。`ls` 側は件数通知のまま。
- (往復 7) R5: `losetup` と `dd loN` を `sh: cui only` の一覧に追加。ループ枠 (`drivers/loop_dev.c` の `loop_slots`) は sh が退場しても残り、backing FD だけ回収されて FD 番号の再利用で別ファイルを向く。カーネル側の本修正は別票。
- (往復 7) **R6 / R7 は常駐にも効くので別コミット (S7 の例外)**。R7: `argv[]` への格納を `max_args - 1` までに (`apply_redirects` が最後に `argv[argc]` へ NUL を置くので 1 つ溢れて隣の static を壊していた)。R6: `do_copy_file` が read / write の失敗で**負**を返す (0 を返していたため別 FS の `mv` が `sys_unlink(src)` して原本を失った)。
- (往復 6) B2: CPL=3 では `sys_ls` のコールバックから KAPI (int 0x80) を呼ぶと落ちる (実機で `find /etc -name filetypes` が crash、`du /etc` は正常)。`sh_ls.inc` の写し取りコールバック (KAPI を 1 つも呼ばない、上限 128 件・溢れは `(... N more)`) を挟み、`sys_ls` が戻ってから内蔵 `ls` の `vfs_ls_cb` と glob の `glob_cb` へ流す。常駐はマクロで従来どおり直接コールバック。
- (往復 6) B3: リダイレクト表は全アプリ共有で read/write/reset が owner を見ないので、外部コマンドに掛けると親のファイルへ入り親の FD を閉じる。`execute_single` が**リダイレクトを張る前**に `sh_has_redirect()` && `!sh_name_is_builtin()` を見て `sh: redirect to external command is not supported` で行を捨てる。
- (往復 6) B4: 先頭語だけの事前判定は `exec /bin/sh.bin | echo tail` を通すので、**最終起動口** `sh_launch` の入口で入れ子カウンタ `sh_pipeline_depth` を見て断る (段ループが `sh_pipeline_enter/leave` で増減)。事前判定は残した。
- (往復 6) B5: `ask` (cmd_script.c) の行末 BS も `sh_erase_cells()` (`\b` + 空白 + `\b`、3 バイト列の先頭は 2 セル) を使う。`sh_backspace_tail` と実装を共有。
- (往復 6) B6: パイプの段ループが各段の前に `sh_exit_flag` を見る (`exit | ask "wait: " V` で入力待ちに入らない)。
- (往復 6) B7: `build/programs.mk` の常駐 / sh 両方の `.o` 規則に `SHELL_DEPS`(= `shell.h` + `$(wildcard userland/shell/*.inc)`) を足した。Makefile の `DEPFILES` は `boot kernel drivers gfx fs exec kapi lib programs sdk` しか走査せず **userland の `.d` は読まれない**。`make -n sh` の dry-run で確認 — `.inc` だけ新しくした状態で、依存なしは再コンパイル 0 行、依存ありは `sh_obj/ui.o` 1 行。
- (往復 5) blocker: 内蔵コマンドのパイプが `sys_pipe_get_buf()` の**カーネル帯**ポインタを `sys_redirect_fd_buf()` へ渡し、`ring3_ptr_ok` (exec/exec.c) の早期検証で CPL=3 の sh ごと畳まれていた (`sh> echo a | cat`)。確保・取得・解放を `sh_pipe_alloc` / `sh_pipe_get_buf` / `sh_pipe_free` の 3 本に包み、`SHELL_AS_APP` では sh 自身の `.bss` (`sh_pipe.inc`、2 枠 × `PIPE_BUF_SIZE` = 128KB) から配る。常駐側はマクロで `g_api->sys_pipe_*` にそのまま展開 (`.o` はバイト一致)。
- (往復 5) 外部段が混じるパイプは `sh_stage_is_builtin()` が段ごとに見て `sh: pipe to external command is not supported` を 1 行出して行を捨てる (外部は要求表経由の別アプリなので sh の FD に掛けたリダイレクトが届かない)。判定は候補パス解決より前、`split_pipeline` の直後。
- (往復 3) blocker: 行末 BS を**破壊的**にした。端末の BS はカーソルを 1 セル左へ動かすだけでセルを消さない (`libos32term` の `model.rs`) ので、BS だけ出して短縮後の写しを確定すると `echo abc` → BS×2 → `x` が画面 `echo axc` / バッファ `echo ax` と食い違う。`sh_backspace_tail()` (sh_redraw.inc) が `\b` + 空白 + `\b` を出して写しまで確定する (3 バイト列の先頭は 2 セルぶん)。CUI の BS は console が消すので常駐側は 1 バイトも変えていない。
- blocker 2: 行の写しに**画面カーソルのバイト位置** `sh_drawn_pos` を足し、差分印字は「前方一致 かつ 新カーソルが行末 かつ **描画済みカーソルも行末**」のときだけにした。`redraw_line` は自分が置いたカーソル位置を写しへ残す。加えて内容を変えずにカーソルだけ動かす LEFT / RIGHT / HOME は `sh_drop_drawn()` で写しを捨て、次回を行の作り直しに倒す (経路の見落としで表示が壊れないように)。non-blocker のハーネス `goto` オフセットも `cmd + 5` へ直し、RED で本当に無限ループになることを確認した。

## 14. 実装メモ (W、2026-09-13)

- 起動口は単独ループ (`lib.rs` `standalone_loop`) の `session_handoff` の**直後**、
  `multiapp::resume_one` (park 判定) の**前**。`exec_start` / `exec_resume` から戻った後で、
  WM が owner 1 で走っている唯一の地点 — `launch_take` / `launch_report` / `exec_kill` はここだけ。
- `drain_launch_requests` (`lib.rs`) は `launch_pending() > 0` の間 `launch_take` を回し、
  LAUNCH は `run_program`、KILL / 孤児 (`LAUNCH_REQ_ORPHAN`) は `multiapp::kill_for_request`
  (= `exec_kill` → `FREE` を全部 `forget`、D8)。`launch_report` の `STALE` は再試行しない (§10 2)。
  要求表経由の失敗はモーダルを出さない (`session_launch` の経路は無変更)。
- D5 の巡回と「同じ tick に 1 回」は `multiapp::pick_poll` + `Multi` の `poll_last` /
  `poll_tick` / `poll_woken` (票の「`GuiState` に持つ」から変更 — 同種の控えが `Multi` にある)。
  印は `mark_resumed` で付けるので `pick` を 2 度呼んでも答えは変わらない。
- `abort_target` は `launch_child` を末尾まで辿る (`chain_tail`、環で止まる)。
  `abort_targets_current` は末尾 == cur のときだけ真 — ただし**宛先なし (`f == 0`) は従来どおり真**
  (偽にすると窓が 1 枚も無い周の CTRL+STOP がどこにも届かない)。
- 検証はホストのみ: `make check-gshell-host` 65 passed (T9-W 9 本追加、RED 7 → GREEN)、
  `check_constraints.py` / `check_gui_proto.py` / C 側 3 本の回帰。記録は `tools/tests/t9_tdd.md` §6。
  **`make` / 配備 / 実機は未実施** ([V4])。`cargo clippy` は着手前から `lib.rs:127` で落ちる (追加分は 0)。
- **実装レビュー 1 の blocker 2 件を修正 (2026-09-13)**: (1) `run_program` に依頼元
  (`LaunchVia::Wm` / `Table`) を渡すようにし、要求表経由の入口の拒否 (`cui only`) は
  モーダルを出さず **`OS32_ERR_INVAL` (負)** を返す — `RUN_REFUSED` = 0 のままだと表が `DONE` に
  なり、要求者が「起動して正常終了した」と読む。(2) 「起こした tick」は `pick` 時ではなく
  `mark_resumed` が `get_tick` を読んだ時刻を **1 本ごと** (`App::poll_tick`) に持つ形へ。
  集合の一括消去をやめたので、選んでから再開するまでに PIT が進んでも 2 回起こさない。
  追加した unsafe には SAFETY コメント。試験は 68 passed (T9-W 12 本、RED 2 → GREEN)。

- **実機受入 S6 不合格の修正 (2026-09-13)**: `abort_seen` を見る点が `handler.rs` の
  `op_wait` と全画面の top-level しか無く、**`WAIT_POLL` のアプリが居るウィンドウモード**
  (端末が `should_park` で park し WM が単独ループに居る周) の CTRL+STOP が捨てられていた。
  `standalone_loop` の分岐を `top_level_abort()` に括り、全画面は従来の `redirect_abort` 予約、
  ウィンドウモードは `multiapp::abort_at_top_level()` が `exec_abort_clear` → `exec_kill(末尾)`
  → `forget_freed` をその場で実行する (予約が立っている周は二重にしない)。試験 71 passed。

- **受入 S6 の 3 回目 (本人宛て) が不安定だった件の修正 (2026-09-13)**: `handler.rs` の
  `abort_targets_current` が真の枝で `break` してカーネルの `abort_req` に任せていたが、IRQ1 が
  アプリの CPL=3 実行中に着地した周は要求が立たず (暴走ではない) 出口で消える (3 回中 2 回失敗)。
  **本人宛ても予約に統一** (`multiapp::reserve_abort_self`) し、`should_park` が譲らせて
  top-level の `drain_top_level` が `exec_abort_clear` → `exec_kill(本人)` → `forget` を実行する。
  宛先なし (`abort_target` = 0) は取り消しだけ予約。表に載っていない owner (park できない) だけは
  従来どおり `break` する。CTRL+STOP を押した周だけ 1 本でも park する (押さない周の回帰はゼロ)。

## 15. 実装メモ (A、2026-09-13)

- 端末の起動は `session_launch` → **`launch_req(cmd)`** (`guest.rs:launch`)。token は受理した時点で
  必ず控える (落とすと誰も poll せず表が `ERR_FULL` で固着する)。`ERR_FULL` は従来どおり `busy` で
  行を残し、他の負は `launch_req failed (rc)` を出してプロンプトのまま。
- 要求表の読み方と取消の進み具合は新設の純粋モジュール `userland/rust/t5a_display/src/launch.rs`
  (`Phase` / `Attach` / `Step` / `Outcome`)。KAPI を呼ぶのは `guest.rs` だけ。
- 既存の 100ms タイマ (`TIMER_SINK`) の周で `launch_poll` を 1 回。`RUNNING` で子 ID を控えて
  最下行が `[running id=N] ESC=cancel`、`DONE` / `FAILED` / 負の `rc` / 未知の status はプロンプトへ。
- D9: 接続モードの ESC = `launch_cancel` 1 回 (`Attach` の印で連打をまとめる)。`AGAIN` は次のタイマで
  自動再試行し、`0` の後は `[cancelling id=N] wait` のまま `DONE` を待つ。`STALE` (もう完了している) でも token は捨てず、次の poll で `DONE` / `FAILED` を消費してからプロンプトへ (Codex 往復 1 の blocker: 捨てると表が IDLE に戻らず以後 `ERR_FULL`)。
- D4: `Record::Exit` は `session.apply` に渡すだけ (表示のみ)。`prompt::Event::Exit` は遷移表から削除し
  `Done` / `Failed` / `CancelRequested` に置き換えた。`exit` / 端末終了時の取消は**出さない** (孤児は
  カーネルの `launch_owner_exit` が回収、§10 non-blocker 1)。
- 検証はホストのみ: host 試験 48 → **62** (RED→GREEN の記録は `tools/tests/t9_tdd.md` §A)、
  `cargo check --release -p t5a_display`、`check_constraints.py`。`make` / 配備 / 実機は未実施 ([V4])。
- 折り返しをまたぐ BS (往復 4 の S blocker、ユーザー決裁で端末モデル側を修正): `libos32term` の
  `write_inner` の `\x08` が `x == 0 && y > 0` で前行末 (`cols - 1`、`Continuation` なら `cols - 2`) へ
  戻るようにした。モデルは「折り返し継続」の印を持たないので改行直後の BS も前行末へ載るが、BS 単体は
  1 文字も消さない — 判断と根拠、RED→GREEN、試験 4 件は `userland/libos32term/TDD_LOG.md`
  (2026-09-13 の節) に記録。model 試験 21 → **24**、t5a_display host 62 件は不変。

## 16. Codex 実装レビュー (W / A / S、2026-09-13) — 往復 1/3: Request changes → 各レーンへ差し戻し

- **A blocker**: ESC → `launch_cancel` が STALE のとき token を捨ててプロンプトへ戻ると、DONE / FAILED の表が poll で消費されず IDLE に戻らない → 以後の `launch_req` が FULL に固着。→ STALE でも poll で完了を消費してから戻る。
- **W blocker 1**: `run_program` の `cui only` 拒否 (`RUN_REFUSED = 0`) をそのまま `launch_report(token, 0)` すると要求者に DONE が届く。→ 要求表経由の拒否は負の rc、モーダルは出さない。
- **W blocker 2**: pick 時の tick で「起こし済み」を記録すると、pick と再開の間に tick が進んだとき同 tick に 2 回起こせる。→ 実際に起こした tick を ID ごとに記録する。
- **S blocker 1**: TAB 補完後の `redraw_line()` が `console_get_cursor` に依存し、GUI 中 (K6C-2 で座標が進まない) は入力位置が戻らない。→ SHELL_AS_APP では座標に依存しない再描画。
- **S blocker 2**: `source` 内の `exit` が行ループを止めない (`goto loop` で永久)。→ 各行の前に exit の印を見て資源を解放して抜ける。
- ゲート (テスター): `make clean` / `all` (72s) / `external` exit=0、`make check` は check-manifests §1c (`docs/07_shell.md` に内蔵 `exit` が無い) で exit=2 → S へ。
- non-blocker: A の STALE 試験は次回起動を見ていない、W の unsafe に SAFETY 注記、S の t9_tdd に RED 記録が無い、CUI 直起動時の `launch_req failed (-9)` の連発、UTF-8 の BS (既存)。
- 実装レビュー往復 1/3 の blocker 修正 (2026-09-13): ESC の `launch_cancel` が `OS32_ERR_STALE` を
  返しても **token を捨てない** — 完了した表は `launch_poll` が消費して初めて `IDLE` に戻る
  (`include/launch.h`) ので、捨てると以後 `launch_req` が `FULL` で固着する。取消待ちのまま poll を
  続け、`DONE` / `FAILED` を消費してからプロンプトへ戻す (host 試験にカーネル要求表の写しを置いて確認)。

**往復 2/3 (W `25e3c46` / A `ba8e540`): Approve** — 3 件の修正を確認、新たな blocker なし。non-blocker: W の mock が NULL 出力を拒否し INVAL を -2 にしている (実契約は NULL 可 / -9)、A の試験模型が取消直後を RUNNING で返す (実契約は PENDING → TAKEN)。いずれも実装の挙動には影響しない。S は別途 (往復 1 の blocker 2 件を修正中)。

**往復 2/3 (S `3103643`): Request changes** — 前回 2 件は修正済み。残る blocker: (1) `.profile` 内の `exit` の後に shell_run が無条件に 1 回入力待ちに入る (行ループ入口でも印を見る)、(2) `hel` → LEFT → TAB (CUI 直起動) で写しの画面カーソルを持たないため「末尾への延長」と誤判定 (写しにカーソル位置を持つ / 移動時は写しを捨てる)。non-blocker: ハーネスの goto 引数オフセット (`cmd + 7`)。
ゲート (テスター、`3103643`): `make all` / `external` / `check` すべて exit=0 (check 36s)。

**往復 3/3 (S `96e756f`): Request changes** — 往復 1・2 の 4 件は修正済みと確認。新たな blocker 1 件: GUI 端末で行末 BS が文字を消さない (端末の BS は非破壊のカーソル移動。`echo abc` → BS×2 → `x` で画面は `echo axc`、バッファは `echo ax`)。→ `\b` + 空白 + `\b` の破壊的消去を SHELL_AS_APP で出す修正をコーダーが準備中。**3 往復を使い切ったため着地の可否はユーザー判断 (ROLES §5)**。
ゲート (テスター、`96e756f`): `make all` / `external` / `check` すべて exit=0。sh.bin 61,232 B、shell.bin 65,680 B (不変)、gshell.bin 169,968 B、t5a_display.bin 38,640 B、kernel.bin 222,520 B。

**往復 5 (S `3374438` + 端末 `17870ea`): Request changes** — 折り返し BS は解消と確認。新規 blocker 1 件 (領域が変わった): 内蔵コマンドのパイプ `sh> echo a | cat` で、`sys_pipe_get_buf` が返すカーネル帯 (kmalloc) のポインタを `sys_redirect_fd_buf` に渡すため CPL=3 の早期ポインタ検証 (`ring3_ptr_ok`) が sh を fault kill する。常駐 CPL=0 では通っていた経路。→ SHELL_AS_APP ではパイプバッファを sh 自身のメモリから取る修正をコーダーが準備中、**着地可否はユーザー判断**。
ゲート (テスター、`17870ea`): `make all` / `external` / `check` すべて exit=0。

**往復 7 (S `1cdeaa1`、確認往復): Request changes — blocker 7 件 (R1〜R7)**。B4〜B7 は修正済みと確認。**R1 = B1 の再提示で有効** (PM の却下は誤り): `cwd` 0x00132020 の PTE は `paging.c:181` の `PAGE_RW` (USER 無し)、`exec.c:118` が RO+USER にするのは KAPI トランポリンの 1 ページだけ → CPL=3 の `cd` / `pwd` (apps/edit も) は #PF。→ K: `sys_getcwd` は CPL=3 由来なら CPL=3 が読めるページ (トランポリンページの空き) に写して返す (ABI 不変)。R2: 内蔵 `exec` / `if` / `time` / `source` 経由の外部起動がリダイレクト拒否を抜ける → `sh_launch` 入口で「リダイレクト中」の印を見る。R3: 写しの name 128B で長い名前が切れる → 256B。R4: glob の上限が一致数でなく列挙順の先頭 128 件 → コールバック内で照合し一致だけ写す、溢れたら行を捨てる。R5: `losetup` 後の退場で loop スロットが回収済み FD を保持 (カーネル側は別票) → sh.bin では `losetup` / `dd loN` を cui only。R6 (継承): 別 FS への `mv` がコピー失敗後も原本を削除 → `do_copy_file` が負を返す (常駐にも効く、別コミット、S7 例外)。R7 (継承): 256 引数で `argv[256]` の配列外書き込み → 上限 MAX_ARGS-1 (同上)。non-blocker 9 件 (script の先読み、ask の日本語、入れ子 glob の解放一覧、引用付き builtin の誤拒否、cp -r の 64 件、symlink 型、dd の未初期化、試験範囲) は残件表へ。
往復 7 の対応: R1 は K (`2540fed`、`ring3_user_str` でトランポリンページの写し)、R2〜R5 + R7 は S (`940b5d6`)、R6 は別コミット (`20ea335`)。ゲート (`20ea335`): `make all` / `external` / `check` exit=0、shell.bin 65,712 B (R6 / R7 で +32 B、S7 の例外)。**ユーザー決裁 (2026-09-13): 往復 8〜10 の 3 回を追加で許可**。以後は blocker を「T9 由来」と「継承」に分けて報告させる。

**往復 8 (`9f1d77a`、追加 1/3): Request changes — T9 由来 2 件 + 継承 4 件** (R1〜R7 は修正確認、R1 の写し場の番地計算も正しいと確認)。T1: リダイレクト表が全アプリ共有で park/resume でも切り替わらない → `ask P V > /tmp/out` で sh が park 中に WM から起動した別アプリの出力が sh の出力先に入る、パイプなら別アプリの CR3 で sh の .bss 番地に書く → **K: リダイレクト状態を AppSlot ごとに退避 / 復元**。T2: 内蔵 `play` が CPL=0 の io_wait で GUI 全体を止め CTRL+STOP も効かない → sh.bin では cui only。継承 I1: 引数上限で切り捨てて `cp` の宛先が変わる → 行を捨てる。I2: HostDrv (`st_ino` 0) で `cp /host/a /host/./a` が原本を切り詰める → パス正規化で同一判定。I3: `dd cd0` が 1024B 確保で 2048B 受ける → セクタ長で確保。I4: `wildcard_match` の指数的バックトラック (R4 でコールバック内に入るので GUI ごと止まる) → 反復型に。non-blocker: 9d が reset_all_redirects を通していない、`?` は glob 開始条件でない。継承 4 件は常駐にも効くので別コミット (S7 例外)。

**往復 9 (`3f919a5`、追加 2/3): Request changes — T9 由来の新規 blocker はゼロ**。T1 / T2 / I1〜I4 の修正は確認。ただし I4 の反復型 `wildcard_match` が回帰 C-1 (`*` 同士を通常文字として先に消費し `("*", "*dest")` が不一致 → `cp /tmp/src /tmp/g/*` が別ファイルを上書き、常駐にも入る) を持ち込んだ → 必須修正。継承 6 件: I-1 `ide` の shell 側 `IdeInfo` (92B) がカーネル (96B) より小さくスタック外 2B 書き込み、I-2 `env_expand` の 4095B 打ち切りで宛先が落ちる、I-3 `fs_join_path` の 256B 切り詰めで `mv` が別宛先を上書き、I-4 `cp -r` の収集表が名前を 31B に切る、I-5 `dd hd0` が 512B セクタを 1024B として出力、I-6 `dd` が write の戻り値を捨てる。non-blocker: T1 の park 中 kill は実際には FD を二度 close (`vfs_close_owned` → `fd_redirect_close_state`)、glob の `mem_alloc` 失敗が行の失敗に伝わらない。ゲート (`3f919a5`): `make all` / `external` / `check` exit=0、shell.bin 66,704 B / sh.bin 64,752 B。
対応: C-1 + I-1〜I-6 は S (継承は別コミット)、二重 close は K。往復 10 (最終) の判定基準は「T9 由来 + T9 の修正が持ち込んだ回帰」とし、継承バグは別票 (`docs/tasks/shell/INHERITED_BUGS.md`、往復 6〜9 の non-blocker を含む) に切る。

**往復 10 (`ecaba37`、追加 3/3 = 最終): Approve** — T9 由来の blocker なし、T9 の修正が持ち込んだ回帰なし (C-1 / I-1〜I-6 / 二重 close の修正を確認、R1 の写し場 0x97c〜、R2 の印、C-1 の照合を重点確認)。継承 3 件 (`source` の 255B 切断、補完の 126B、`cat -n` の区切り) は台帳へ。ゲート (`ecaba37`): `make all` / `external` / `check` exit=0、vmkernel.lz4 465,581 B、sh.bin 65,360 B、shell.bin 67,312 B (継承修正で +1.6KB、S7 例外)、gshell.bin 169,968 B、t5a_display.bin 38,640 B。**配備へ**。

## 17. 実機受入の記録 (PM / テスター、2026-09-13、feat/gui `ecaba37` を NHD 配備、vmkernel 465,581 B、sh.bin 65,360 B、shell.bin 67,312 B、gshell.bin 169,968 B、t5a_display.bin 38,640 B、15MB pc98、API v49、Build Sep 13 08:18、kselftest 76 / 0)

配備手順: NP21/W 停止 → `make nhd-pull` (テスター) → バックアップ `os32.nhd.bak-t9-20260913-082539` → `os32-cycle deploy` (テスター、vmkernel サイズ一致) → `make deploy` (HostDrv、テスター) → `ver` / `ls -l` でサイズ照合。

| 受入 | obs | 判定 |
|---|---|---|
| **S1** | 端末で `sh` → バナー `OS32 External Shell Started` と `sh> ` (`[running id=3]`)。`ls /` `pwd` (`/`) `env` `cat /etc/system.cfg` (`GFX=pc98` `GUI=0`) `cd /usr` → `pwd` = `/usr/` (CPL=3 の `sys_getcwd` 写し R1 が動作) | **合格** |
| **S2** | `sh> kbd_echo` → 子が起動しバナー、`abc` → `got 0x61 'a'` …、`q` → `bye` → **`sh> ` に復帰** | **合格** |
| **S3** | `sh> source /test/hello.sh` → `hello from sh script` / `pwd` / `klibc_test` (49 passed) / `after external` / `env` / `script done` → `sh> ` | **合格** |
| **S4** | `os32gui` / `filer` / `rshell` / `play cde` → いずれも `sh: cui only` | **合格** |
| **S5** | `exit` → 端末プロンプト `> `。`sh` → ESC → `> ` (launch_cancel、DONE 消費)。再度 `sh` → `sh> ` (FULL にならない) → `exit` | **合格** |
| **S6** | 端末 → sh → kbd_echo で CTRL+STOP ×3 → **何も畳まれない** (kbd_echo 生存、`ring3_abort_count` 0)。原因: sh が WAIT_POLL に居る間 WM はウィンドウモードの top-level で回り、そこに `abort_seen` を消費する経路が無い (handler.rs:388 は op_wait 内、lib.rs:205 は全画面のみ) → **W へ差し戻し** | **不合格 (修正中)** |
| **S7** | Start → CUI mode: 端末 + sh + kbd_echo が生きた状態から `appslot_reclaim_count` 5 → 8 (`last_reclaim_id` 2 = 端末が最後 = 末尾から)、CUI シェル応答 (`ver` API v49)、regress 6 本 (テスター、obs は §17 の下)、常駐 `shell.bin` は継承修正 (R6/R7/I1〜I6/C-1) 以外の .o がバイト一致 (S の突合)、`SHELL_AS_APP` の `exec_run` 参照 0 件。regress 6 本 obs 全通過 (kselftest 76/0、klibc_test 49/0、alloc_demo all passed、ring3_fault → `ver` 応答、`echo abc \| wc -c` = 4、screenshot 128,118 B)、`v86 -t` OK | **合格** |
| **S6 再試験** (W `dc2f78d` の gshell.bin 170,288 B を HostDrv → `hsync`) | 1 回目の CTRL+STOP で kbd_echo だけ畳まれ `sh> ` に復帰 (W の top-level abort は動作)。**2 回目で sh と端末が両方消える** (`ring3_abort_count` 0 → 1、`fault_kill_count` 0 → 2)。原因: IRQ1 の `ring3_abort_request()` が**その瞬間に走っている slot** (WAIT_POLL の sh か 100ms タイマの端末) に abort_req を立て、K2 の経路 (IRQ1 スタブ / syscall 出口) がカーネル側で畳む — GUI 配下では D8 の宛先 (WM が解決する末尾) と無関係なアプリを畳む。K5c の op_wait 内では WM が先に降ろせたが top-level では間に合わない → **K へ差し戻し** (GUI 中は IRQ1 由来の abort をカーネルが自動処理せず WM に委ねる。暴走の逃げ道は「N tick 以上 resume されていない文脈」に限定) | **不合格 (修正中)** |
| **S6 補正版の再確認** (K `50cb48a`、vmkernel 465,907 B、kselftest 80/0、各回の前に 6 秒待つ) | 1 回目 kbd_echo → `sh> `、2 回目 sh → 端末の `> ` (6 秒待っても端末は暴走扱いされない = 補正の狙いは達成)。**3 回目 (端末自身が宛先) が畳まれない** (`> _` のまま、`ring3_abort_count` 0)。K5c の「本人宛て」経路 (op_wait 中の IRQ1 が本人に abort_req を立て、WM が break → syscall 出口で畳む) を「GUI 中は IRQ1 由来を立てない」が壊していた (2c74ffb の 3 回目は暴走の誤判定で偶然動いた) → **K へ**: op_wait 中のアプリには従来どおり立てる (`admit = !gui \|\| runaway \|\| cur_in_op_wait`)。regress 6 本は通過、CUI 復帰 OK | **不合格 (修正中)** |
| **S6 (K `c498cb5`、vmkernel 466,045 B、kselftest 81/0)** | 1 回目 kbd_echo、2 回目 sh は毎回合格。**3 回目 (端末本人) は不安定** (3 走のうち 2 走で畳まれず、1 走と単発 1 回は `ring3_abort_count` +1 で畳まれた)。K5c 回帰: 素の GUI アプリ `gui_demo` をフォーカスして CTRL+STOP → 閉じる (合格)。原因: 本人宛ては IRQ1 が op_wait 中に落ちたときだけカーネルの abort_req 経由で畳まれ、端末の 100ms タイマ処理 (CPL=3) の最中に落ちると abort_seen だけが立ち handler.rs:388 が `break` して消える → **W へ**: 本人宛ても予約 (`request_kill` + top-level の drain) に統一し IRQ1 の着地点に依存しない | **不合格 (修正中)** |
| **S6 最終** (K `c498cb5` + W `ddb3ff3`、gshell.bin 170,736 B を HostDrv → `hsync`、各 CTRL+STOP の前に 6 秒待つ) | 3 走とも 3 回の CTRL+STOP で `appslot_reclaim_count` が +1 ずつ、`last_reclaim_id` は 4 → 3 → 2 (kbd_echo → sh → 端末、D8 の末尾から)。K5c 回帰: `gui_demo` をフォーカスして CTRL+STOP → 閉じる。CUI 復帰 OK、regress 6 本 obs 全通過 (kselftest 81/0) | **合格** |

**最終判定 (PM、2026-09-13)**: S1〜S7 すべて合格、**T9 受入済み** (feat/gui `ddb3ff3`)。実機受入で見つけて直した 4 件 (静的レビューの範囲外): W ウィンドウモード top-level の abort 消費 (`dc2f78d`)、K GUI 中の IRQ1 由来 abort_req をカーネルが先取りしない (`2c74ffb` → 暴走基準を最後の syscall から 200 tick に `50cb48a` → op_wait 中は K5c 経路を維持 `c498cb5`)、W 本人宛ても予約経由に統一 (`ddb3ff3`)。
