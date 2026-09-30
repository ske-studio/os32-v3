# T7 — 既存 CUI コマンドを端末で実際に流す

> 発行: PM (2026-09-12) / 状態: **受入完了 (2026-09-12)**

受入済み (2026-09-12、K `7a6b124` / A `ce0406b`)。決裁 B の順序 (K5 → K6 console → 端末アプリ → K7 入力統合 → **CUI コマンドを端末で流す**) の 5 段目。
前提: K6C (con_sink、端末アプリ `t5a_display`)、K7 (kbd 待ちの park、`kbd_inject`)。すべて feat/gui `474f9ce` に着地・受入済み。

## 0. 目的

端末窓のプロンプトにコマンド名を打って Enter すると、その CUI プログラムが gshell 配下で起動し、
出力が端末窓に出て、打鍵がそのプログラムに届き、終了するとプロンプトに戻る。
v1.3 の「GUI 上の CUI 実行」の縦切りをここで完成させる (shell script / full-screen GFX 復帰は次段)。

## 1. 接点 (確認済み)

- 起動: libos32gui `session_launch(path)` (`userland/rust/libos32gui/src/session.rs:90`、op 66 `GUI_SESSION_LAUNCH`)。
  gshell 側 (`userland/gshell/src/session.rs:146`) は **1〜255B の絶対パス**だけ受け、K5b-W で「LAUNCH は 1 本増やすだけ
  (要求元は畳まれない)」。値はそのまま `exec_start(cmdline)` に渡る (`exec_start` は `const char *cmdline`、引数付き可)。
  既存 pending があれば `OS32_ERR_FULL`。
- 出力: 子の `kprintf` / `shell_print` / `console_write` / `tvram_clear` / `console_set_cursor` は K6C のレコードで端末に届く。
- 入力: 子が `kbd_getchar` で `WAIT_KEY` に park → 端末の `kbd_inject` (K7-A) → gshell が起こす (K7-W)。
- 子の終了: いまは端末に**通知が無い** (K7 受入では `q` → `bye` の表示で見ただけ)。
- 起動失敗: gshell が「Launch failed …」のモーダルを出す (K5b)。exec の `Error: …` 行は con_sink 経由で端末にも出る。

## 2. 設計 (PM 案)

| # | 決定 | 担当 |
|---|---|---|
| E1 | **子の終了レコード**: con_sink に `EXIT` (type 4、payload `id u8`、ヘッダ 2 バイト) を足す。シンク有効中に非シェル ID が回収されるとき (`exec_reclaim_owned` の経路、正常終了 / kill / fault のすべて) にカーネルが積む。`os32_kapi_shared.h` の `CON_SINK_REC_EXIT` / `CON_SINK_HDR_EXIT` を追記 ([C4] Shared Layer、既存 type の値は不変)。`CON_SINK_REC_MAX` は変わらない | K |
| E2 | **プロンプト行**: 端末の最下行に `> ` + 編集中の行 (ローカル編集: 印字可能 ASCII / BS / Enter。FEP の TEXT もそのまま行に入れる)。プロンプト表示中は **打鍵を注入しない** | A |
| E3 | **起動**: Enter で行を確定 → 先頭トークンが `/` で始まればそのまま、そうでなければ `/usr/bin/<名>.bin` → `/bin/<名>.bin` の順に**存在確認** (os32api の open / stat 相当) して最初に見つかった絶対パス + 残りの引数を `session_launch` に渡す。見つからなければ端末に `command not found: <名>` を出す (ローカル)。`OS32_ERR_FULL` (別の LAUNCH が pending) は `busy` を出して行を残す | A |
| E4 | **接続モード**: 起動要求を出したら **接続モード**へ: プロンプトを消し、打鍵は K7-A どおり `kbd_inject`。`EXIT` レコードを受けたらプロンプトに戻る (どの ID が自分の子かは持たない — v1.3 は端末 1 本 / 子 1 本。複数 EXIT が来ても最後のでプロンプトに戻るだけ)。**起動失敗** (gshell のモーダル) の場合は `EXIT` が来ないので、接続モード中の **ESC** で強制的にプロンプトへ戻れるようにする (ESC は子には注入しない) | A |
| E5 | 空行 Enter はプロンプトを再表示するだけ。`exit` はプロンプトから端末自身を終了 (ESC と同じ)。それ以外の内蔵コマンドは作らない (シェルの内蔵コマンド `ls` `cat` 等は常駐シェル内蔵なので端末からは**まだ**呼べない — 次段の「shell script」で `sh -c` 相当を検討) | A |
| E6 | gshell は変更しない。カーネルは E1 だけ | — |

## 2-1. 実装メモ (K) — E1 完了 (2026-09-12、コーダー)

- `CON_SINK_REC_EXIT 4` / `CON_SINK_HDR_EXIT 2` を `os32_kapi_shared.h` に追記 (既存 type と
  `CON_SINK_REC_MAX` は不変)、`con_sink_push_exit(int id)` と `ring_rec_size()` の type 4 を実装。
- 積むのは `exec_reclaim_owned()` の (9) だけ — 正常終了 (`exec_exit`)、`exec_kill`、fault
  (`ring3_fault_kill` → `exec_fault_recover` → `exec_exit`) の 3 経路すべてがここを通る。
  `id != APP_ID_SHELL` かつシンク有効かつ `con_sink_reader_get() != id` のときだけ積み、
  判定は `con_sink_owner_exit()` で所有を返す**前**に置いた (後だと端末自身の退場でも積む)。
- **A 側の前提**: `t5a_display/src/sink.rs` は未知 type で `Stop::Unknown` して解析を止めるので、
  E4 で `REC_EXIT` を足すまで端末は最初の `EXIT` で固まる。E1 単独では受入 T1 は通らない。

## 3. 受入 (ゲスト、PM / テスター)

| ID | 試験 | 合格条件 |
|---|---|---|
| T1 | `kbd_echo` | プロンプトに `kbd_echo` + Enter → 子が起動しバナーが端末に出る → `abc` を打つと `got 0x61 'a'` … が出る → `q` で `bye` が出て**プロンプトに戻る** (`EXIT` レコード) |
| T2 | 出力のみのコマンド | `klibc_test` (または `hal_test`) + Enter → 出力が流れ、終了でプロンプトに戻る。`dropped` 0 |
| T3 | 見つからない | `nosuch` + Enter → `command not found: nosuch`、プロンプトのまま |
| T4 | 起動失敗からの復帰 | `/etc/system.cfg` + Enter → gshell の「Launch failed」モーダル、OK → ESC でプロンプトに戻る |
| T5 | 回帰 | regress 6 本、v86 -t、Start → CUI mode の畳み込み (子が生きていても) |

## 4. 禁止 / 範囲外

- gshell の変更。常駐シェルの内蔵コマンドの端末からの実行。shell script。full-screen GFX の子 (次段)。
- 配備・コミット・push・エミュレータ・ローカル AI・ini・.env・`make` は禁止 (コーダー)。ホスト試験は必須。

## 5. 実装メモ (A) — E2〜E5 完了 (2026-09-12、コーダー)

- `userland/rust/t5a_display/src/prompt.rs` (新規、純関数) に行編集 / 行の解釈 / 候補パス / モード遷移を切り出し、
  `sink.rs` は `EXIT` (type 4、`[type][id]`、K 側の `CON_SINK_REC_EXIT` と一致) を `Record::Exit(id)` として解く。
- `view.rs` の最下行をプロンプト行に確保 (`body_rows() = rows() - 1`)。con_sink の出力 64 行の領域とは分けた。
- `guest.rs`: プロンプト中は 1 バイトも注入せず、Enter で `sys_open`/`sys_close` の存在確認 → `session_launch`。
  `ERR_FULL` は `busy` で行を残し、`EXIT` / 接続モードの ESC でプロンプトへ戻る (ESC は子に注がない)。
- 検査は `cargo check --release -p t5a_display` と host テスト 48 件 (36 → 48) のみ。**`make` / 配備 / 実機は未実施** ([V4])。

## 6. 実機受入の記録 (PM / テスター、2026-09-12、K `7a6b124` + A `ce0406b` を NHD 配備、vmkernel 460,206 B、t5a_display 37,548 B、15MB、API v47、kselftest 64 / 0)

| 受入 | obs | 判定 |
|---|---|---|
| **T1** | プロンプトに `kbd_echo` + Enter → `> kbd_echo` のエコーと子のバナー、最下行 `[running] ESC=prompt`。`abc` → `got 0x61 'a'` … 、`q` → `bye` の直後にプロンプト `> _` へ復帰 (`EXIT` レコード、`appslot_last_reclaim_id` 3)。`ring3_kbd_park_count` 3 | **合格** |
| **T2** | `klibc_test` + Enter → 出力が流れ (画面 1 回折り返し `wrap=1`)、`=== Result: 49 passed, 0 failed ===` の後にプロンプト復帰。`dropped` 0、`in=1749B rec=86` | **合格** |
| **T3** | `nosuch` + Enter → `command not found: nosuch`、プロンプトのまま | **合格** |
| **T4** | `/etc/system.cfg` + Enter → exec の `Error: invalid OS32X binary` が端末に出て gshell の「Launch failed」モーダル → OK → ESC でプロンプト復帰 | **合格** |
| **T5** | Start → CUI mode で端末が畳まれ `g_slot[2]` / `[3]` とも state 0、`v86 -t` OK、regress 6 本 obs 全通過 (kselftest 64 / 0) | **合格** |

**判定 (PM、2026-09-12)**: T1〜T5 合格、**T7 受入済み**。GUI 上で CUI コマンドを起動し、出力を見て、打鍵を渡し、終了で戻る縦切りが通った。
残: 常駐シェルの内蔵コマンド (`ls` `cat` 等) は端末から呼べない (次段の shell script / `sh -c` 相当で扱う)、FEP 経由の日本語入力、8MB。
| 8MB (`ram-8mb`、pc98) | 端末 (段 2、約 460 ページ) を立てた後の空きは 308 ページで、プロンプトからの `kbd_echo` は `[DBG] NOMEM: need pages=453 free=308` → gshell の「Launch failed」。端末は無事 (接続モードから ESC でプロンプトへ)。決裁 D9-3 (8MB は GUI アプリ 1 本立てば要件、複数は本体メモリ依存) の範囲内で**仕様どおり**。3KB の CUI プログラムでも段 2 の下限 (sbrk 256KB + exec_heap + スタック 256KB + PT) で約 1.8MB 要るのは、将来の縮小候補 (小さな CUI プログラム向けの段) | 仕様どおり (端末 + 子は 8MB では入らない) |
