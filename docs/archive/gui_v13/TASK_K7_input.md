# K7 — 入力統合: GUI 中の `kbd_getchar` を端末アプリの打鍵で満たす (設計草案)

> 発行: PM (2026-09-12) / 状態: **受入完了 (2026-09-12)**

受入済み (2026-09-12) — K `c6a775d` / A `c37d6e3` / W `df1df96` + W2 `ffed0e5`。I1 / I2 (ASCII) / I3 / I4 (15MB) / I5 合格。未実施: I2 の FEP 経由の日本語、I4 の 8MB。
親: [PLAN.md](../../tasks/gui/v13/PLAN.md) §1 (決裁 B: K5 → K6 console → 端末アプリ → **K7 入力統合** → 既存 CUI コマンドを端末で流す)。
前提: K5b (協調型 4 本、`exec_park` / `exec_resume`)、K6C (con_sink、端末アプリ `t5a_display`)。

## 0. いま起きること (確認済み)

- GUI モード中、IRQ1 は **raw リングにだけ**積む (`drivers/kbd.c:242`、WM が `kbd_trygetrawkey` で読む)。cooked リング `kbd_buf` は空のまま。
- CUI プログラムを gshell の Run で起動すると (K5b で可能)、そのプログラムが `kbd_getchar()` (KAPI、`drivers/kbd.c:351`) を呼んだ瞬間に
  カーネルの `for(;;) { if (kbd_count) …; hlt; }` に入り、**戻らない**。syscall の中なので OP_WAIT の park も起きず、
  協調型の全体 (gshell と他の 3 本) が止まる。脱出は CTRL+STOP (IRQ1 の abort 要求) だけで、それも `hlt` ループが要求を見なければ効かない。
- 端末アプリは K6C-A で出力側だけ持つ。打鍵は gshell からフォーカス窓へ `GUI_EV_KEY` / `GUI_EV_TEXT` (FEP 確定文字、UTF-8) で届く
  (`userland/gshell/src/ring.rs:117-121`)。

## 1. 設計 (PM 案、レビュー対象)

| # | 決定 | 根拠 |
|---|---|---|
| D1 | **kbd の待ちを第 2 の park 点にする**: GUI モード中、CPL=3 の呼び手 (syscall フレームあり) が `kbd_getchar` / `kbd_getkey` で cooked が空なら `hlt` せず **`exec_park` と同じ手順で止める** (状態 `APP_STATE_WAIT_KEY`、印 `parked_from_kbd`)。`resume` は `wait_ret` に**その文字**を入れるので、アプリから見ると `kbd_getchar()` が普通に値を返したように見える (OP_WAIT の `wait_ret` と同じ仕掛け、`exec/exec.c:1600` 付近) | K5b の切替点は「park されたフレームからのみ resume」(G7)。印を分けて、WM が「鍵待ち」と「OP_WAIT 待ち」を区別できるようにする。`hlt` ループを残すと協調型が壊れる |
| D2 | **打鍵の注入は KAPI 1 本** `kbd_inject(const u8 *utf8, u32 len)` (v47、末尾追記)。呼べるのは **con_sink の読み手** (= 端末アプリ、`g_reader`) だけ。カーネルは注入リング (256B、静的) に積む。GUI モード中の `kbd_getchar` / `kbd_trygetchar` / `kbd_getkey` は**この注入リングだけ**を見る (IRQ1 の cooked は GUI 中は使わない、現状どおり) | 端末アプリは CPL=3 で他アプリを起こせない (owner 1 専用)。注入と起床を分けると WM の規則 (D11) をそのまま使える |
| D3 | **起床は WM (gshell) の OP_WAIT の中**: `exec_app_state` に `WAIT_KEY` を足し、`multiapp::ready_to_run` を「`WAIT_KEY` かつ注入リングが空でない」でも真にする (`kbd_inject` の戻り値か `con_sink_stat` 拡張で pending を知らせる)。選択規則は D11 のまま (ID 昇順、入力優先の streak 上限 4)。resume 時にカーネルが注入リングから 1 文字 (UTF-8 なら 1 バイト) を取り `wait_ret` に入れる | プリエンプションは足さない (ユーザー決裁)。鍵待ちのアプリが複数なら D11 が決める |
| D4 | **FEP / 日本語**: 端末アプリは `GUI_EV_TEXT` の UTF-8 をそのまま `kbd_inject` に渡す。`kbd_getchar` は 1 バイトずつ返す (既存 CUI コマンドは UTF-8 バイト列を読む前提: §4-27) | 変換は gshell 側に既にある。カーネルで再変換しない |
| D5 | **CTRL+STOP**: `WAIT_KEY` で止まっているアプリは `exec_kill` で畳める (K5c の経路、フォーカス窓宛)。注入リングはアプリ終了 (`exec_reclaim_owned`) と CUI 復帰で捨てる | 既存の回収に並べるだけ |
| D6 | **CUI モードは無変更**: `kbd_gui_mode` が 0 なら従来の cooked + `hlt`。rshell のタイムアウト経路も無変更 | 回帰 6 本と v86 を守る |
| D7 | **`kbd_getkey` (スキャンコード付き)** は GUI 中は下位 8 bit だけ意味を持つ (スキャンコードは 0)。`kbd_is_pressed` / `kbd_get_modifiers` は raw 側の状態をそのまま | 端末経由ではスキャンコードが無い。必要になったら `GUI_EV_KEY` を別リングで注入する拡張を後で |

メモリ: 注入リング 256B + AppSlot の印 1 語 × 5。`MEMORY_BUDGET.md` に計上。

## 2. 受入 (ゲスト、PM / テスター)

| ID | 試験 | 合格条件 |
|---|---|---|
| I1 | 止まらない | gshell で端末を出し、Run で `kbd_getchar` を呼ぶ CUI プログラム (`input_test` 等、無ければ小さな試験プログラム) を起動 → gshell と他アプリが動き続ける (`ring3_switch_count` が増える、クリックが届く) |
| I2 | 打鍵が届く | 端末窓にフォーカスして文字を打つ → CUI プログラムが受け取り、その出力が端末窓に出る (K6C の経路)。日本語は FEP 確定文字が UTF-8 バイト列で届く |
| I3 | CTRL+STOP | 鍵待ちのプログラムをフォーカス窓宛の CTRL+STOP で畳める。`fault_kill_count` は増えない |
| I4 | 回帰 | regress 6 本、v86 -t、CUI の `kbd_getchar` (シェルの入力) が従来どおり。8MB / 15MB |
| I5 | 切替点 | `ring3_resume_bad_frame_count` 0、`parked_from_kbd` 無しの resume を拒否する負例 (kselftest) |

## 3. レビューで見てほしい点

1. D1 の「syscall の中から park」は OP_WAIT と同じ機構だが、`kbd_getchar` は **`hlt` を前提に書かれた呼び手** (rshell タイムアウト等) を持つ。GUI 中だけ分岐する境界が正しいか。
2. D2 の権限 (con_sink の読み手だけが注入できる) で足りるか。端末が 2 本のときの挙動 (busy 側は注入も拒否)。
3. D3 で「鍵待ちのアプリが注入リングを共有する」ことの是非 (端末 1 本 / CUI 1 本の v1.3 では問題にならないが、複数 CUI が同時に鍵待ちなら最初の 1 本が全部取る)。

## 4. 範囲外

- 端末から CUI コマンドを起動する UI (次段「既存 CUI コマンドを端末で実際に流す」)。full-screen GFX 復帰。shell script。
- `kbd_getkey` のスキャンコード完全再現。

## 5. 独立レビュー (2026-09-12) の回答と反映 — 実装はこの節を優先する

§3 の 3 点はいずれも **妥当** (① GUI 中だけ分岐する境界は正しい、② 注入権限は con_sink の読み手で必要十分、
③ 注入リングの共有は v1.3 の範囲で承認。将来は pty 化)。加えて実コードの精査で出た 4 点を決定事項に足す:

| # | 反映 | 対象 |
|---|---|---|
| R1 (①) | park の条件は **`kbd_gui_mode && g_cur_app && a->cpl3 && g_cur_frame != 0`**。`kbd_trygetchar` (`kbd.c:333-349`、ノンブロッキング) は park せず注入リングを見て無ければ -1。注入リングに文字があれば park せず即返す (UTF-8 の続きバイトを含む) | K |
| R2 (②) | 端末アプリは**イベントループに入る前に `con_sink_read` を 1 回呼んで読み手権限を確立**する規約。`g_reader == CON_SINK_NO_READER` の段階の `kbd_inject` は `OS32_ERR_EXIST` で拒否 | A (規約)、K (拒否) |
| A | gshell `multiapp.rs:601-608` の `slot_of_owner(k)` が `None` → `forget(k)` する経路は、**CUI アプリ (スロット無し) が `WAIT_KEY` のとき forget してはならない**。状態が `WAIT_KEY` なら `exec_resume(k, 0)` を呼ぶ分岐を足す (`wait_ret` はカーネルが上書きする、下記 B) | W |
| B | **文字の取り出しはカーネルの `exec_resume` で完結**: `parked_from_kbd` の印を見て注入リングから 1 バイト取り、`a->frame[APP_FRAME_EAX]` に入れる。WM 側に取り出し用 KAPI は作らない。空なら resume を拒否 (`OS32_ERR_AGAIN` 相当、印は残す) | K |
| C | WM が「注入リングに文字がある」ことを知る手段: **`con_sink_stat` は拡張しない (append-only)**。KAPI v47 は **`kbd_inject(const u8 *utf8, u32 len)`** (読み手専用、戻り = 積んだバイト数 / 負 = エラー) と **`kbd_inject_pending(void)`** (未読バイト数、誰でも可) の **2 本**。`exec_app_state` は新しい値 `APP_STATE_WAIT_KEY` を返す (値の追加は互換)。WM の `ready_to_run` は「`WAIT_KEY` かつ `kbd_inject_pending() > 0`」で真 | K (v47)、W |
| D | `exec/appslot.c:321` の `if (a->state != APP_STATE_PARKED) return OS32_ERR_STALE;` を **`WAIT_KEY` も許す**ように広げ、`exec_kill` で鍵待ちのアプリを畳めるようにする (D5) | K |

発注の分割: **K7-K** (カーネル + KAPI v47 + kselftest + ホスト TDD) → 着地後に **K7-W** (gshell: A / C の WM 側) と
**K7-A** (端末アプリ: R2 の規約、`GUI_EV_TEXT` / `GUI_EV_KEY` → `kbd_inject`)。受入 I1〜I5 は 3 票が揃ってから。

## 6. K7-W / K7-A の発注内容 (K7-K 着地後に出す)

**K7-W (gshell、`userland/gshell/src/multiapp.rs` ほか)**
- `exec_app_state` の新値 `APP_STATE_WAIT_KEY` を模型 (`session` / `multiapp`) に足す。`ready_to_run` は
  「`WAIT_KEY` かつ `kbd_inject_pending() > 0`」で真 (指摘 C)。
- `slot_of_owner(k)` が `None` でも状態が `WAIT_KEY` なら `forget(k)` せず `exec_resume(k, 0)` (指摘 A)。
  resume が「空」で拒否されたら (K の専用負値) その周は譲る (streak に数えない)。
- D11 の規則・上限 (30) は不変。ホスト試験 `host/wm_tests.rs` / `test_multiapp_model.py` に RED→GREEN
  (スロット無しの `WAIT_KEY` アプリが forget されない / pending 0 なら起こさない / pending > 0 で resume される)。
- `build/app.conf` の gshell を KAPI 47 に。

**K7-A (端末アプリ、`userland/rust/t5a_display`)**
- イベントループに入る前に `con_sink_read` を 1 回呼び読み手権限を確立 (R2)。失敗 (`ERR_EXIST`) は状態行 busy のまま注入もしない。
- `GUI_EV_TEXT` の UTF-8 と、`GUI_EV_KEY` のうち制御キー (Enter → `\n` または `\r`、BS、TAB、ESC は自分の終了に使うので注入しない、
  矢印は範囲外) を `kbd_inject` へ。戻り値が負なら状態行に出す。
- ローカルエコーはしない (CUI プログラム側の出力が con_sink 経由で戻る)。
- `build/app.conf` の t5a_display を KAPI 47 に。ホスト試験: キー → バイト列の変換表 (境界: 空 TEXT、多バイト、Enter)。

## 7. 実装メモ (K) — K7-K 着地、2026-09-12

- 注入リング `kernel/kbd_inject.c` (256B 静的)。あふれは **新しい方**を捨てる (打鍵は順序が
  意味を持ち、古い方を捨てると打った頭が欠ける)。捨てた数は戻り値 (< `len`) と
  `kbd_inject_drop_count`。権限は `con_sink_reader_get()` (新設、KAPI にしない) で照合。
- 第 2 の park 点は `exec_park_kbd()` (exec.c) + `appslot_park_kbd_check/commit`。R1 の条件の
  うち `kbd_gui_mode` は kbd.c、`g_cur_app && cpl3 && g_cur_frame` は exec.c が見る。
  **CPL=0 / フレーム無しは数えない** (正常な hlt 落ち)。数えるのは CUI 入れ子の子だけ。
- `exec_resume` が空の注入で返す負値は **-14 `OS32_ERR_AGAIN`** を新設した (既存に該当なし)。
  KAPI_SPEC §3-2 のネットワーク予約を -15 以降へ 1 つ下げた (ネットワークは未使用)。
- 決めたこと 2 つ (未レビュー): ① `kbd_trygetkey` と `kbd_has_key` は票が挙げていないので
  **無変更** — GUI 中は常に -1 / 0 を返す (注入リングを見ない)。端末経由で使う予定が
  出たら K7-W/A の前に足す。② 印は `parked_from_kbd` を新設し `parked_from_wait` と併存
  (取り違えは `STALE` + `bad_frame_count`)。
- 未確認: 実機 (受入 I1〜I5) は 1 つも未実施。`make` も配備も行っていない ([V4])。

## 8. 実装メモ (A) — K7-A 着地、2026-09-12

- **Enter は `\r` (0x0D)**。§6 の「`\n` または `\r`」は `\r` を採る: `drivers/kbd.c:108`
  の keymap が 0x0D で、常駐シェルの行入力 (`userland/shell/ui.c:497`) は 0x0D **だけ**を
  行末に見る。両対応の呼び手 (`cmd_script.c` の `read`、`cmds/less.c`、`lib/filer`) も通る。
- 変換は `userland/rust/t5a_display/src/inject.rs` (`no_std` 純関数、host 4 件)。
  印字可能キーは `GUI_EV_KEY` から注がない — gshell が KEY と TEXT を両方積む (`input.rs:436-450`)。
- **表示操作を矢印 / ROLL / HOME へ移した (票に無い判断、要レビュー)**: 読み手を取れた端末で
  ASCII の `j k g e q` を横取りすると、CUI プログラムへ渡した打鍵で画面が動き `q` で端末が落ちる。
- 実機・`make` は未実施 ([V4])。
## 9. 実装メモ (W) — K7-W 着地、2026-09-12

- 鍵待ちは **入力群**に入れた (`multiapp::key_ready` = `kbd_inject_pending() > 0 && exec_app_state(id) == 3`)。
  `kbd_inject_pending` を先に見るので、空のとき KAPI は 1 本で済む。D11 の規則と上界 (30) は不変。
- 指摘 A: `slot_of_owner` が `None` でも `WAIT_KEY` なら `forget` せず `exec_resume(k, 0)`。
  `OS32_ERR_AGAIN` (-14) はその周を譲るだけ (`save_turn` / `restore_turn` で turn・`last_run`・`input_streak` を巻き戻す)。
- ~~未着手: `session::owner_active` はスロットと窓しか見ないので、鍵待ちの CUI が生きたまま SHUTDOWN / SWITCH_CUI が通る (票外)。~~
  → **K7-W2 (2026-09-12、受入 I3 の不具合) で修正**。`owner_active` に `multiapp::slotless_live` (被追跡かつ `slot_of_owner` が `None`) を足し、
  `arm_quit` と `session::x3_cycle` が `multiapp::request_kill_slotless` を予約する — `Quit` を積む先 (スロットのリング) が無い 1 本は
  決裁 A3 の猶予を待たず、top-level の `drain_top_level` で `exec_kill` + `forget`。判定は KAPI を呼ばない
  (`arm_quit` の呼び元に X1 = op 66 が居るため、契約 T8)。ホスト試験は `wm_tests.rs` の
  `switch_cui_kills_a_slotless_key_waiting_app_that_cannot_be_sent_a_quit` (41 → 42 本)。実機・`make` は未実施 ([V4])。
## 10. 実機受入の記録 (PM / テスター)

| 受入 | 構成 | obs | 判定 |
|---|---|---|---|
| ゲート | K7-K `c6a775d` | `make clean/all/external/check` exit=0 ([ABI3]) | 合格 |
| 配備 | 15MB | NHD バックアップ後 `os32-cycle deploy` exit=0、vmkernel 459,388 B 一致、`ver` API v47 Build 10:31 | 合格 |
| **I4** (CUI 側) | 15MB | kselftest **50 → 61** (fail 0、注入 5 + 印の負例 6 = I5 の kselftest 分)、`v86 -t` OK、regress 6 本 obs 全通過 (CUI の `kbd_getchar` は従来どおり)。8MB は未実施 | **合格** (15MB) |
| **I1** 止まらない | 15MB、W `df1df96` + A `c37d6e3` (HostDrv → hsync、gshell 162,472 B) | 端末を出して Run `kbd_echo.bin` → `kbd_echo: type keys, 'q' to quit` が端末窓に出て gshell は動き続ける (`ring3_kbd_park_count` 1 → 4、クリック / Start が効く) | **合格** |
| **I2** 打鍵が届く | 同 | 端末窓にフォーカスして `abc` + Enter → `got 0x61 'a'` / `0x62 'b'` / `0x63 'c'` / `0x0D` が端末窓に戻る (`in=141B rec=20`)。`ring3_switch_count` 8、`ring3_resume_bad_frame_count` 0、`fault_kill_count` 0 | **合格** (ASCII。FEP 経由の日本語は未実施) |
| **I5** 切替点 | 同 | `ring3_resume_bad_frame_count` 0 / `ring3_park_reject_count` 0 のまま、負例は kselftest (`test_resume_mark` 6 項) | **合格** |
| I3 CTRL+STOP | 同 | kbd_echo は窓を持たないので CTRL+STOP (フォーカス窓宛) の対象にならない。Start → CUI mode の畳み込みで CUI に復帰 (回帰 6/6) するが、**ID 3 (kbd_echo) が `APP_STATE_WAIT_KEY` のまま AppSlot に残る** (`g_slot[3]`: state 3 / gui 1 / cpl3 1 / parked_from_kbd 1、`appslot_reclaim_count` は端末の 1 だけ)。原因は K7-W 申し送り 1 (`owner_active` がスロットと窓しか見ない) | **不合格 → K7-W2** (gshell の畳み込みでスロット無しの被追跡アプリを `exec_kill`) |
| **I3** 再試験 (K7-W2 `ffed0e5` + K6C-2 `253057d` 配備、vmkernel 459,467 B、kselftest 63 / 0) | 15MB | 端末 + kbd_echo (WAIT_KEY) の状態で Start → CUI mode → `g_slot[2]` / `[3]` とも state 0、`appslot_reclaim_count` 2 (`last_reclaim_id` 3)、CUI 復帰後のテキスト面はクリア済み、regress 6 本 obs 全通過 | **合格** |
| 不具合候補 | 同 | GUI 中の `kprintf` がテキスト VRAM にも描かれ、画面左上に残像 (`kbd_echo: type keys…`) が出る (`text_disp` 1)。シンク有効中は console.c の描画を抑止すべき → K6C-2 `253057d` で修正、再試験で残像なし (`scratchpad/i2b.png`) | 修正済み |

