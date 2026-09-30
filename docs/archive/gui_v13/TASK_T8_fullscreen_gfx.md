# T8 — full-screen GFX 復帰: 端末から起動した GFX プログラムが全画面を使い、終了で GUI に戻る

> 発行: PM (2026-09-12) / 状態: **受入完了 (2026-09-12)**

受入済み (2026-09-12、F1〜F8)。残は PEGC / Cirrus 構成の確認のみ。
親: [PLAN.md](../../tasks/gui/v13/PLAN.md) §1 (決裁 B: … → K7 → T7 → **full-screen GFX 復帰** → shell script → 設定 S0〜)。
前提: K5b (協調型 4 本)、K6C / K7 (端末、con_sink、kbd 待ちの park)、T7 (端末からの起動)。すべて main `3b7677b`。

## 0. いま起きること (確認済み)

- GFX プログラム (`gfx200_test` / `blit_test` / `tile_bench` / `gfx_demo200` など) は `libos32gfx_init(api)` → KAPI `gfx_init` /
  `gfx_init_200` で **画面を丸ごと取る** (`gfx/gfx_core.c:341`: バックエンド再選択、両ページの VRAM ゼロクリア、パレット初期化)。
  gshell 配下の GUI アプリは `libos32gfx_attach` で取り付くだけで `gfx_init` を呼ばない (gotcha §4-20)。
- `exec_start` は「最初の park」まで戻らない (`exec/exec.c:1548`)。K7 以前は GFX プログラムが `kbd_getchar` で `hlt` に入り
  走り切るまで戻らなかったので、抜けた後に gshell の `rc <= 0` の枝 (`userland/gshell/src/lib.rs:281`) が `gfx_init` +
  `invalidate_all_clients` + `composite_full` で GUI を戻した。
- **K7 の後**: `kbd_getchar` で park する (`WAIT_KEY`) ので `exec_start` は `rc > 0` で戻り、gshell が合成・present を続けて
  **GFX プログラムの画面を上書きする**。`rc > 0` の枝は復帰処理をしない (W-1) ので、プログラムが抜けても GUI は戻らない。
- OS32X ヘッダには **`OS32X_FLAG_GFX` (0x0001、`mkos32x --gfx`) が既にある**が、いまは誰も立てず、カーネルも見ていない。
  `OS32X_FLAG_FORCE_CPL0` (0x0004、`--cpl0`) は v86 / VDM 系の CPL=0 プログラムに付いていて `appslot_launch_is_app` が見ている。

## 1. ユーザー決裁 (2026-09-12)

- **VRAM を直接触る系 (v86 / VDM = `--cpl0`) は GUI からの直接実行を禁止**し、CUI に降りてから実行させる。
  VRAM を触らない DOS 的な CUI プログラム (CPL=3、出力は console.c 経由) は制限しない。
- **ユーザーランドは全部ビルドし直してよい** → ヘッダの宣言ビットを使う。
- 実装コストが高ければ「CUI のみの機能」で構わない (→ 下記のとおり低コストなので GUI からの全画面を実装する)。

## 2. 設計 (決裁反映後)

| # | 決定 | 担当 |
|---|---|---|
| D1 | **画面の所有者をカーネルが 1 つ持つ** (`g_gfx_owner`: 1 = シェル帯 (WM) / 2〜5 = アプリ)。CPL=3 の非シェル ID が `gfx_init` / `gfx_init_200` を呼んだら所有者をその ID にし、その ID の回収 (`exec_reclaim_owned`: 正常終了 / kill / fault / CTRL+STOP) で WM に戻す。**GUI 中 (con_sink 有効中) の `exec_start` は `OS32X_FLAG_FORCE_CPL0` のプログラムを `OS32_ERR_INVAL` で拒否** (CUI 専用。K5b A1 の「CPL=3 アプリが生きていたら拒否」を「GUI からは常に拒否」に広げる)。`gfx_init` を呼ばずに VRAM へ直接書く CPL=3 プログラムは「行儀の悪いプログラム」として扱い、守らない | K |
| D1a | **宣言ビット**: `OS32X_FLAG_GFX` を「全画面 GFX を使う」の宣言として使う。`build/app.conf` に 4 列目 `gfx` (省略 = 無し) を足し、`mkos32x` が `--gfx` を付ける。全画面を使う既存プログラム (`gfx200_test` / `blit_test` / `blit_test2` / `tile_bench` / `gfx_demo200` / `demo_tile` / `rotate_test` / `mgx_test` / `hello_gfx` 系 / `apps/` `game/` の GFX 物 — `libos32gfx_init` / `gfx_init` を呼ぶもの) に立てる。`tools/check_manifests.py` で「`gfx_init` を呼ぶのに宣言が無い」を検出できるなら足す (少なくとも app.conf の列の検査)。**カーネルは GUI 中に宣言の無い CPL=3 が `gfx_init` を呼んだら `OS32_ERR_INVAL` で拒否** (黙って画面を壊さない)。CUI 中は従来どおり何でも通す | ビルド系 + K |
| D2 | **落とす (ユーザー決裁)**。WM の present はカーネルで捨てない。全画面中に描かないのは WM の規律 (D4a) だけで守る | — |
| D3 | **KAPI v48**: `gfx_screen_owner(void) -> i32` (誰でも呼べる、値は D1)。WM は `exec_start` / `exec_resume` から戻った直後にこれを見て全画面モードに入る / 抜ける | K |
| D4 | **gshell の全画面モード**: 所有者 ≠ WM の間は (a) 合成・present をしない、(b) 入力はいつもどおりフォーカス窓 (= 端末) へ配る (端末が `kbd_inject`)、(c) マウスはタスクバー / Start を含めて無視、(d) **CTRL+STOP は所有者宛** (`exec_abort_clear` → `exec_kill(owner)`、K5c の分岐に 1 条件)。所有者が WM に戻ったら **`gfx_init` → パレット復元 → `install_system_palette` → `lease::reapply` → `invalidate_all_clients` → `composite_full`** (いまの `rc <= 0` の枝を関数にして両方から呼ぶ)。**入口の追加**: Run ダイアログ / 端末からの起動前に OS32X ヘッダを読み (`sys_open` + 40B 読み)、`FORCE_CPL0` なら `cui only: <名>` を出して起動しない (カーネルの拒否は最後の砦)。`FLAG_GFX` なら起動直後から全画面モードに入る (所有者の問い合わせと二重) | W (+ 端末 A は入口だけ) |
| D5 | **復帰は WM の `gfx_init` 任せ**: 復帰の `gfx_init` は WM が最初に選んだバックエンドで立ち上げ直す。プログラムが `gfx_init_200` にしていても 400 ラインに戻る。Cirrus のリレーは §4-21 の作法どおり | — |
| D6 | **プログラム側のコード変更は無し** (宣言ビットは app.conf / 再ビルドで付く)。GUI アプリ (`libos32gfx_attach`) は所有者を取らない | ビルド系 |
| D7 | 端末 (T7-A) は入口 (D4 の `cui only`) 以外は無変更。全画面中も接続モードで打鍵を注入し、EXIT でプロンプトへ | A |

メモリ: 所有者 1 語 + カウンタ 1 語。`MEMORY_BUDGET.md` に計上。KAPI v48 は `gfx_screen_owner` の 1 本 (KAPI_SPEC §3-2 に予約済み)。

将来 (この票の範囲外、記録のみ): VDM のコンソール出力を con_sink に流せば「文字だけの DOS プログラムを端末窓で」も可能になる。
宣言ビットは Start メニューの一覧や `ls` の表示 (種別) にも使える。

## 3. 独立レビューの残点 → ユーザー決裁 (2026-09-12): 1 = D1a で十分、2 = D2 は落とす、3 = D4(d) 採用、4 = D5 のまま (F7 で実測)

1. D1a の「宣言の無い CPL=3 の `gfx_init` を GUI 中は拒否」で、既存プログラムの取りこぼし (宣言し忘れ) は `check_manifests` で網を張るが、それで十分か。
2. D2 を保険として残すか落とすか。
3. D4(d) CTRL+STOP の宛先を所有者に切り替える例外 (K5c の「フォーカス窓宛」に対する)。
4. D5 の Cirrus / PEGC 480 ライン構成の復帰 (§4-21 の再 init の罠) — 受入 F6 で実測する。

## 4. 受入 (ゲスト、PM / テスター)

| ID | 試験 | 合格条件 |
|---|---|---|
| F1 | 200 ライン | 端末に `gfx200_test` + Enter → 画面全体がテストパターン (WM が上書きしない)。端末経由でキーを打つ → プログラムが終了 → **デスクトップ・端末窓・他の窓が全面再描画され、パレットが戻る**。`gfx_screen_owner` は 3 → 1 |
| F2 | 400 ライン | `blit_test` または `tile_bench` で同じ |
| F3 | CTRL+STOP | 全画面中に CTRL+STOP → プログラムだけ畳まれ (`appslot_reclaim_count` +1、`fault_kill_count` 不変)、GUI が戻る |
| F4 | 共存 | 端末のほかに `gui_demo` を出した状態で F1 → 復帰後に `gui_demo` の窓も描き直されている |
| F5 | CUI 専用の拒否 | 端末に `v86` + Enter → `cui only: v86` で起動しない。Run ダイアログからも同じ。CUI からは従来どおり `v86 -t` が通る |
| F6 | 宣言なし | 宣言ビットを外した試験バイナリ (テスト用に 1 本、`app.conf` で `gfx` 無し) を GUI 中に起動 → `gfx_init` が `ERR_INVAL` で画面は無事。CUI 中は動く |
| F7 | 回帰 | regress 6 本、v86 -t、Start → CUI mode。可能なら PEGC 構成で F1 (§3-4) |

## 4a. 実装メモ (ビルド系、2026-09-12)

- `build/app.conf` に 4 列目 `gfx` (省略 = 無し) を追加。`build/programs.mk` の `userland/%.bin` が `--gfx` を付ける。`sdk/mkos32x.py` は無変更 (`--gfx` = 0x0001 を単体実行で確認)。
- 立てた 14 本 = `gshell` / `bench` / `bench_scale2x` / `blit_test` / `blit_test2` / `demo_tile` / `font_test` / `gdi_test` / `gfx200_test` / `gfx_demo200` / `hal_test` / `hello_gfx` / `rotate_test` / `tile_bench`。`hal_test` `gdi_test` は行が無かったので `7 0 gfx` で新設。
- 票の候補のうち `mgx_test` `asset_demo` は gfx を呼ばないので立てず、逆に票が GUI 側に挙げた `gdi_test` は `libos32gfx_init` を直接呼ぶ単独 GFX なので立てた (`programs.mk` の注記どおり)。`demo_tile` / `tile_bench` は `tilemap_init` 経由。
- `tools/check_manifests.py` に §2b を追加: 4 列目の書式と「gfx_init 系を呼ぶのに宣言が無い」を検出。除外リストは持たず、`userland/lib` の呼び出しグラフを不動点まで辿る (コメントは除去)。宣言だけあって呼ばないものは `[--]` の警告。
- `gshell` にも宣言を立てた: シェル帯の WM 自身が復帰時に `gfx_init` を呼ぶので、K が「宣言の無い CPL=3」で弾く実装にした場合に GUI 復帰が死ぬのを避ける。`apps/` `game/` は submodule 未チェックアウトのため未対応 (各リポジトリ側で `--gfx`)。
## 4a. 実装メモ (K、2026-09-12)

- 所有者の表は `exec/appslot.c` (`g_gfx_owner` / `gfx_init_reject_count`)。判定材料 (走っている ID /
  `cpl3` / OS32X `hdr_flags`) が全部 AppSlot にあり、純関数 `appslot_gfx_claim_check()` としてホストで試験できるため。
- KAPI の門は `sdk/kapi.json` の `"target"` で `gfx_init` / `gfx_init_200` → `gfx_kapi_init(_200)`
  (`gfx/gfx_core.c`)。戻り型は `void` のまま、生成物は手で触っていない ([ABI1])。**KAPI v48 = `gfx_screen_owner` (スロット 192 / offset 0x308)**。
- D1 の cpl0 拒否は `appslot_cpl0_admit(is_shell, gui)` に `gui` を足して広げた (K5b A1)。CUI の `exec_run` は無変更。
- ホスト試験は `tools/tests/multiapp_impl_host.c` ケース 20 (33 検査、RED→GREEN 5 通り) → `tools/tests/t8_tdd.md`。
  kselftest に 2 項 (`test_gfx_owner`)。**`make`・配備・実機は未実施** ([V4]) — 受入 F1〜F7 は PM / テスターへ。

## 4a. 実装メモ (W、2026-09-12)

- 全画面モード = `userland/gshell/src/fullscreen.rs` (印 / 所有者 / 入る時点のパレット)。門は `wm::composite_rect`
  `composite_full` `queue_present` `flush_present` `flush_screen_dirty`・`input::capture_mouse`・`cursor::show` `move_to`・
  `fep::post_cycle` の入口 1 条件ずつ (dirty は溜める)。
- 入口 = `os32x.rs` の純関数 `classify` (cpl0 > gfx > 無し) + `sys_open` 40B 読み。`run_program` が cpl0 を `cui only: <名>` で断り
  (戻り値 0 = `RUN_REFUSED`)、gfx は `exec_start` 前に `arm`。所有者の問い合わせは `after_exec` (`exec_start` / `exec_resume` 直後)。
- 復帰 = `lib.rs::restore_screen` (`gfx_init` → `invalidate_all_clients` → パレット → `install_system_palette` → `lease::reapply` →
  露出 → `composite_full`)。`rc <= 0` の既存経路も同じ関数。CTRL+STOP の宛先は `multiapp::abort_target` (全画面中は所有者)。
- 端末 (D7) は `prompt::classify` (同じ判定の写し。共有ライブラリは増やさない) で `FORCE_CPL0` を `cui only: <名>` にして起動しない。
- ホスト試験は gshell 4 本 + `os32x` 2 本 (48 pass、実装を外すと RED を確認)、端末 1 本 (49 pass)。`app.conf` の gshell を KAPI 48 に。
  **`make`・配備・実機は未実施** ([V4]) — 受入 F1〜F7 は PM / テスターへ。

## 4a. 実装メモ (T8-2 W/A、2026-09-12)

- `classify` を `FORCE_CPL0 | CUI_ONLY` の OR に変えた (gshell `src/os32x.rs`、端末 `t5a_display/src/prompt.rs`)。表示は従来どおり `cui only: <名>`。
- `OS32X_FLAG_CUI_ONLY` (0x0010) は K/B が `os32_kapi_shared.h` へ足すまで各 crate にリテラルで持つ (コメントで T8-2 を指す)。
- ホスト試験に「`CUI_ONLY` 単独」「`CUI_ONLY | GFX`」を追加し RED → GREEN を確認 (gshell 48 pass / 端末 49 pass)。**`make`・配備・実機は未実施** ([V4])。
## 4a. 実装メモ (T8-2 K/B、2026-09-12)

- 宣言ビット `OS32X_FLAG_CUI_ONLY` = 0x0010 (`--cui-only` / app.conf 4 列目 `cui`)。立てたのは
  `userland/cmds/v86` と、グラフィック VRAM を直書きする `ring3_hello` / `ring3_fault` / `ring3_guard`
  (この 3 本は app.conf を持たないので `build/programs.mk` の explicit ルール側)。
- 砦は 2 枚: 純関数 `appslot_cui_only_admit(gui, hdr_flags)` を `exec_launch` の帯・池を動かす前に置いて
  GUI からの起動を断ち、古いバイナリ用に `v86_smoke_test` / `v86_disk_test` / `v86_boot2` (= `v86_boot`) が
  GUI 中は `v86_gui_refuse()` で `-1` (`v86_gui_reject_count`)。CUI は両方とも無変更。KAPI は無改版。
- **拒否 = そのアプリを畳む** (受入 F6 の実測: 断って続行させると描画 KAPI と VRAM 直書きで GUI が壊れた)。
  `gfx_init` の門と `v86_*` の門は `shell_print` で理由を出し `appslot_abort_request()` で `abort_req` を立て、
  syscall 出口の `ring3_abort_check()` に畳ませる。数は `gfx_init_reject_count` / `v86_gui_reject_count` のまま。
- ホスト試験は `multiapp_impl_host.c` ケース 21 (18 検査、RED→GREEN 4 通り) → `tools/tests/t8_tdd.md`。
  kselftest に 1 項追加。**`make`・配備・実機は未実施** ([V4]) — 受入 F5 / F6 の再試験は PM / テスターへ。

## 4a. 実装メモ (T8-3 W、2026-09-12)

- `APP_STATE_WAIT_POLL` = 4 は `multiapp.rs` にリテラルで持つ (`exec/appslot.h` へ足すのは T8-3 K)。規則は
  `poll_ready` (常に ready) + `pick_poll` (**`pick` が入力群も導出群も空と判定した周だけ**、ID 昇順、`LAUNCH` 保留 /
  `SessionAction` / kill 予約がある周は譲らない)。`ready` には入れない = D11-3a の上界 (30) も `should_park` も不変。
- スロット無しでも forget しない (`resume_one` の `WAIT_KEY` 分岐に並べ、`exec_resume(k, 0)`)。全画面中も同判断。
  K7-W2 の畳み (`is_slotless` / `request_kill_slotless`) は状態を見ない述語なので `session.rs` は**無変更で効く**。
- 検査: 模型 `multiapp_model_host.c` ケース 19、gshell `wm_tests.rs` T8-3 W。**`make`・配備・実機は未実施** ([V4])。

## 4a. 実装メモ (T8-3 W 追加修正、2026-09-12 — F8 不合格 + 復旧時の凍結)

- **F8 不合格の原因は `pick` ではなく `should_park`**。端末が `op_wait` の中に居ると WM の 1 周は
  `wm_cycle` + `sys_halt` で、`pick_poll` を呼ぶ点 (= top-level) へ行けない。`WAIT_POLL` を「譲る理由」に
  数えていなかったので端末は park せず、ポーリングの 1 本は永久に起きない。→ `poll_live(cur)` を
  `should_park` の (b) に足した (`if !other_ready && !poll_live(cur) { return false; }`)。**「最下位」は
  `pick` の側で守る**ので起こす順は不変、据え置き (`input_streak`) も従来どおり効く。上界 (30) は譲りが
  増えるだけなので伸びない。lib.rs の `sys_halt` 側は `resume_one` → `pick` → `pick_poll` が既に通るので不要。
- **CTRL+STOP で畳んだ後の凍結**: 所有者の問い合わせ (`after_exec`) が `exec_start` / `exec_resume` の
  直後にしか無く、`exec_kill` の経路を通らなかった。→ (1) `resume_one` の `drain_top_level` 成功直後、
  (2) 単独ループの `resume_one` / `sys_halt` の直前、どちらも**全画面中だけ** `after_exec` (KAPI 1 本)。
  `op_wait` の中では**呼ばない** — そこは `res_owner_get()` がアプリ ID なので `gfx_init` を呼ぶと
  `appslot_gfx_claim_check` が「宣言の無いアプリ」と見て**端末を畳む**。代わりに `should_park` の (a) に
  `fullscreen_restore_pending()` (全画面中 && `gfx_screen_owner()==1`) を足して park させ、top-level に返す。
- 検査: 模型ケース 19 を 16 検査に (19j〜19n を新規則へ)、gshell に T8-3 W 検査 4/5/7/8 を追加 (**56 pass**、
  4 通りの RED を実測)。**`make`・配備・実機は未実施** ([V4]) — F8 / F3 の再試験は PM / テスターへ。

## 5. 範囲外

- shell script、設定 S0〜。全画面プログラムと GUI アプリの同時描画 (排他が仕様)。VDM の端末化。
- 配備・コミット・push・エミュレータ・ローカル AI・ini・.env・`make` は禁止 (コーダー)。

## 6. 実機受入の記録 (PM / テスター、2026-09-12、K `f19dd00` + B `d73ceea` + W `732b07d` を NHD 配備、vmkernel 460,792 B、gshell 166,568 B、API v48、kselftest 66 / 0、15MB)

| 受入 | obs | 判定 |
|---|---|---|
| **F1** 前半 | 端末に `gfx200_test` + Enter → 200 ライン (400 ライン画面に縦 2 倍) のテストパターンが全画面に出て WM は上書きしない (`g_gfx_owner` 3)。端末経由の Space で `kbd_getchar` の park から復帰し次段 (FPS 計測) へ | **合格** |
| **F1** 後半 | FPS 計測ループが `kbd_trygetchar()` をポーリングしており、GUI 中は park しない (K7 R1 どおり) ため **WM に制御が戻らず端末からキーを注入できない**。プログラムは終了せず、CTRL+STOP でしか抜けられない | **不合格 (設計の穴 → §7 判断待ち)** |
| **F3** | 全画面中 (ポーリング中) に CTRL+STOP → 所有者だけ畳まれ (`appslot_last_reclaim_id` 3、`g_gfx_owner` 1)、デスクトップ・端末窓・プロンプト (`EXIT` レコード) まで復帰。**`fault_kill_count` が +1** (走行中のアプリへの abort は `exec_exit(EXEC_ERR_FAULT)` 経由で fault 扱い。park 中の kill は増えない) | **合格** (カウンタの註付き) |
| **F5** | 端末に `v86` + Enter → **起動してしまう** (使い方を表示して終了)。`v86.bin` の flags は 0x0 = CPL=3 プログラムで、V86 へは KAPI `v86_*` で入る。`FORCE_CPL0` の判定では捕まらない | **不合格 → T8-2** (`OS32X_FLAG_CUI_ONLY` 0x0010 を app.conf `cui` で立て、入口判定 + `exec_start` 拒否 + `v86_*` KAPI の GUI 中拒否) |
| 事故 | F6 の準備で CUI から `hsync` を実行 → HostDrv の古いビルドで NHD が戻った (§4-33)。`make deploy` → `hsync` で復旧、サイズ照合済み | — |
| **F6** | `gfx200_test` の flags ビットを落とした `gfx_noflag.bin` を端末から起動 → `gfx_init` / `gfx_init_200` は拒否 (`gfx_init_reject_count` 2、`g_gfx_owner` 1 のまま) されたが、**プログラムは続行して描画 KAPI で VRAM を描き、GUI を壊した** (上 200 ラインに FPS 計測の絵、CTRL+STOP で復旧) | **不合格 → T8-2 K に追加: 拒否 = そのアプリを畳む** (`v86_*` の GUI 中拒否も同じ) |
| **F5** 再試験 (T8-2 `5911d80` + `a5bdca7` 配備、vmkernel 461,075 B、kselftest 67 / 0) | 端末に `v86` + Enter → `cui only: v86` (`v86.bin` の flags 0x10)、起動しない (`appslot_reclaim_count` 不変) | **合格** |
| **F6** 再試験 | `gfx_noflag` → `Error: gfx_init without GFX declaration -> kill app` が端末に出て畳まれる (`gfx_init_reject_count` 1、`last_reclaim_id` 3)、GUI は無事、`EXIT` でプロンプト復帰 | **合格** |
| **F2 / F4** | 端末 + `gui_demo` (Widgets / Help) を出した状態で端末に `blit_test` + Enter → 全画面 (`g_gfx_owner` 4、WM は描かない)。ベンチ終了 → プログラムの `libos32gfx_shutdown` で所有者が 1 に戻り WM が `gfx_init` + 全面再描画 (デスクトップ / Help / 端末枠)。`kbd_getchar` の park に Space → `Done. Press any key.` まで端末に流れプロンプト復帰、`appslot_last_reclaim_id` 4、`fault_kill_count` 0。Widgets 窓は端末窓の下に隠れているだけ (Z 順) | **合格** |
| **F7** | Start → CUI mode で畳まれ (`g_gfx_owner` 1)、CUI から `v86 -t` result OK (CUI では拒否されない)、regress 6 本 obs 全通過 (kselftest 67 / 0)。PEGC / Cirrus 構成の F1 は未実施 | **合格** (15MB / pc98) |

**判定 (PM、2026-09-12)**: F1 前半 / F2 / F3 / F4 / F5 / F6 / F7 合格。**残: F1 後半 = `kbd_trygetchar` をポーリングする GFX プログラム
(`gfx200_test` の FPS 計測、`gfx_demo200` / `rotate_test` / `demo_tile`) は GUI 中に WM へ制御を返さず、端末からキーを注入できない
(CTRL+STOP でしか止まらない)。ユーザー判断待ち (協調的な yield を `kbd_trygetchar` に入れるか、仕様とするか)。**

## 7. D8 ポーリング型の協調 yield (ユーザー承認 2026-09-12) — T8-3 として K / W を発注

- **決定**: GUI 中 (`con_sink` 有効)、注入リングが空、**前回の譲りから PIT tick が進んでいる** (10ms に 1 回まで) の 3 条件が揃ったとき、
  `kbd_trygetchar` (と `kbd_trygetkey` / `kbd_has_key` の同型) は **1 周だけ WM へ譲る**: `exec_park` と同じ手順で止め、状態 `APP_STATE_WAIT_POLL` (= 4)、
  印 `parked_from_poll`。`exec_resume` は印を見て EAX に **-1** (キーなし) を書く — 注入リングに文字があれば `WAIT_KEY` と同じく 1 バイトを書く。
  条件が揃わなければ従来どおり即 -1 (park しない)。CUI は無変更。カウンタ `ring3_poll_yield_count`。
- **WM (gshell)**: `WAIT_POLL` は **常に ready、ただし優先度は最下位** (入力群 / Paint / Timer / Quit のどれも無ければ起こす。ID 昇順)。
  `slot_of_owner` が `None` でも forget しない (`WAIT_KEY` と同じ分岐)。D11 の上界は「最下位」なので不変 (他が ready な周は数えない)。
- **性能見積もり** (PM、命令数から): 譲り 1 回 ≈ 386DX-33 で 10〜15k サイクル (0.3〜0.45ms)、P100 で 5〜6k (0.05ms)。描画ループ 1 フレーム 1 回なら
  386 で 1% 前後、tick 制限により busy-wait でも最大 100 回/秒 = 386 で 3〜4%。キー到達の遅れは最大 tick 1 つ + WM 1 周 ≈ 11ms。
- **受入 F8**: 端末から `gfx200_test` → FPS 段で Space を打つ → プログラムが自分で終了して GUI 復帰 (CTRL+STOP を使わない)。`ring3_poll_yield_count` が
  FPS 段の秒数 × ≤100 で増える。FPS 表示の譲りあり / なし比較 (NP21/W 上の相対値、期待 1% 未満)。`gfx_demo200` / `rotate_test` / `demo_tile` も端末から終了できる。

### 実装メモ (T8-3 K、2026-09-12)

- K 側実装済み: 状態 `APP_STATE_WAIT_POLL` (= 4) / 印 `parked_from_poll` / `appslot_park_poll_check(now_tick)` +
  `_commit()` + `appslot_poll_yield_reset()` / `exec_park_poll(now_tick)` / `exec_resume` の poll 分岐 (空なら EAX = -1) /
  `kbd_trygetchar` + `kbd_trygetkey` の GUI 分岐 / カウンタ `ring3_poll_yield_count`。**KAPI は増やしていない (v48 のまま)**。
- 間引きの控えは **`check` 側で進める** (成立時だけだと、譲れない文脈のポーリングで `ring3_park_reject_count` が跳ねる)。順番は「間引き → 表」。
  `kbd_has_key` は真偽を返すので resume の EAX と噛み合わず、KAPI にも無いため譲りは入れていない (注入リングを見る枝だけ追加)。
- 試験: ホスト TDD ケース 22 (37 検査、RED 7 通り) + kselftest 2 項 → [`tools/tests/t8_tdd.md`](../../../tools/tests/t8_tdd.md) の「T8-3 K」節。`make` / 実機は未実施 ([V4])。
- **K だけでは動かない**: gshell が 4 を知らないと `slot_of_owner` が `None` の譲りを `forget` し、譲ったアプリが二度と起きない (`multiapp.rs:735`)。T8-3 W と同時に入れること。
| **F8** (T8-3 K `74f8015` + W `e6a04ef` 配備、vmkernel 461,706 B、kselftest 69 / 0) | 端末から `gfx200_test` → Space で FPS 段へ → 最初の `kbd_trygetchar` で `WAIT_POLL` (state 4、`parked_from_poll` 1、`ring3_poll_yield_count` 1) に park した後、**WM が起こさない** (画面は `FPS: 0` で凍結、WM は `sys_halt` で待つ)。原因: gshell は入力群 / 導出群に ready が無いと `pick()` を呼ばず halt するので、`pick()` の降り口の `pick_poll` に到達しない。CTRL+STOP (所有者宛) で復旧 | **不合格 → T8-3 W 追加修正** (halt の前に `WAIT_POLL` を起こす) |
| F8 の復旧で発見 | park 中 (`WAIT_POLL`) の所有者アプリへ CTRL+STOP → WM の `exec_kill(owner)` で畳まれ `g_gfx_owner` は 1 に戻るが、**WM は全画面モードから抜けず** (`after_exec` が exec_start / exec_resume の直後にしか無い)、誰も ready でないので `sys_halt` のまま画面が凍る (マウス・Start 不可、`/api/reset` で復旧)。F3 が通ったのはアプリが走行中で abort が exec_resume の戻りに乗ったため | **不合格 → T8-3 W 追加修正 (2)**: kill 直後と halt 直前にも所有者を見て復帰 |
| **F8** 再試験 (W 追加修正 `097cd43`、gshell 167,368 B を HostDrv → hsync) | 端末から `gfx200_test` → Space で FPS 段 → **FPS: 8 (譲りなしのときと同じ)**、`ring3_poll_yield_count` 126 (約 15 秒、フレームごとに 1 回 ≈ 8/s、tick 制限 100/s の内側)。Space で FPS 段が自力終了し `GFX 200-line mode test completed.` が端末に出てプロンプト復帰 (`last_reclaim_id` 3、`fault_kill_count` 0、`park_reject` 0) | **合格** |
| **F3** 再試験 (ポーリング中) | FPS 段 (WAIT_POLL と走行を tick ごとに往復) で CTRL+STOP → 所有者だけ畳まれ (`last_reclaim_id` 3)、デスクトップ・端末・プロンプトまで復帰 (kill 直後の所有者確認が効いた)。`fault_kill_count` +1 は abort 経路の註どおり | **合格** |
| **F7** 再試験 | Start → CUI mode、`v86 -t` result OK、regress 6 本 obs 全通過 (kselftest 69 / 0、API v48 Build 19:39) | **合格** |

**判定 (PM、2026-09-12)**: F1〜F8 合格。**T8 受入済み** (K `f19dd00` `5911d80` `74f8015`、B `d73ceea` + submodule、W `732b07d` `a5bdca7` `e6a04ef` `097cd43`)。
未実施: PEGC / Cirrus 構成 (§3-4)、8MB。譲りの性能: NP21/W 上で FPS 8 → 8 (相対劣化は観測できず)。
| 8MB (`ram-8mb`、pc98) | 端末を立てた後は 2 本目が NOMEM で入らない (T7 の 8MB と同じ) ため、端末からの全画面起動は 8MB では試験できない。CUI から `gfx200_test` を直接走らせる経路は従来どおり | 仕様どおり |
| PEGC 構成 | `gfxmode pegc` + リセットでも `hal_test` は pc98 のまま — K6-RAM 以後 `sys_mem_kb` が RAM 上端 (15MB 構成で 17MB) になり、`backend_pegc.c:219` の「上端 > 0xF00000 なら不可」が常に落ちる (15〜16MB は RAM でない穴なのに)。**K6-3 `1a559a0` で修正** (probe は「線形窓 15〜16MB に RAM が無いか」で判定) | K6 回帰 → 修正済み |
| **PEGC 構成 F1** (K6-3 配備、vmkernel 461,795 B、kselftest 69 / 0、`gfxmode pegc`) | GUI が 640×480 で上がる (`scrn_ymax` 480)。端末から `gfx200_test` → PEGC のネイティブ画面の上部に 200 ライン試験のパターン、Space で FPS 段 (FPS 6、`ring3_poll_yield_count` 49)、Space で自力終了 → **480 ラインの GUI と端末 (プロンプト、`completed.`) が復帰**、`g_gfx_owner` 1、`fault_kill_count` 0 | **合格** |
